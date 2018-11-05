#ifndef __RING_BUFFER_H
#define __RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>

void ring_buffer_init(void);

uint32_t ring_buffer_size(void);
bool ring_buffer_full(void);

uint32_t ring_buffer_read(void);
void ring_buffer_write(uint32_t frame);

uint32_t ring_buffer_underruns(void);
#endif
