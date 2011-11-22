/*
 * Copyright (C) 2009-2010 Felipe Contreras
 * Copyright (C) 2008-2010 Nokia Corporation
 *
 * Authors:
 * Felipe Contreras <felipe.contreras@nokia.com>
 * Marco Ballesio <marco.ballesio@nokia.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef DMM_BUFFER_H
#define DMM_BUFFER_H

#include <stdlib.h> /* for calloc, free */
#include <string.h> /* for memset */

#include "dsp_bridge.h"
#include "log.h"

#define ROUND_UP(num, scale) (((num) + ((scale) - 1)) & ~((scale) - 1))
#define PAGE_SIZE 0x1000

enum dma_data_direction {
	DMA_BIDIRECTIONAL,
	DMA_TO_DEVICE,
	DMA_FROM_DEVICE,
};

typedef struct {
	int handle;
	void *proc;
	void *data;
	void *allocated_data;
	size_t size;
	size_t len;
#if DSP_API >= 2
	size_t dma_len;
#endif
	void *reserve;
	void *map;
	bool need_copy;
	int dir;
} dmm_buffer_t;

static inline dmm_buffer_t *
dmm_buffer_new(int handle,
		void *proc,
		int dir)
{
	dmm_buffer_t *b;
	b = calloc(1, sizeof(*b));

	pr_debug(NULL, "%p", b);
	b->handle = handle;
	b->proc = proc;
	b->dir = dir;

	return b;
}

static inline void
dmm_buffer_free(dmm_buffer_t *b)
{
	pr_debug(NULL, "%p", b);
	if (!b)
		return;
	if (b->map)
		dsp_unmap(b->handle, b->proc, b->map);
	if (b->reserve)
		dsp_unreserve(b->handle, b->proc, b->reserve);
	free(b->allocated_data);
	free(b);
}

static inline void
dmm_buffer_begin(dmm_buffer_t *b,
		size_t len)
{
	pr_debug(NULL, "%p", b);
	if (len == 0)
		return;
#if DSP_API < 2
	if (b->dir == DMA_FROM_DEVICE)
		dsp_invalidate(b->handle, b->proc, b->data, len);
	else
		dsp_flush(b->handle, b->proc, b->data, len, 1);
#else
	if (b->dma_len == (size_t) -1)
		return;
	if (dsp_begin_dma(b->handle, b->proc, b->data, len, b->dir))
		b->dma_len = (size_t) -1;
	else
		b->dma_len = len;
#endif
}

static inline void
dmm_buffer_end(dmm_buffer_t *b,
		size_t len)
{
	pr_debug(NULL, "%p", b);
	if (len == 0)
		return;
#if DSP_API < 2
	if (b->dir != DMA_TO_DEVICE)
		dsp_invalidate(b->handle, b->proc, b->data, len);
#else
	if (b->dma_len == (size_t) -1)
		return;
	dsp_end_dma(b->handle, b->proc, b->data, b->dma_len, b->dir);
	b->dma_len = 0;
#endif
}

static inline void
dmm_buffer_map(dmm_buffer_t *b)
{
	size_t to_reserve;
	unsigned long attr;

	pr_debug(NULL, "%p", b);

	if (b->map)
		dsp_unmap(b->handle, b->proc, b->map);
	if (b->reserve)
		dsp_unreserve(b->handle, b->proc, b->reserve);
	/**
	 * @todo What exactly do we want to do here? Shouldn't the driver
	 * calculate this?
	 */
	to_reserve = ROUND_UP(b->size, PAGE_SIZE) + PAGE_SIZE;
	dsp_reserve(b->handle, b->proc, to_reserve, &b->reserve);
	switch (b->dir) {
	case DMA_TO_DEVICE:
		attr = DSP_IN_BUFFER; break;
	case DMA_FROM_DEVICE:
		attr = DSP_OUT_BUFFER; break;
	case DMA_BIDIRECTIONAL:
		attr = DSP_IN_BUFFER | DSP_OUT_BUFFER; break;
	default:
		attr = 0;
	}
	dsp_map(b->handle, b->proc, b->data, b->size, b->reserve, &b->map, attr);
}

static inline void
dmm_buffer_unmap(dmm_buffer_t *b)
{
	pr_debug(NULL, "%p", b);
	if (b->map) {
		dsp_unmap(b->handle, b->proc, b->map);
		b->map = NULL;
	}
	if (b->reserve) {
		dsp_unreserve(b->handle, b->proc, b->reserve);
		b->reserve = NULL;
	}
}

static inline void
dmm_buffer_allocate(dmm_buffer_t *b,
		size_t size)
{
	int alignment = b->dir == DMA_TO_DEVICE ? 0 : 128;
	pr_debug(NULL, "%p", b);
	free(b->allocated_data);
	if (alignment != 0) {
		b->size = ROUND_UP(size, alignment);
		if (posix_memalign(&b->allocated_data, alignment, b->size) != 0)
			b->allocated_data = NULL;
		b->data = b->allocated_data;
	}
	else {
		b->size = size;
		b->data = b->allocated_data = malloc(size);
	}
	b->len = size;
}

static inline void
dmm_buffer_use(dmm_buffer_t *b,
		void *data,
		size_t size)
{
	pr_debug(NULL, "%p", b);
	b->data = data;
	b->len = b->size = size;
}

static inline dmm_buffer_t *
dmm_buffer_calloc(int handle,
		void *proc,
		size_t size,
		int dir)
{
	dmm_buffer_t *tmp;
	tmp = dmm_buffer_new(handle, proc, dir);
	dmm_buffer_allocate(tmp, size);
	memset(tmp->data, 0, size);
	return tmp;
}

#endif /* DMM_BUFFER_H */
