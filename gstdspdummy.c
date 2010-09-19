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
create_node(GstDspDummy *self)
{
	int dsp_handle = self->dsp_handle;
	void *proc = self->proc;
	struct dsp_node *node;
	const struct dsp_uuid dummy_uuid = { 0x3dac26d0, 0x6d4b, 0x11dd, 0xad, 0x8b,
		{ 0x08, 0x00, 0x20, 0x0c, 0x9a, 0x66 } };

	if (!gstdsp_register(dsp_handle, &dummy_uuid, DSP_DCD_NODETYPE, "test.dll64P")) {
		pr_err(self, "dsp node register failed");
		return NULL;
	}

	if (!gstdsp_register(dsp_handle, &dummy_uuid, DSP_DCD_LIBRARYTYPE, "test.dll64P")) {
		pr_err(self, "dsp node register failed");
		return NULL;
	}

	if (!dsp_node_allocate(dsp_handle, proc, &dummy_uuid, NULL, NULL, &node)) {
		pr_err(self, "dsp node allocate failed");
		return NULL;
	}

	if (!dsp_node_create(dsp_handle, node)) {
		pr_err(self, "dsp node create failed");
		return NULL;
	}

	pr_info(self, "dsp node created");

	return node;
}

static inline bool
destroy_node(GstDspDummy *self)
{
	if (self->node) {
		if (!dsp_node_free(self->dsp_handle, self->node)) {
			pr_err(self, "dsp node free failed");
			return false;
		}

		pr_info(self, "dsp node deleted");
	}

	return true;
}

static gboolean
dsp_init(GstDspDummy *self)
{
	int dsp_handle;

	self->dsp_handle = dsp_handle = dsp_open();

	if (dsp_handle < 0) {
		pr_err(self, "dsp open failed");
		return FALSE;
	}

	if (!dsp_attach(dsp_handle, 0, NULL, &self->proc)) {
		pr_err(self, "dsp attach failed");
		goto fail;
	}

	self->node = create_node(self);
	if (!self->node) {
		pr_err(self, "dsp node creation failed");
		goto fail;
	}

	return TRUE;

fail:
	if (self->proc) {
		if (!dsp_detach(dsp_handle, self->proc))
			pr_err(self, "dsp detach failed");
		self->proc = NULL;
	}

	if (self->dsp_handle >= 0) {
		if (dsp_close(dsp_handle) < 0)
			pr_err(self, "dsp close failed");
		self->dsp_handle = -1;
	}

	return FALSE;
}

static gboolean
dsp_deinit(GstDspDummy *self)
{
	gboolean ret = TRUE;

	if (self->node) {
		if (!destroy_node(self)) {
			pr_err(self, "dsp node destroy failed");
			ret = FALSE;
		}
		self->node = NULL;
	}

	if (self->proc) {
		if (!dsp_detach(self->dsp_handle, self->proc)) {
			pr_err(self, "dsp detach failed");
			ret = FALSE;
		}
		self->proc = NULL;
	}

	if (self->dsp_handle >= 0) {
		if (dsp_close(self->dsp_handle) < 0) {
			pr_err(self, "dsp close failed");
			ret = FALSE;
		}
		self->dsp_handle = -1;
	}

	return ret;
}

static inline void
configure_dsp_node(int dsp_handle,
		   struct dsp_node *node,
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
		pr_err(self, "dsp node run failed");
		return FALSE;
	}

	pr_info(self, "dsp node running");

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
		pr_err(self, "dsp node terminate failed: %lx", exit_status);
		return FALSE;
	}

	pr_info(self, "dsp node terminated");

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
			pr_err(self, "dsp init failed");
			return GST_STATE_CHANGE_FAILURE;
		}

		break;

	case GST_STATE_CHANGE_READY_TO_PAUSED:
		if (!_dsp_start(self)) {
			pr_err(self, "dsp start failed");
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
			pr_err(self, "dsp stop failed");
			return GST_STATE_CHANGE_FAILURE;
		}

		break;

	case GST_STATE_CHANGE_READY_TO_NULL:
		if (!dsp_deinit(self)) {
			pr_err(self, "dsp deinit failed");
			return GST_STATE_CHANGE_FAILURE;
		}

		break;

	default:
		break;
	}

	return ret;
}

static inline bool
buffer_is_aligned(GstBuffer *buf, dmm_buffer_t *b)
{
	if ((unsigned long) GST_BUFFER_DATA(buf) % b->alignment != 0)
		return false;
	if (((unsigned long) GST_BUFFER_DATA(buf) + GST_BUFFER_SIZE(buf)) % b->alignment != 0)
		return false;
	return true;
}

static inline void
map_buffer(GstDspDummy *self,
	   GstBuffer *g_buf,
	   dmm_buffer_t *d_buf)
{
	if (d_buf->alignment == 0 || buffer_is_aligned(g_buf, d_buf)) {
		dmm_buffer_use(d_buf, GST_BUFFER_DATA(g_buf), GST_BUFFER_SIZE(g_buf));
		gst_buffer_ref(g_buf);
		d_buf->user_data = g_buf;
		return;
	}

	if (d_buf->alignment != 0) {
		pr_warning(self, "buffer not aligned: %p-%p",
			   GST_BUFFER_DATA(g_buf),
			   GST_BUFFER_DATA(g_buf) + GST_BUFFER_SIZE(g_buf));
	}

	dmm_buffer_allocate(d_buf, GST_BUFFER_SIZE(g_buf));
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
		pr_err(self, "couldn't allocate buffer");
		ret = GST_FLOW_ERROR;
		goto leave;
	}

	/* map dsp to gpp address */
	map_buffer(self, buf, self->in_buffer);
	map_buffer(self, out_buf, self->out_buffer);

	if (self->in_buffer->need_copy) {
		memcpy(self->in_buffer->data, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
		self->in_buffer->need_copy = false;
	}

	dmm_buffer_map(self->in_buffer);
	dmm_buffer_map(self->out_buffer);

	configure_dsp_node(self->dsp_handle, self->node, self->in_buffer, self->out_buffer);

	{
		struct dsp_msg msg;

		msg.cmd = 1;
		msg.arg_1 = self->in_buffer->size;
		dsp_node_put_message(self->dsp_handle, self->node, &msg, -1);
		dsp_node_get_message(self->dsp_handle, self->node, &msg, -1);
	}

	dmm_buffer_unmap(self->out_buffer);
	dmm_buffer_unmap(self->in_buffer);

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
