#ifndef __PCM_H
#define __PCM_H

#include <stdint.h>
#include <pthread.h>

#define SAMPLE_RATE (44100)

/*
 *
 * At 44.1kHz and 256 frames per buffer, the callback is called ~172 times
 * per second.
 */
#define PA_FRAMES_PER_BUFFER (256)

#define WAV_HEADER_SIZE (44) /* bytes */

struct playback {
	volatile bool playback_ready;
	pthread_mutex_t playback_mutex;
	pthread_cond_t playback_cond;
	pthread_barrier_t pthread_barrier;
	pthread_mutex_t pcm_timer_mutex;
	pthread_cond_t pcm_timer_cv;
	volatile bool pcm_timer_expired;
};

int pcm_reader_start(char *path, uint32_t *frames, struct playback *playback);

#endif
