/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef UTIL_H
#define UTIL_H

#include "dsp_bridge.h"
#include "dmm_buffer.h"

struct _GstBuffer;

bool gstdsp_register(int dsp_handle,
		     const struct dsp_uuid *uuid,
		     int type,
		     const char *filename);

bool gstdsp_map_buffer(void *self,
		struct _GstBuffer *g_buf,
		dmm_buffer_t *d_buf);

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#endif /* UTIL_H */
