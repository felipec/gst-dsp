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

	if (self->dsp_error)
		goto leave;

	if (self->proc) {
		if (!dsp_detach(self->dsp_handle, self->proc)) {
			pr_err(self, "dsp detach failed");
			ret = FALSE;
		}
		self->proc = NULL;
	}

leave:

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

	self->events[0] = calloc(1, sizeof(struct dsp_notification));
	if (!dsp_node_register_notify(self->dsp_handle, self->node,
				      DSP_NODEMESSAGEREADY, 1,
				      self->events[0]))
	{
		pr_err(self, "failed to register for notifications");
		return false;
	}

	self->events[1] = calloc(1, sizeof(struct dsp_notification));
	if (!dsp_register_notify(self->dsp_handle, self->proc,
				 DSP_MMUFAULT, 1,
				 self->events[1]))
	{
		pr_err(self, "failed to register for DSP_MMUFAULT");
		return false;
	}

	self->events[2] = calloc(1, sizeof(struct dsp_notification));
	if (!dsp_register_notify(self->dsp_handle, self->proc,
				 DSP_SYSERROR, 1,
				 self->events[2]))
	{
		pr_err(self, "failed to register for DSP_SYSERROR");
		return false;
	}

	return TRUE;
}

static gboolean
_dsp_stop(GstDspDummy *self)
{
	unsigned long exit_status;
	unsigned i;

	dmm_buffer_free(self->out_buffer);
	dmm_buffer_free(self->in_buffer);

	for (i = 0; i < ARRAY_SIZE(self->events); i++) {
		free(self->events[i]);
		self->events[i] = NULL;
	}

	if (self->dsp_error)
		goto leave;

	if (!dsp_node_terminate(self->dsp_handle, self->node, &exit_status))
		pr_err(self, "dsp node terminate failed: 0x%lx", exit_status);

leave:

	if (!destroy_node(self))
		pr_err(self, "dsp node destroy failed");

	self->node = NULL;

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

static void
post_error(GstDspDummy *self,
		const char *message)
{
	GError *gerror;
	GstMessage *gst_msg;

	gerror = g_error_new_literal(GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED, message);
	gst_msg = gst_message_new_error(GST_OBJECT(self), gerror, NULL);
	gst_element_post_message(GST_ELEMENT(self), gst_msg);

	g_error_free(gerror);
}

static void
got_error(GstDspDummy *self,
		unsigned id,
		const char *message)
{
	pr_err(self, "%s", message);
	post_error(self, message);

	self->dsp_error = id;
}

static bool check_events(GstDspDummy *self,
		struct dsp_node *node, struct dsp_msg *msg)
{
	unsigned int index = 0;
	pr_debug(self, "waiting for events");
	if (!dsp_wait_for_events(self->dsp_handle, self->events, 3, &index, 1000)) {
		if (errno == ETIME) {
			pr_info(self, "timed out waiting for events");
			return true;
		}
		pr_err(self, "failed waiting for events: %i", errno);
		got_error(self, -1, "unable to get event");
		return false;
	}

	switch (index) {
	case 0:
		dsp_node_get_message(self->dsp_handle, node, msg, 100);
		pr_debug(self, "got dsp message: 0x%0x 0x%0x 0x%0x",
				msg->cmd, msg->arg_1, msg->arg_2);
		return true;
	case 1:
		pr_err(self, "got DSP MMUFAULT");
		return false;
	case 2:
		pr_err(self, "got DSP SYSERROR");
		return false;
	default:
		pr_err(self, "wrong event index");
		return false;
	}
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
	gstdsp_map_buffer(self, buf, self->in_buffer);
	gstdsp_map_buffer(self, out_buf, self->out_buffer);

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
		msg.arg_1 = self->in_buffer->len;
		dsp_node_put_message(self->dsp_handle, self->node, &msg, -1);
		dsp_node_get_message(self->dsp_handle, self->node, &msg, -1);
		if (!check_events(self, self->node, &msg)) {
			dmm_buffer_unmap(self->out_buffer);
			dmm_buffer_unmap(self->in_buffer);
			ret = GST_FLOW_ERROR;
			return ret;
		}
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
