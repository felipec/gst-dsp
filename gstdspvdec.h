/*
 * Copyright (C) 2009 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
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

#ifndef GST_DSP_VDEC_H
#define GST_DSP_VDEC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_VDEC(obj) (GstDspVDec *)(obj)
#define GST_DSP_VDEC_TYPE (gst_dsp_vdec_get_type())
#define GST_DSP_VDEC_CLASS(obj) (GstDspVDecClass *)(obj)

/* #define TS_COUNT */

typedef struct GstDspVDec GstDspVDec;
typedef struct GstDspVDecClass GstDspVDecClass;

#include "dmm_buffer.h"
#include "sem.h"

typedef struct {
	guint buffer_count;
	dmm_buffer_t *buffer;
	GSem *sem;
} du_port_t;

enum {
	GSTDSP_MPEG4VDEC,
	GSTDSP_H264DEC,
};

struct GstDspVDec
{
	GstElement element;

	GstPad *sinkpad, *srcpad;

	int dsp_handle;
	void *proc, *node;
	struct dsp_notification *events[3];

	GstFlowReturn status;
	unsigned long output_buffer_size;
	GThread *dsp_thread, *out_thread;
	gboolean done;

	du_port_t *port[2];
	dmm_buffer_t *out_buffer;
	GstClockTime ts;
	GMutex *ts_mutex;
#ifdef TS_COUNT
	gulong ts_count;
#endif
	GSem *flush;
	dmm_buffer_t *array[4];
	guint alg;
};

struct GstDspVDecClass
{
	GstElementClass parent_class;
};

GType gst_dsp_vdec_get_type(void);

G_END_DECLS

#endif /* GST_DSP_VDEC_H */
