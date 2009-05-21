/*
 * Copyright (C) 2009 Nokia Corporation.
 *
 * Author: Felipe Contreras <felipe.contreras@nokia.com>
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

#ifndef GST_DSP_DUMMY_H
#define GST_DSP_DUMMY_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_DUMMY(obj) (GstDspDummy *) (obj)
#define GST_DSP_DUMMY_TYPE (gst_dsp_dummy_get_type ())
#define GST_DSP_DUMMY_CLASS(obj) (GstDspDummyClass *) (obj)

typedef struct GstDspDummy GstDspDummy;
typedef struct GstDspDummyClass GstDspDummyClass;

#include "dmm_buffer.h"

struct GstDspDummy
{
	GstElement element;

	GstPad *sinkpad, *srcpad;

	int dsp_handle;
	void *proc;
	void *node;

	dmm_buffer_t *in_buffer, *out_buffer;
};

struct GstDspDummyClass
{
	GstElementClass parent_class;
};

GType gst_dsp_dummy_get_type(void);

G_END_DECLS

#endif /* GST_DSP_DUMMY_H */
