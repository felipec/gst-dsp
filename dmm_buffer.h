/*
 * Copyright (C) 2009 Felipe Contreras
 * Copyright (C) 2008-2009 Nokia Corporation.
 *
 * Authors:
 * Felipe Contreras <felipe.contreras@nokia.com>
 * Marco Ballesio <marco.ballesio@nokia.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifndef DMM_BUFFER_H
#define DMM_BUFFER_H

#include <stdlib.h> /* for calloc, free */
#include <unistd.h> /* for getpagesize */

#include "dsp_bridge.h"
#include "log.h"

#define ROUND_UP(num, scale) (((num) + ((scale) - 1)) & ~((scale) - 1))

typedef struct
{
	int handle;
	void *node;
	void *data;
	void *allocated_data;
	size_t size;
	size_t len;
	void *reserve;
	void *map;
	bool need_copy;
	size_t alignment;
	void *user_data;
	bool used;
} dmm_buffer_t;

static inline dmm_buffer_t *
dmm_buffer_new(int handle,
	       void *node)
{
	dmm_buffer_t *b;
	b = calloc(1, sizeof(*b));

	pr_debug(NULL, "%p", b);
	b->handle = handle;
	b->node = node;
	b->alignment = 128;

	return b;
}

static inline void
dmm_buffer_free(dmm_buffer_t *b)
{
	pr_debug(NULL, "%p", b);
	if (!b)
		return;
	if (b->map)
		dsp_unmap(b->handle, b->node, b->map);
	if (b->reserve)
		dsp_unreserve(b->handle, b->node, b->reserve);
	free(b->allocated_data);
	free(b);
}

static inline void
dmm_buffer_map(dmm_buffer_t *b)
{
	pr_debug(NULL, "%p", b);
	if (b->map)
		dsp_unmap(b->handle, b->node, b->map);
	dsp_map(b->handle, b->node, b->data, b->size, b->reserve, &b->map, 0);
}

static inline void
dmm_buffer_unmap(dmm_buffer_t *b)
{
	pr_debug(NULL, "%p", b);
	if (!b->map)
		return;
	dsp_unmap(b->handle, b->node, b->map);
	b->map = NULL;
}

static inline void
dmm_buffer_clean(dmm_buffer_t *b,
		 size_t len)
{
	pr_debug(NULL, "%p", b);
	dsp_flush(b->handle, b->node, b->data, len, 1);
}

static inline void
dmm_buffer_invalidate(dmm_buffer_t *b,
		      size_t len)
{
	pr_debug(NULL, "%p", b);
	dsp_invalidate(b->handle, b->node, b->data, len);
}

static inline void
dmm_buffer_flush(dmm_buffer_t *b,
		 size_t len)
{
	pr_debug(NULL, "%p", b);
	dsp_flush(b->handle, b->node, b->data, len, 0);
}

static inline void
dmm_buffer_reserve(dmm_buffer_t *b,
		   size_t size)
{
	size_t to_reserve;
	size_t page_size;
	page_size = getpagesize();
	if (b->reserve) {
		if (ROUND_UP(size, page_size) <= ROUND_UP(b->size, page_size))
			goto leave;
		dsp_unreserve(b->handle, b->node, b->reserve);
	}
	to_reserve = ROUND_UP(size, page_size) + page_size;
	dsp_reserve(b->handle, b->node, to_reserve, &b->reserve);
leave:
	b->size = size;
}

static inline void
dmm_buffer_allocate(dmm_buffer_t *b,
		    size_t size)
{
	pr_debug(NULL, "%p", b);
	dmm_buffer_unmap(b);
	free(b->allocated_data);
	if (b->alignment != 0) {
		posix_memalign(&b->allocated_data, b->alignment, ROUND_UP(size, b->alignment));
		b->data = b->allocated_data;
	}
	else
		b->data = b->allocated_data = malloc(size);
	dmm_buffer_reserve(b, size);
	dmm_buffer_map(b);
}

static inline void
dmm_buffer_use(dmm_buffer_t *b,
	       void *data,
	       size_t size)
{
	pr_debug(NULL, "%p", b);
	dmm_buffer_unmap(b);
	b->data = data;
	dmm_buffer_reserve(b, size);
	dmm_buffer_map(b);
}

#endif /* DMM_BUFFER_H */
