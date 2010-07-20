/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef GST_DSP_BASE_H
#define GST_DSP_BASE_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_BASE(obj) (GstDspBase *)(obj)
#define GST_DSP_BASE_TYPE (gst_dsp_base_get_type())
#define GST_DSP_BASE_CLASS(obj) (GstDspBaseClass *)(obj)
#define GST_DSP_BASE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_DSP_BASE_TYPE, GstDspBaseClass))

/* #define TS_COUNT */

typedef struct GstDspBase GstDspBase;
typedef struct GstDspBaseClass GstDspBaseClass;

typedef struct du_port_t du_port_t;

#include "dmm_buffer.h"
#include "sem.h"
#include "async_queue.h"

typedef void (*port_buffer_cb_t) (GstDspBase *base,
				  du_port_t *port,
				  dmm_buffer_t *p,
				  dmm_buffer_t *b);

struct du_port_t {
	guint index;
	dmm_buffer_t **buffers;
	dmm_buffer_t **comm; /**< arm-dsp communication structure */
	dmm_buffer_t **params;
	guint num_buffers;
	AsyncQueue *queue;
	port_buffer_cb_t send_cb;
	port_buffer_cb_t recv_cb;
	int dir;
};

struct GstDspBase {
	GstElement element;

	GstPad *sinkpad, *srcpad;

	int dsp_handle;
	void *proc;
	dsp_node_t *node;
	struct dsp_notification *events[3];

	GstFlowReturn status;
	unsigned long input_buffer_size;
	unsigned long output_buffer_size;
	GThread *dsp_thread, *out_thread;
	gboolean done;
	int deferred_eos;

	du_port_t *ports[2];
	dmm_buffer_t *alg_ctrl;
	GstClockTime ts_array[20];
	GstEvent *event_array[20];
	guint ts_in_pos, ts_out_pos, ts_push_pos;
	GMutex *ts_mutex;
	gulong ts_count;
	GSem *flush;
	guint alg;

	gboolean use_pad_alloc; /**< Use pad_alloc for output buffers. */
	guint dsp_error;

	void *(*create_node)(GstDspBase *base);
	gboolean (*parse_func)(GstDspBase *base, GstBuffer *buf);
	GstCaps *tmp_caps;

	/* hacks */
	guint skip_hack; /* don't push x number of buffers */
	guint skip_hack_2; /* don't process x number of buffers */
};

struct GstDspBaseClass {
	GstElementClass parent_class;

	gboolean (*sink_event)(GstDspBase *base, GstEvent *event);
	gboolean (*src_event)(GstDspBase *base, GstEvent *event);
};

GType gst_dsp_base_get_type(void);

du_port_t *du_port_new(guint index, int dir);
void du_port_free(du_port_t *p);
void du_port_alloc_buffers(du_port_t *p, guint num_buffers);

gboolean gstdsp_start(GstDspBase *self);
gboolean gstdsp_send_codec_data(GstDspBase *self, GstBuffer *buf);
void gstdsp_got_error(GstDspBase *self, guint id, const char *message);
void gstdsp_post_error(GstDspBase *self, const char *message);
void gstdsp_send_alg_ctrl(GstDspBase *self, dsp_node_t *node, dmm_buffer_t *b);

typedef void (*gstdsp_setup_params_func)(GstDspBase *base, dmm_buffer_t *b);

static inline void gstdsp_port_setup_params(GstDspBase *self,
					    du_port_t *p,
					    size_t size,
					    gstdsp_setup_params_func func)
{
	unsigned i;
	for (i = 0; i < p->num_buffers; i++) {
		dmm_buffer_t *b;
		b = dmm_buffer_calloc(self->dsp_handle,
				      self->proc, size, p->dir);
		if (func)
			func(self, b);
		dmm_buffer_map(b);
		p->params[i] = b;
	}
}

G_END_DECLS

#endif /* GST_DSP_BASE_H */
