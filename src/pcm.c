#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "portaudio.h"
#include "ring_buffer.h"
#include "wrapper.h"
#include "pcm.h"

static FILE *fp;
/* The total number of PCM frames in the WAV file. */
static uint32_t wave_frames;

static pthread_t pcm_timer_thread;
static pthread_t pcm_reader_thread;

static void *pcm_timer(void *arg)
{
	struct playback *playback = (struct playback *)arg;
	int ret;

	const struct timespec tim_req = {
	    .tv_sec = 0,
	    .tv_nsec =
		((float)PA_FRAMES_PER_BUFFER / (float)SAMPLE_RATE) * 1000000000L,
	};
	struct timespec tim_rem;

	wrap_barrier_wait(&playback->pthread_barrier);

	while (1) {
		if (nanosleep(&tim_req, &tim_rem) < 0) {
			fprintf(stderr, "Can't set up PCM reader tick\n");
		}

		wrap_mutex_lock(&playback->pcm_timer_mutex);

		playback->pcm_timer_expired = true;

		ret = pthread_cond_signal(&playback->pcm_timer_cv);
		if (ret) {
			fprintf(stderr, "Cannot signal cond (err=%d)\n", ret);
		}

		wrap_mutex_unlock(&playback->pcm_timer_mutex);
	}

	return NULL;
}

static void *pcm_reader(void *arg)
{
	int ret;
	long int size;
	uint32_t frame_i;
	uint32_t frame;

	struct playback *playback = (struct playback *)arg;

	/* Fill up the ring buffer before kicking off the playback */
	for (frame_i = 0; frame_i < wave_frames && frame_i < ring_buffer_size(); frame_i++) {

		size = fread(&frame, 1, sizeof(uint32_t), fp);
		if (size != sizeof(uint32_t)) {
			fprintf(stderr, "error reading file (%ld)\n", size);
		}
		ring_buffer_write(frame);
	}

	wrap_barrier_wait(&playback->pthread_barrier);

	/* Kick off the stream */

	wrap_mutex_lock(&playback->playback_mutex);
	playback->playback_ready = true;
	ret = pthread_cond_signal(&playback->playback_cond);
	if (ret) {
		fprintf(stderr, "Cannot signal cond (err=%d)\n", ret);
	}
	wrap_mutex_unlock(&playback->playback_mutex);

	/* Feed the ring buffer based on PCM timer tick */

	while (1) {
		wrap_mutex_lock(&playback->pcm_timer_mutex);

		/* wait for next PCM tick */
		while (playback->pcm_timer_expired == false) {
			ret =
			    pthread_cond_wait(&playback->pcm_timer_cv, &playback->pcm_timer_mutex);
			if (ret) {
				fprintf(stderr,
					"Cannot wait on cond (err=%d)\n", ret);
			}
		}

		playback->pcm_timer_expired = false;

		wrap_mutex_unlock(&playback->pcm_timer_mutex);

		/* Refill the ring buffer */

		for (; ring_buffer_full() == false && frame_i < wave_frames;
		     frame_i++) {

			size = fread(&frame, 1, sizeof(uint32_t), fp);
			if (size != sizeof(uint32_t)) {
				fprintf(stderr, "error reading file (%ld)\n", size);
			}

			ring_buffer_write(frame);
		}
	}

	return NULL;
}

int pcm_reader_start(char *path, uint32_t *frames, struct playback *playback)
{
	int ret;
	long int size;

	fp = fopen(path, "rb");
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
	*frames = wave_frames;

	ret = fseek(fp, WAV_HEADER_SIZE, SEEK_SET);
	if (ret) {
		fprintf(stderr, "cannot fseek file\n");
		exit(-1);
	}

	ret = pthread_create(&pcm_timer_thread, NULL, pcm_timer, playback);
	if (ret) {
		fprintf(stderr, "cannot create pcm timer\n");
		exit(-1);
	}
	return pthread_create(&pcm_reader_thread, NULL, pcm_reader, playback);

}

