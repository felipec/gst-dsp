/*
 * Copyright (C) 2011 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "gstdspbuffer.h"
#include "dmm_buffer.h"

typedef struct _GstDspBuffer GstDspBuffer;
typedef struct _GstDspBufferClass GstDspBufferClass;

static GstMiniObjectClass *parent_class;

struct _GstDspBuffer {
	GstBuffer parent;
	GstDspBase *base;
	struct td_buffer *tb;
};

struct _GstDspBufferClass {
	GstBufferClass parent_class;
};

static GType type;

GstBuffer *gst_dsp_buffer_new(GstDspBase *base, struct td_buffer *tb)
{
	GstBuffer *buf;
	GstDspBuffer *dsp_buf;
	dmm_buffer_t *b = tb->data;
	buf = (GstBuffer *) gst_mini_object_new(type);
	gst_buffer_set_caps(buf, GST_PAD_CAPS(base->srcpad));
	if (!tb->pinned)
		GST_BUFFER_MALLOCDATA(buf) = b->allocated_data;
	GST_BUFFER_DATA(buf) = b->data;
	GST_BUFFER_SIZE(buf) = b->len;
	dsp_buf = (GstDspBuffer *) buf;
	dsp_buf->tb = tb;
	dsp_buf->base = base;
	return buf;
}

static void finalize(GstMiniObject *obj)
{
	GstDspBuffer *dsp_buf = (GstDspBuffer *) obj;
	GstDspBase *base = dsp_buf->base;
	if (dsp_buf->tb->pinned) {
		if (G_UNLIKELY(g_atomic_int_get(&base->eos)))
			dsp_buf->tb->clean = true;
		base->send_buffer(base, dsp_buf->tb);
	}
	parent_class->finalize(obj);
}

static void class_init(void *g_class, void *class_data)
{
	GstMiniObjectClass *obj_class;
	obj_class = GST_MINI_OBJECT_CLASS(g_class);
	obj_class->finalize = finalize;
	parent_class = g_type_class_peek_parent(g_class);
}

GType
gst_dsp_buffer_get_type(void)
{
	if (G_UNLIKELY(type == 0)) {
		GTypeInfo type_info = {
			.class_size = sizeof(GstDspBufferClass),
			.class_init = class_init,
			.instance_size = sizeof(GstDspBuffer),
		};

		type = g_type_register_static(GST_TYPE_BUFFER, "GstDspBuffer", &type_info, 0);
	}

	return type;
}
