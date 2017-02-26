/*
 * $Id$
 *
 * This program uses the PortAudio Portable Audio Library.
 * For more information see: http://www.portaudio.com/
 * Copyright (c) 1999-2000 Ross Bencina and Phil Burk
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * The text above constitutes the entire PortAudio license; however,
 * the PortAudio community also makes the following non-binding requests:
 *
 * Any person wishing to distribute modifications to the Software is
 * requested to send the modifications to the original developer so that
 * they can be incorporated into the canonical version. It is also
 * requested that these non-binding requests be included along with the
 * license above.
 */

/*
 * Based on paex_sine.c example from the PortAudio repository.
 */

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "portaudio.h"
#include "wrapper.h"
/*
 * At 44.1kHz and 256 frames per buffer, the callback is called ~172 times
 * per second.
 */
#define FRAMES_PER_BUFFER (256)

/* Size of the ring buffer in frames. Must be a power of two. */
#define RB_FRAMES (32 * FRAMES_PER_BUFFER)

#define SAMPLE_RATE (44100)

#define WAV_HEADER_SIZE (44) /* bytes */

static volatile bool finished;
static pthread_mutex_t terminate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t terminate_cond = PTHREAD_COND_INITIALIZER;

static volatile bool playback_ready;
static pthread_mutex_t playback_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t playback_cond = PTHREAD_COND_INITIALIZER;

static pthread_barrier_t pthread_barrier;

/* The total number of PCM frames in the WAV file. */
static uint32_t wave_frames;

/* The ring buffer. Each ring buffer element holds a 16bit stereo frame. */
static uint32_t rb[RB_FRAMES];

/* Frames present in the ring buffer. */
static _Atomic volatile int32_t frames_in_rb;

static pthread_mutex_t pcm_timer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pcm_timer_cv = PTHREAD_COND_INITIALIZER;
static volatile bool pcm_timer_expired;

static volatile uint32_t underruns;

static int cb(const void *inputBuffer, void *outputBuffer,
	      unsigned long framesPerBuffer,
	      const PaStreamCallbackTimeInfo *timeInfo,
	      PaStreamCallbackFlags statusFlags, void *userData)
{
	(void)inputBuffer;
	(void)timeInfo;
	(void)statusFlags;
	(void)userData;

	/* Pointer to the oldest populated frame to be consumed by PortAudio. */
	static uint32_t out;
	/* Consumed frame (wrt the overall PCM file) */
	static uint32_t frame = 0;
	unsigned long i;
	uint32_t *portaudio_buffer = (uint32_t *)outputBuffer;

	for (i = 0; frame < wave_frames && i < framesPerBuffer; i++, frame++) {

		if (frames_in_rb == 0) {
			underruns++;
		}

		*portaudio_buffer++ = rb[out];

		out = (out + 1) & (RB_FRAMES - 1);

		__sync_fetch_and_sub(&frames_in_rb, 1);
	}

	if (frame == wave_frames) {
		return paComplete;
	}

	return paContinue;
}

static void cb_stream_finished(void *userData)
{
	(void)userData;

	wrap_mutex_lock(&terminate_mutex);

	finished = true;

	int ret = pthread_cond_signal(&terminate_cond);
	if (ret) {
		fprintf(stderr, "Cannot signal cond (err=%d)\n", ret);
	}

	wrap_mutex_unlock(&terminate_mutex);
}

static void print_usage(char *name) { printf("Usage: %s inputfile\n", name); }

static void *pcm_timer(void *t)
{
	(void)t;
	int ret;

	const struct timespec tim_req = {
	    .tv_sec = 0,
	    .tv_nsec =
		((float)FRAMES_PER_BUFFER / (float)SAMPLE_RATE) * 1000000000L,
	};
	struct timespec tim_rem;

	wrap_barrier_wait(&pthread_barrier);

	while (1) {
		if (nanosleep(&tim_req, &tim_rem) < 0) {
			fprintf(stderr, "Can't set up PCM reader tick\n");
		}

		wrap_mutex_lock(&pcm_timer_mutex);

		pcm_timer_expired = true;

		ret = pthread_cond_signal(&pcm_timer_cv);
		if (ret) {
			fprintf(stderr, "Cannot signal cond (err=%d)\n", ret);
		}

		wrap_mutex_unlock(&pcm_timer_mutex);
	}

	return NULL;
}

static void *pcm_reader(void *t)
{
	int ret;
	long int size;
	uint32_t frame;
	/* Pointer to the first empty frame to be produced by the PCM reader. */
	uint32_t in = 0;

	char *path = (char *)t;

	FILE *fp = fopen(path, "rb");
	if (fp == NULL) {
		fprintf(stderr, "cannot open file\n");
		exit(-1);
	}

	ret = fseek(fp, 0, SEEK_END);
	if (ret) {
		fprintf(stderr, "cannot fseek file\n");
		exit(-1);
	}
	size = ftell(fp);
	if (size < 0) {
		fprintf(stderr, "cannot ftell file\n");
		exit(-1);
	}

	/* sanity check: the file holds at least the WAV header, and the PCM
	 * stream is a muiltiple of the frame size.
	 */
	assert(size >= WAV_HEADER_SIZE);
	assert(((size - WAV_HEADER_SIZE) % sizeof(uint32_t)) == 0);

	wave_frames = (size - WAV_HEADER_SIZE) / sizeof(uint32_t);
	ret = fseek(fp, WAV_HEADER_SIZE, SEEK_SET);
	if (ret) {
		fprintf(stderr, "cannot fseek file\n");
		exit(-1);
	}

	/* Fill up the ring buffer before kicking off the playback */
	for (frame = 0; frame < wave_frames && frame < RB_FRAMES; frame++) {
		size = fread(&rb[in], 1, sizeof(uint32_t), fp);
		if (size != sizeof(uint32_t)) {
			fprintf(stderr, "error reading file (%ld)\n", size);
		}

		in = (in + 1) & (RB_FRAMES - 1);

		__sync_fetch_and_add(&frames_in_rb, 1);
	}

	wrap_barrier_wait(&pthread_barrier);

	/* Kick off the stream */

	wrap_mutex_lock(&playback_mutex);
	playback_ready = true;
	ret = pthread_cond_signal(&playback_cond);
	if (ret) {
		fprintf(stderr, "Cannot signal cond (err=%d)\n", ret);
	}
	wrap_mutex_unlock(&playback_mutex);

	/* Feed the ring buffer based on PCM timer tick */

	while (1) {
		wrap_mutex_lock(&pcm_timer_mutex);

		/* wait for next PCM tick */
		while (pcm_timer_expired == false) {
			ret =
			    pthread_cond_wait(&pcm_timer_cv, &pcm_timer_mutex);
			if (ret) {
				fprintf(stderr,
					"Cannot wait on cond (err=%d)\n", ret);
			}
		}

		pcm_timer_expired = false;

		wrap_mutex_unlock(&pcm_timer_mutex);

		assert(frames_in_rb <= RB_FRAMES);

		/* Refill the ring buffer */

		for (; frames_in_rb < RB_FRAMES && frame < wave_frames;
		     frame++) {
			size = fread(&rb[in], 1, sizeof(uint32_t), fp);
			if (size != sizeof(uint32_t)) {
				fprintf(stderr, "error reading file (%ld)\n",
					size);
			}

			in = (in + 1) & (RB_FRAMES - 1);

			__sync_fetch_and_add(&frames_in_rb, 1);
		}
	}

	return NULL;
}

int main(int argc, char *argv[])
{
	PaStreamParameters params;
	PaStream *stream;
	PaError err;
	int ret;
	pthread_t pcm_reader_thread;
	pthread_t pcm_timer_thread;

	/* sanity check: ring buffer size must be a power of two */
	assert((RB_FRAMES & (RB_FRAMES - 1)) == 0);

	if (argc < 2) {
		print_usage(argv[0]);
		exit(-1);
	}

	err = Pa_Initialize();
	if (err != paNoError)
		goto error;

	params.device = Pa_GetDefaultOutputDevice(); /* default output device */
	if (params.device == paNoDevice) {
		fprintf(stderr, "Error: No default output device.\n");
		goto error;
	}
	params.channelCount = 2;
	params.sampleFormat = paInt16;
	params.suggestedLatency =
	    Pa_GetDeviceInfo(params.device)->defaultLowOutputLatency;
	params.hostApiSpecificStreamInfo = NULL;

	err = Pa_OpenStream(&stream, NULL, &params, SAMPLE_RATE,
			    FRAMES_PER_BUFFER, paClipOff, cb, NULL);
	if (err != paNoError)
		goto error;

	err = Pa_SetStreamFinishedCallback(stream, &cb_stream_finished);
	if (err != paNoError)
		goto error;

	ret = pthread_barrier_init(&pthread_barrier, NULL, 2);
	if (ret) {
		fprintf(stderr, "Cannot initialize pthread barrier\n");
	}

	ret = 0;
	ret |= pthread_create(&pcm_timer_thread, NULL, pcm_timer, NULL);
	ret |= pthread_create(&pcm_reader_thread, NULL, pcm_reader, argv[1]);
	if (ret) {
		fprintf(stderr, "Cannot create PCM reader/timer threads.\n");
	}

	/*
	 * Wait for PCM reader to fill up the ring buffer before kicking off
	 * the playback.
	 */
	wrap_mutex_lock(&playback_mutex);
	while (!playback_ready) {
		ret = pthread_cond_wait(&playback_cond, &playback_mutex);
		if (ret) {
			fprintf(stderr, "Cannot wait on cond (err=%d)\n", ret);
		}
	}
	wrap_mutex_unlock(&playback_mutex);

	err = Pa_StartStream(stream);
	if (err != paNoError)
		goto error;

	wrap_mutex_lock(&terminate_mutex);
	while (!finished) {
		ret = pthread_cond_wait(&terminate_cond, &terminate_mutex);
		if (ret) {
			fprintf(stderr, "Cannot wait on cond (err=%d)\n", ret);
		}
	}
	wrap_mutex_unlock(&terminate_mutex);

	err = Pa_CloseStream(stream);
	if (err != paNoError)
		goto error;

	Pa_Terminate();

	printf("Underruns: %u\n", underruns);

	return err;
error:
	Pa_Terminate();
	fprintf(stderr, "An error occured while using the portaudio stream\n");
	fprintf(stderr, "Error number: %d\n", err);
	fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
	return err;
}
