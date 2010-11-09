/*
 * Copyright (C) 2009-2010 Nokia Corporation
 *
 * Author: Felipe Contreras <felipe.contreras@nokia.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef GST_DSP_DUMMY_H
#define GST_DSP_DUMMY_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_DUMMY(obj) (GstDspDummy *) (obj)
#define GST_DSP_DUMMY_TYPE (gst_dsp_dummy_get_type())
#define GST_DSP_DUMMY_CLASS(obj) (GstDspDummyClass *) (obj)

typedef struct _GstDspDummy GstDspDummy;
typedef struct _GstDspDummyClass GstDspDummyClass;

#include "dmm_buffer.h"

struct _GstDspDummy {
	GstElement element;

	GstPad *sinkpad, *srcpad;

	int dsp_handle;
	void *proc;
	struct dsp_node *node;

	dmm_buffer_t *in_buffer, *out_buffer;
	struct dsp_notification *events[3];
	unsigned dsp_error;
};

struct _GstDspDummyClass {
	GstElementClass parent_class;
};

GType gst_dsp_dummy_get_type(void);

G_END_DECLS

#endif /* GST_DSP_DUMMY_H */
