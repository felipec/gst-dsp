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

#ifndef GST_DSP_MP4VDEC_H
#define GST_DSP_MP4VDEC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_MP4VDEC(obj) (GstDspMp4vDec *)(obj)
#define GST_DSP_MP4VDEC_TYPE (gst_dsp_mp4v_dec_get_type())
#define GST_DSP_MP4VDEC_CLASS(obj) (GstDspMp4vDecClass *)(obj)

/* #define TS_COUNT */

typedef struct GstDspMp4vDec GstDspMp4vDec;
typedef struct GstDspMp4vDecClass GstDspMp4vDecClass;

#include "dmm_buffer.h"
#include "sem.h"
#include "comp.h"

typedef struct {
	guint buffer_count;
	dmm_buffer_t *buffer;
	GSem *sem;
} du_port_t;

struct GstDspMp4vDec
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
	GComp *flush;
};

struct GstDspMp4vDecClass
{
	GstElementClass parent_class;
};

GType gst_dsp_mp4v_dec_get_type(void);

G_END_DECLS

#endif /* GST_DSP_MP4VDEC_H */
