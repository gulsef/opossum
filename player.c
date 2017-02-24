/** @file paex_sine.c
	@ingroup examples_src
	@brief Play a sine wave for several seconds.
	@author Ross Bencina <rossb@audiomulch.com>
    @author Phil Burk <philburk@softsynth.com>
*/
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
#include "portaudio.h"
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SAMPLE_RATE (44100)

#define WAV_HEADER_SIZE (44) /* bytes */

static uint16_t *wave;
static uint32_t wave_size;

static volatile bool finished;
static pthread_mutex_t terminate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t terminate_cond = PTHREAD_COND_INITIALIZER;

static void wrap_mutex_lock(pthread_mutex_t *mutex)
{
	int ret = pthread_mutex_lock(mutex);
	if (ret) {
		fprintf(stderr, "Cannot lock mutex (err=%d).\n", ret);
	}
}

static void wrap_mutex_unlock(pthread_mutex_t *mutex)
{
	int ret = pthread_mutex_unlock(mutex);
	if (ret) {
		fprintf(stderr, "Cannot unlock mutex (err=%d).\n", ret);
	}
}

static int cb(const void *inputBuffer, void *outputBuffer,
	      unsigned long framesPerBuffer,
	      const PaStreamCallbackTimeInfo *timeInfo,
	      PaStreamCallbackFlags statusFlags, void *userData)
{
	static uint32_t frame = 0;
	unsigned long i;
	const uint32_t frames = wave_size / 2;
	uint16_t *out = (uint16_t *)outputBuffer;

	for (i = 0; frame < frames && i < framesPerBuffer; i++) {
		/* feed left then right */
		*out++ = wave[frame++];
		*out++ = wave[frame++];
	}

	if (frame == frames) {
		return paComplete;
	}

	return paContinue;
}

static void cb_stream_finished(void *userData)
{
	wrap_mutex_lock(&terminate_mutex);

	finished = true;

	int ret = pthread_cond_broadcast(&terminate_cond);
	if (ret) {
		fprintf(stderr, "Cannot broadcast cond (err=%d)\n", ret);
	}

	wrap_mutex_unlock(&terminate_mutex);
}

void print_usage(char *name) { printf("Usage: %s inputfile\n", name); }

int main(int argc, char *argv[])
{
	PaStreamParameters params;
	PaStream *stream;
	PaError err;
	int ret;
	int i;
	size_t size;

	if (argc < 2) {
		print_usage(argv[0]);
		exit(-1);
	}

	FILE *fp = fopen(argv[1], "rb");
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

	assert(size >= WAV_HEADER_SIZE);

	wave_size = size - WAV_HEADER_SIZE;
	ret = fseek(fp, WAV_HEADER_SIZE, SEEK_SET);
	if (ret) {
		fprintf(stderr, "cannot fseek file\n");
		exit(-1);
	}

	wave = (uint16_t *)malloc(wave_size);
	if (wave == NULL) {
		fprintf(stderr, "OOM\n");
		exit(-1);
	}

	size = fread(wave, 1, wave_size, fp);
	if (size != wave_size) {
		fprintf(stderr, "error reading file (%d)\n", size);
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

	err = Pa_OpenStream(&stream, NULL, &params, SAMPLE_RATE, 0, paClipOff,
			    cb, NULL);
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

	return err;
error:
	Pa_Terminate();
	fprintf(stderr, "An error occured while using the portaudio stream\n");
	fprintf(stderr, "Error number: %d\n", err);
	fprintf(stderr, "Error message: %s\n", Pa_GetErrorText(err));
	return err;
}
