/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "gstdspvpp.h"
#include "gstdspparse.h"
#include "plugin.h"
#include "util.h"

#include "dsp_bridge.h"
#include "log.h"

static GstDspBaseClass *parent_class;

static inline GstCaps *
generate_sink_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();

	struc = gst_structure_new("video/x-raw-rgb",
				  "bpp", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16,
				  "endianness", G_TYPE_INT, 1234,
				  "red_mask", G_TYPE_INT, 63488,
				  "green_mask", G_TYPE_INT, 2016,
				  "blue_mask", G_TYPE_INT, 31,
				  NULL);

	gst_caps_append_structure(caps, struc);

	return caps;
}

static inline GstCaps *
generate_src_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();

	struc = gst_structure_new("video/x-raw-yuv",
				  "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC('I', '4', '2', '0'),
				  NULL);

	gst_caps_append_structure(caps, struc);

	return caps;
}

static void *
create_node(GstDspBase *base)
{
	GstDspVpp *self;
	struct td_codec *codec;
	int dsp_handle;
	struct dsp_node *node;

	const struct dsp_uuid usn_uuid = { 0x79A3C8B3, 0x95F2, 0x403F, 0x9A, 0x4B,
		{ 0xCF, 0x80, 0x57, 0x73, 0x05, 0x41 } };

	self = GST_DSP_VPP(base);
	dsp_handle = base->dsp_handle;

	if (!gstdsp_register(dsp_handle, &usn_uuid, DSP_DCD_LIBRARYTYPE, "usn.dll64P")) {
		pr_err(self, "failed to register usn node library");
		return NULL;
	}

	codec = base->codec;
	if (!codec) {
		pr_err(self, "unknown algorithm");
		return NULL;
	}

	pr_info(base, "algo=%s", codec->filename);

	if (!gstdsp_register(dsp_handle, codec->uuid, DSP_DCD_LIBRARYTYPE, codec->filename)) {
		pr_err(self, "failed to register algo node library");
		return NULL;
	}

	if (!gstdsp_register(dsp_handle, codec->uuid, DSP_DCD_NODETYPE, codec->filename)) {
		pr_err(self, "failed to register algo node");
		return NULL;
	}

	{
		struct dsp_node_attr_in attrs = {
			.cb = sizeof(attrs),
			.priority = 5,
			.timeout = 1000,
		};
		void *arg_data;

		codec->create_args(base, &attrs.profile_id, &arg_data);

		if (!dsp_node_allocate(dsp_handle, base->proc, codec->uuid, arg_data, &attrs, &node)) {
			pr_err(self, "dsp node allocate failed");
			free(arg_data);
			return NULL;
		}
		free(arg_data);
	}

	if (!dsp_node_create(dsp_handle, node)) {
		pr_err(self, "dsp node create failed");
		dsp_node_free(dsp_handle, node);
		return NULL;
	}

	pr_info(self, "dsp node created");

	if (codec->setup_params)
		codec->setup_params(base);

	if (codec->send_params)
		codec->send_params(base, node);

	return node;
}

static inline bool
destroy_node(GstDspVpp *self,
	     int dsp_handle,
	     struct dsp_node *node)
{
	if (node) {
		if (!dsp_node_free(dsp_handle, node)) {
			pr_err(self, "dsp node free failed");
			return false;
		}

		pr_info(self, "dsp node deleted");
	}

	return true;
}

static inline void
configure_caps(GstDspVpp *self,
	       GstCaps *in,
	       GstCaps *out)
{
	GstDspBase *base;
	GstStructure *out_struc, *in_struc;
	const GValue *aspect_ratio;
	const GValue *framerate;

	base = GST_DSP_BASE(self);

	in_struc = gst_caps_get_structure(in, 0);

	out_struc = gst_structure_new("video/x-raw-yuv",
				      "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC('I', '4', '2', '0'),
				      NULL);

	if (gst_structure_get_int(in_struc, "width", &self->width))
		self->out_width = self->width;
	if (gst_structure_get_int(in_struc, "height", &self->height))
		self->out_height = self->height;

	gst_structure_set(out_struc, "width", G_TYPE_INT, self->out_width, NULL);
	gst_structure_set(out_struc, "height", G_TYPE_INT, self->out_height, NULL);

	aspect_ratio = gst_structure_get_value(in_struc, "pixel-aspect-ratio");
	if (aspect_ratio)
		gst_structure_set_value(out_struc, "pixel-aspect-ratio", aspect_ratio);

	framerate = gst_structure_get_value(in_struc, "framerate");
	if (framerate)
		gst_structure_set_value(out_struc, "framerate", framerate);

	base->output_buffer_size = self->out_width * self->out_height * 1.5;

	gst_caps_append_structure(out, out_struc);
}

static gboolean
sink_setcaps(GstPad *pad,
	     GstCaps *caps)
{
	GstDspVpp *self;
	GstDspBase *base;
	GstStructure *in_struc;
	GstCaps *out_caps;
	gboolean ret;

	self = GST_DSP_VPP(GST_PAD_PARENT(pad));
	base = GST_DSP_BASE(self);

	{
		gchar *str = gst_caps_to_string(caps);
		pr_info(self, "sink caps: %s", str);
		g_free(str);
	}

	in_struc = gst_caps_get_structure(caps, 0);

	base->codec = &td_vpp_codec;

	du_port_alloc_buffers(base->ports[0], 4);
	du_port_alloc_buffers(base->ports[1], 4);

	out_caps = gst_caps_new_empty();
	configure_caps(self, caps, out_caps);
	base->tmp_caps = out_caps;

	ret = gst_pad_set_caps(pad, caps);

	if (!ret)
		return FALSE;

	return TRUE;
}

static void
instance_init(GTypeInstance *instance,
	      gpointer g_class)
{
	GstDspBase *base;

	base = GST_DSP_BASE(instance);

	base->use_pad_alloc = TRUE;
	base->create_node = create_node;

	/* ugly, but needed */
	base->ports[1]->id = 3;

	gst_pad_set_setcaps_function(base->sinkpad, sink_setcaps);
}

static void
base_init(gpointer g_class)
{
	GstElementClass *element_class;
	GstPadTemplate *template;

	element_class = GST_ELEMENT_CLASS(g_class);

	gst_element_class_set_details_simple(element_class,
					     "DSP VPP filter",
					     "Filter/Converter/Video",
					     "Converts video from different formats",
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
	parent_class = g_type_class_peek_parent(g_class);
}

GType
gst_dsp_vpp_get_type(void)
{
	static GType type;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo type_info = {
			.class_size = sizeof(GstDspVppClass),
			.class_init = class_init,
			.base_init = base_init,
			.instance_size = sizeof(GstDspVpp),
			.instance_init = instance_init,
		};

		type = g_type_register_static(GST_DSP_BASE_TYPE, "GstDspVpp", &type_info, 0);
	}

	return type;
}
