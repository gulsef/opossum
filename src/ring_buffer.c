#include "ring_buffer.h"
#include "pcm.h"
#include <assert.h>

/* Size of the ring buffer in frames. Must be a power of two. */
#define RB_FRAMES (32 * PA_FRAMES_PER_BUFFER)

/* The ring buffer. Each ring buffer element holds a 16bit stereo frame. */
static uint32_t rb[RB_FRAMES];

/* Frames present in the ring buffer. */
static _Atomic volatile uint32_t frames_in_rb;

static volatile uint32_t underruns;

uint32_t ring_buffer_size(void)
{
	return RB_FRAMES;
}

bool ring_buffer_full(void)
{
	return frames_in_rb >= ring_buffer_size();
}

void ring_buffer_init()
{
	/* sanity check: ring buffer size must be a power of two */
	assert((RB_FRAMES & (RB_FRAMES - 1)) == 0);
}

void ring_buffer_write(uint32_t frame)
{
	/* Pointer to the first empty frame to be produced by the PCM reader. */
	static uint32_t in;

	rb[in] = frame;

	in = (in + 1) & (RB_FRAMES - 1);

	__sync_fetch_and_add(&frames_in_rb, 1);

	assert(frames_in_rb <= RB_FRAMES);
}

uint32_t ring_buffer_read(void)
{
	/* Pointer to the oldest populated frame to be consumed by PortAudio. */
	static uint32_t out;

	uint32_t frame;

	if (frames_in_rb == 0) {
		underruns++;
	}

	frame = rb[out];

	out = (out + 1) & (RB_FRAMES - 1);

	__sync_fetch_and_sub(&frames_in_rb, 1);

	return frame;
}

uint32_t ring_buffer_underruns()
{
	return underruns;
}
