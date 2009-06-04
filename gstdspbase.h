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

#ifndef GST_DSP_BASE_H
#define GST_DSP_BASE_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_BASE(obj) (GstDspBase *)(obj)
#define GST_DSP_BASE_TYPE (gst_dsp_base_get_type())
#define GST_DSP_BASE_CLASS(obj) (GstDspBaseClass *)(obj)

/* #define TS_COUNT */

typedef struct GstDspBase GstDspBase;
typedef struct GstDspBaseClass GstDspBaseClass;

typedef struct du_port_t du_port_t;

#include "dmm_buffer.h"
#include "sem.h"

struct du_port_t {
	dmm_buffer_t *buffer, *param;
	GSem *sem;
	void (*send_cb)(GstDspBase *base, du_port_t *port);
};

struct GstDspBase
{
	GstElement element;

	GstPad *sinkpad, *srcpad;

	int dsp_handle;
	void *proc, *node;
	struct dsp_notification *events[3];

	GstFlowReturn status;
	unsigned long input_buffer_size;
	unsigned long output_buffer_size;
	GThread *dsp_thread, *out_thread;
	gboolean done;

	du_port_t *ports[2];
	dmm_buffer_t *out_buffer;
	dmm_buffer_t *alg_ctrl;
	GstClockTime ts_array[20];
	guint ts_in_pos, ts_out_pos;
	GMutex *ts_mutex;
#ifdef TS_COUNT
	gulong ts_count;
#endif
	GSem *flush;
	dmm_buffer_t *array[10];
	guint alg;

	gboolean use_pad_alloc; /**< Use pad_alloc for output buffers. */
	gboolean use_map_cache;
	guint dsp_error;

	void *(*create_node)(GstDspBase *base);
	gboolean (*parse_func)(GstDspBase *base, GstBuffer *buf);
	GstCaps *tmp_caps;
};

struct GstDspBaseClass
{
	GstElementClass parent_class;
};

GType gst_dsp_base_get_type(void);

gboolean gstdsp_start(GstDspBase *self);
void gstdsp_post_error(GstDspBase *self, const char *message);

G_END_DECLS

#endif /* GST_DSP_BASE_H */
