/*
 * Copyright (C) 2009-2010 Nokia Corporation
 *
 * Author: Felipe Contreras <felipe.contreras@nokia.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "gstdspdummy.h"
#include "plugin.h"
#include "util.h"

#include "dsp_bridge.h"

#include <string.h> /* for memcpy */

#define GST_CAT_DEFAULT gstdsp_debug

static GstElementClass *parent_class;

static GstCaps *
generate_src_template(void)
{
	GstCaps *caps;

	caps = gst_caps_new_any();

	return caps;
}

static GstCaps *
generate_sink_template(void)
{
	GstCaps *caps;

	caps = gst_caps_new_any();

	return caps;
}

static inline void *
create_node(int dsp_handle,
	    void *proc)
{
	dsp_node_t *node;
	const dsp_uuid_t dummy_uuid = { 0x3dac26d0, 0x6d4b, 0x11dd, 0xad, 0x8b,
		{ 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66 } };

	if (!gstdsp_register(dsp_handle, &dummy_uuid, DSP_DCD_NODETYPE, "dummy.dll64P")) {
		GST_ERROR("dsp node register failed");
		return NULL;
	}

	if (!gstdsp_register(dsp_handle, &dummy_uuid, DSP_DCD_LIBRARYTYPE, "dummy.dll64P")) {
		GST_ERROR("dsp node register failed");
		return NULL;
	}

	if (!dsp_node_allocate(dsp_handle, proc, &dummy_uuid, NULL, NULL, &node)) {
		GST_ERROR("dsp node allocate failed");
		return NULL;
	}

	if (!dsp_node_create(dsp_handle, node)) {
		GST_ERROR("dsp node create failed");
		return NULL;
	}

	GST_INFO("dsp node created");

	return node;
}

static inline bool
destroy_node(int dsp_handle,
	     dsp_node_t *node)
{
	if (node) {
		if (!dsp_node_free(dsp_handle, node)) {
			GST_ERROR("dsp node free failed");
			return false;
		}

		GST_INFO("dsp node deleted");
	}

	return true;
}

static gboolean
dsp_init(GstDspDummy *self)
{
	int dsp_handle;

	self->dsp_handle = dsp_handle = dsp_open();

	if (dsp_handle < 0) {
		GST_ERROR("dsp open failed");
		return FALSE;
	}

	if (!dsp_attach(dsp_handle, 0, NULL, &self->proc)) {
		GST_ERROR("dsp attach failed");
		goto fail;
	}

	self->node = create_node(dsp_handle, self->proc);
	if (!self->node) {
		GST_ERROR("dsp node creation failed");
		goto fail;
	}

	return TRUE;

fail:
	if (self->proc) {
		if (!dsp_detach(dsp_handle, self->proc))
			GST_ERROR("dsp detach failed");
		self->proc = NULL;
	}

	if (self->dsp_handle >= 0) {
		if (dsp_close(dsp_handle) < 0)
			GST_ERROR("dsp close failed");
		self->dsp_handle = -1;
	}

	return FALSE;
}

static gboolean
dsp_deinit(GstDspDummy *self)
{
	gboolean ret = TRUE;

	if (self->node) {
		if (!destroy_node(self->dsp_handle, self->node)) {
			GST_ERROR("dsp node destroy failed");
			ret = FALSE;
		}
		self->node = NULL;
	}

	if (self->proc) {
		if (!dsp_detach(self->dsp_handle, self->proc)) {
			GST_ERROR("dsp detach failed");
			ret = FALSE;
		}
		self->proc = NULL;
	}

	if (self->dsp_handle >= 0) {
		if (dsp_close(self->dsp_handle) < 0) {
			GST_ERROR("dsp close failed");
			ret = FALSE;
		}
		self->dsp_handle = -1;
	}

	return ret;
}

static inline void
configure_dsp_node(int dsp_handle,
		   dsp_node_t *node,
		   dmm_buffer_t *input_buffer,
		   dmm_buffer_t *output_buffer)
{
	dsp_send_message(dsp_handle, node, 0,
			 (uint32_t) input_buffer->map,
			 (uint32_t) output_buffer->map);
}

static gboolean
_dsp_start(GstDspDummy *self)
{
	if (!dsp_node_run(self->dsp_handle, self->node)) {
		GST_ERROR("dsp node run failed");
		return FALSE;
	}

	GST_INFO("dsp node running");

	self->in_buffer = dmm_buffer_new(self->dsp_handle, self->proc, DMA_TO_DEVICE);
	self->out_buffer = dmm_buffer_new(self->dsp_handle, self->proc, DMA_FROM_DEVICE);

	self->in_buffer->alignment = 0;

	return TRUE;
}

static gboolean
_dsp_stop(GstDspDummy *self)
{
	unsigned long exit_status;

	dmm_buffer_free(self->out_buffer);
	dmm_buffer_free(self->in_buffer);

	if (!dsp_node_terminate(self->dsp_handle, self->node, &exit_status)) {
		GST_ERROR("dsp node terminate failed: %lx", exit_status);
		return FALSE;
	}

	GST_INFO("dsp node terminated");

	return TRUE;
}

static GstStateChangeReturn
change_state(GstElement *element,
	     GstStateChange transition)
{
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
	GstDspDummy *self;

	self = GST_DSP_DUMMY(element);

	switch (transition) {
	case GST_STATE_CHANGE_NULL_TO_READY:
		if (!dsp_init(self)) {
			GST_ERROR("dsp init failed");
			return GST_STATE_CHANGE_FAILURE;
		}

		break;

	case GST_STATE_CHANGE_READY_TO_PAUSED:
		if (!_dsp_start(self)) {
			GST_ERROR("dsp start failed");
			return GST_STATE_CHANGE_FAILURE;
		}

		break;

	default:
		break;
	}

	ret = parent_class->change_state(element, transition);

	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition) {
	case GST_STATE_CHANGE_PAUSED_TO_READY:
		if (!_dsp_stop(self)) {
			GST_ERROR("dsp stop failed");
			return GST_STATE_CHANGE_FAILURE;
		}

		break;

	case GST_STATE_CHANGE_READY_TO_NULL:
		if (!dsp_deinit(self)) {
			GST_ERROR("dsp deinit failed");
			return GST_STATE_CHANGE_FAILURE;
		}

		break;

	default:
		break;
	}

	return ret;
}

static inline void
map_buffer(GstDspDummy *self,
	   GstBuffer *g_buf,
	   dmm_buffer_t *d_buf)
{
#if 0
	if (d_buf->alignment == 0 ||
	    (unsigned long) GST_BUFFER_DATA(g_buf) % d_buf->alignment == 0)
	{
		if (d_buf->data != GST_BUFFER_DATA(g_buf))
			dmm_buffer_use(d_buf, GST_BUFFER_DATA(g_buf), GST_BUFFER_SIZE(g_buf));
		d_buf->user_data = g_buf;
		return;
	}

	if (d_buf->alignment != 0) {
		GST_WARNING("buffer not aligned: %p, %lu",
			    GST_BUFFER_DATA(g_buf),
			    (unsigned long) GST_BUFFER_DATA(g_buf) % d_buf->alignment);
	}
#endif

	/* reallocate? */
	if (!d_buf->allocated_data ||
	    d_buf->size > GST_BUFFER_SIZE(g_buf)) {
		dmm_buffer_allocate(d_buf, GST_BUFFER_SIZE(g_buf));
	}
	d_buf->need_copy = true;
}

static GstFlowReturn
pad_chain(GstPad *pad,
	  GstBuffer *buf)
{
	GstDspDummy *self;
	GstFlowReturn ret;
	GstBuffer *out_buf;

	self = GST_DSP_DUMMY(GST_OBJECT_PARENT(pad));

	ret = gst_pad_alloc_buffer_and_set_caps(self->srcpad,
						GST_BUFFER_OFFSET_NONE,
						GST_BUFFER_SIZE(buf),
						GST_BUFFER_CAPS(buf),
						&out_buf);

	if (G_UNLIKELY(ret != GST_FLOW_OK)) {
		GST_ERROR_OBJECT(self, "couldn't allocate buffer");
		ret = GST_FLOW_ERROR;
		goto leave;
	}

	/* map dsp to gpp address */
	map_buffer(self, buf, self->in_buffer);
	map_buffer(self, out_buf, self->out_buffer);

	configure_dsp_node(self->dsp_handle, self->node, self->in_buffer, self->out_buffer);

	if (self->in_buffer->need_copy) {
		memcpy(self->in_buffer->data, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
		self->in_buffer->need_copy = false;
	}

	{
		dsp_msg_t msg;

		dmm_buffer_map(self->in_buffer);
		dmm_buffer_map(self->out_buffer);
		msg.cmd = 1;
		msg.arg_1 = self->in_buffer->size;
		dsp_node_put_message(self->dsp_handle, self->node, &msg, -1);
		dsp_node_get_message(self->dsp_handle, self->node, &msg, -1);
		dmm_buffer_unmap(self->out_buffer);
		dmm_buffer_unmap(self->in_buffer);
	}

	if (self->out_buffer->need_copy) {
		memcpy(GST_BUFFER_DATA(out_buf), self->out_buffer->data, GST_BUFFER_SIZE(out_buf));
		self->out_buffer->need_copy = false;
	}

	GST_BUFFER_TIMESTAMP(out_buf) = GST_BUFFER_TIMESTAMP(buf);

	ret = gst_pad_push(self->srcpad, out_buf);

leave:
	gst_buffer_unref(buf);

	return ret;
}

static void
instance_init(GTypeInstance *instance,
	      gpointer g_class)
{
	GstDspDummy *self;
	GstElementClass *element_class;

	element_class = GST_ELEMENT_CLASS(g_class);
	self = GST_DSP_DUMMY(instance);

	self->sinkpad =
		gst_pad_new_from_template(gst_element_class_get_pad_template(element_class, "sink"), "sink");

	gst_pad_set_chain_function(self->sinkpad, pad_chain);

	self->srcpad =
		gst_pad_new_from_template(gst_element_class_get_pad_template(element_class, "src"), "src");

	gst_pad_use_fixed_caps(self->srcpad);

	gst_element_add_pad(GST_ELEMENT(self), self->sinkpad);
	gst_element_add_pad(GST_ELEMENT(self), self->srcpad);
}

static void
base_init(gpointer g_class)
{
	GstElementClass *element_class;
	GstPadTemplate *template;

	element_class = GST_ELEMENT_CLASS(g_class);

	gst_element_class_set_details_simple(element_class,
					     "DSP dummy element",
					     "None",
					     "Copies the input to the output",
					     "Felipe Contreras");

	template = gst_pad_template_new("src", GST_PAD_SRC,
					GST_PAD_ALWAYS,
					generate_src_template());

	gst_element_class_add_pad_template(element_class, template);
	gst_object_unref(template);

	template = gst_pad_template_new("sink", GST_PAD_SINK,
					GST_PAD_ALWAYS,
					generate_sink_template());

	gst_element_class_add_pad_template(element_class, template);
	gst_object_unref(template);
}

static void
class_init(gpointer g_class,
	   gpointer class_data)
{
	GstElementClass *gstelement_class;

	parent_class = g_type_class_peek_parent(g_class);
	gstelement_class = GST_ELEMENT_CLASS(g_class);

	gstelement_class->change_state = change_state;
}

GType
gst_dsp_dummy_get_type(void)
{
	static GType type;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo type_info = {
			.class_size = sizeof(GstDspDummyClass),
			.class_init = class_init,
			.base_init = base_init,
			.instance_size = sizeof(GstDspDummy),
			.instance_init = instance_init,
		};

		type = g_type_register_static(GST_TYPE_ELEMENT, "GstDspDummy", &type_info, 0);
	}

	return type;
}
