/*
 * Copyright (C) 2010 Nokia Corporation
 *
 * Authors:
 * Elamparithi Shanmugam <parithi@ti.com>
 * Felipe Contreras <felipe.contreras@nokia.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "gstdspipp.h"
#include "util.h"

static GstDspBaseClass *parent_class;

static inline GstCaps *generate_sink_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();
	struc = gst_structure_new("video/x-raw-yuv", "format",
				  GST_TYPE_FOURCC, GST_MAKE_FOURCC('U','Y','V','Y'), NULL);
	gst_caps_append_structure(caps, struc);
	return caps;
}

static inline GstCaps *generate_src_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();
	struc = gst_structure_new("video/x-raw-yuv", "format",
				  GST_TYPE_FOURCC, GST_MAKE_FOURCC('U','Y','V','Y'), NULL);
	gst_caps_append_structure(caps, struc);
	return caps;
}

static void *create_node(GstDspIpp *self)
{
	GstDspBase *base;
	int dsp_handle;
	struct dsp_node *node = NULL;

	const struct dsp_uuid dfgm_uuid = { 0xe57d1a99, 0xbc8d, 0x463c, 0xac, 0x93,
		{ 0x49, 0xeA, 0x1A, 0xC0, 0x19, 0x53 } };

	const struct dsp_uuid ipp_uuid = { 0x8ea1b508, 0x49be, 0x4cd0, 0xbb, 0x12,
		{ 0xea, 0x95, 0x00, 0x58, 0xb3, 0x6b } };

	struct dsp_node_attr_in attrs = {
		.cb = sizeof(attrs),
		.priority = 5,
		.timeout = 1000,
	};

	base = GST_DSP_BASE(self);
	dsp_handle = base->dsp_handle;

	if (!gstdsp_register(dsp_handle, &dfgm_uuid, DSP_DCD_LIBRARYTYPE, "dfgm.dll64P")) {
		pr_err(self, "failed to register usn node library");
		return NULL;
	}

	if (!gstdsp_register(dsp_handle, &ipp_uuid, DSP_DCD_LIBRARYTYPE, "ipp_sn.dll64P")) {
		pr_err(self, "failed to register algo node library");
		return NULL;
	}

	if (!gstdsp_register(dsp_handle, &ipp_uuid, DSP_DCD_NODETYPE, "ipp_sn.dll64P")) {
		pr_err(self, "failed to register algo node");
		return NULL;
	}

	if (!dsp_node_allocate(dsp_handle, base->proc, &ipp_uuid, NULL, &attrs, &node)) {
		pr_err(self, "dsp node allocate failed");
		return NULL;
	}

	if (!dsp_node_create(dsp_handle, node)) {
		pr_err(self, "dsp node create failed");
		return NULL;
	}

	return node;
}

static gboolean sink_setcaps(GstPad *pad, GstCaps *caps)
{
	GstDspIpp *self;
	GstDspBase *base;
	GstStructure *in_struc;
	GstCaps *out_caps;
	GstStructure *out_struc;

	self = GST_DSP_IPP(GST_PAD_PARENT(pad));
	base = GST_DSP_BASE(self);

	in_struc = gst_caps_get_structure(caps, 0);
	out_caps = gst_caps_new_empty();
	out_struc = gst_structure_new("video/x-raw-yuv", NULL);

	gst_caps_append_structure(out_caps, out_struc);

	if (!gst_pad_take_caps(base->srcpad, out_caps))
		return FALSE;

	base->node = create_node(self);

	if (!base->node) {
		pr_err(self, "dsp node creation failed");
		return FALSE;
	}
	return true;
}

static void instance_init(GTypeInstance *instance, gpointer g_class)
{
	GstDspBase *base = GST_DSP_BASE(instance);

	du_port_alloc_buffers(base->ports[0], 1);
	du_port_alloc_buffers(base->ports[1], 1);

	gst_pad_set_setcaps_function(base->sinkpad, sink_setcaps);
}

static void finalize(GObject *obj)
{
	G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void class_init(gpointer g_class, gpointer class_data)
{
	GObjectClass *gobject_class;

	gobject_class = G_OBJECT_CLASS(g_class);

	parent_class = g_type_class_peek_parent(g_class);
	gobject_class->finalize = finalize;
}

static void base_init(gpointer g_class)
{
	GstElementClass *element_class;
	GstPadTemplate *template;

	element_class = GST_ELEMENT_CLASS(g_class);

	gst_element_class_set_details_simple(element_class,
					     "DSP IPP",
					     "Codec/Encoder/Image",
					     "Image processing algorithms",
					     "Texas Instruments");

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

GType gst_dsp_ipp_get_type(void)
{
	static GType type;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo type_info = {
			.class_size = sizeof(GstDspIppClass),
			.base_init = base_init,
			.class_init = class_init,
			.instance_size = sizeof(GstDspIpp),
			.instance_init = instance_init,
		};

		type = g_type_register_static(GST_DSP_BASE_TYPE, "GstDspIpp", &type_info, 0);
	}
	return type;
}
