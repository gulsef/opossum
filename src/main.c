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
#include "pcm.h"
#include "ring_buffer.h"
#include "wrapper.h"

static volatile bool finished;
static pthread_mutex_t terminate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t terminate_cond = PTHREAD_COND_INITIALIZER;

static struct playback playback = {
	.playback_mutex = PTHREAD_MUTEX_INITIALIZER,
	.playback_cond = PTHREAD_COND_INITIALIZER,
	.pcm_timer_mutex = PTHREAD_MUTEX_INITIALIZER,
	.pcm_timer_cv = PTHREAD_COND_INITIALIZER
};

static int cb(const void *inputBuffer, void *outputBuffer,
	      unsigned long framesPerBuffer,
	      const PaStreamCallbackTimeInfo *timeInfo,
	      PaStreamCallbackFlags statusFlags, void *userData)
{
	(void)inputBuffer;
	(void)timeInfo;
	(void)statusFlags;
	uint32_t wave_frames = *((uint32_t *)userData);

	/* Consumed frame (wrt the overall PCM file) */
	static uint32_t frame = 0;
	unsigned long i;
	uint32_t *portaudio_buffer = (uint32_t *)outputBuffer;

	for (i = 0; frame < wave_frames && i < framesPerBuffer; i++, frame++) {

		*portaudio_buffer++ = ring_buffer_read();
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

int main(int argc, char *argv[])
{
	PaStreamParameters params;
	PaStream *stream;
	PaError err;
	int ret;
	uint32_t wave_frames;

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

	ring_buffer_init();

	ret = pthread_barrier_init(&playback.pthread_barrier, NULL, 2);
	if (ret) {
		fprintf(stderr, "Cannot initialize pthread barrier\n");
	}

	ret = pcm_reader_start(argv[1], &wave_frames, &playback);
	if (ret) {
		fprintf(stderr, "Cannot create PCM reader/timer threads.\n");
	}

	/*
	 * Wait for PCM reader to fill up the ring buffer before kicking off
	 * the playback.
	 */
	wrap_mutex_lock(&playback.playback_mutex);
	while (!playback.playback_ready) {
		ret = pthread_cond_wait(&playback.playback_cond, &playback.playback_mutex);
		if (ret) {
			fprintf(stderr, "Cannot wait on cond (err=%d)\n", ret);
		}
	}
	wrap_mutex_unlock(&playback.playback_mutex);

	err = Pa_OpenStream(&stream, NULL, &params, SAMPLE_RATE,
			    PA_FRAMES_PER_BUFFER, paClipOff, cb, &wave_frames);
	if (err != paNoError)
		goto error;

	err = Pa_SetStreamFinishedCallback(stream, &cb_stream_finished);
	if (err != paNoError)
		goto error;

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

	printf("Underruns: %u\n", ring_buffer_underruns());

	return err;
error:
	Pa_Terminate();
	fprintf(stderr, "An error occured while using the portaudio stream\n");
	fprintf(stderr, "Error number: %d\n", err);
	fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
	return err;
}
