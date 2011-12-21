/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "util.h"

#include <glib.h>
#include <gst/gst.h>

bool gstdsp_register(int dsp_handle,
		     const struct dsp_uuid *uuid,
		     int type,
		     const char *filename)
{
	gchar *path;
	path = g_build_filename(DSPDIR, filename, NULL);
	if (!dsp_register(dsp_handle, uuid, type, path)) {
		g_free(path);
		return false;
	}

	g_free(path);
	return true;
}

static inline bool
buffer_is_aligned(void *buf_data, size_t buf_size, dmm_buffer_t *b)
{
	int alignment = b->dir == DMA_TO_DEVICE ? 0 : 128;
	if (alignment == 0)
		return true;
	if ((size_t) buf_data % alignment != 0)
		return false;
	if (((size_t) buf_data + buf_size) % alignment != 0)
		return false;
	return true;
}

bool gstdsp_map_buffer(void *self,
		GstBuffer *buf,
		dmm_buffer_t *b)
{
	if (buffer_is_aligned(buf->data, buf->size, b)) {
		dmm_buffer_use(b, buf->data, buf->size);
		gst_buffer_ref(buf);
		return true;
	}

	if (b->dir != DMA_TO_DEVICE) {
		pr_warning(self, "buffer not aligned: %p-%p",
			   buf->data,
			   buf->data + buf->size);
	}

	dmm_buffer_allocate(b, buf->size);
	b->need_copy = true;
	return false;
}
