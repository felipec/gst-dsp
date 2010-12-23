/*
 * Copyright (C) 2010 Víctor M. Jáquez Leal
 *
 * Author: Víctor M. Jáquez Leal <vjaquez@igalia.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "gstdspadec.h"

#include "log.h"

#define GST_CAT_DEFAULT gstdsp_debug

static GstDspBaseClass *parent_class;

static inline GstCaps *
generate_sink_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();

	struc = gst_structure_new("audio/mpeg",
				"mpegversion", GST_TYPE_INT_RANGE, 1, 4,
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

	struc = gst_structure_new("audio/x-raw-int",
				"endianness", G_TYPE_INT, G_BYTE_ORDER,
				"signed", G_TYPE_BOOLEAN, TRUE,
				"width", G_TYPE_INT, 16,
				"depth", G_TYPE_INT, 16,
				"rate", GST_TYPE_INT_RANGE, 8000, 96000,
				"channels", GST_TYPE_INT_RANGE, 1, 8,
				NULL);

	gst_caps_append_structure(caps, struc);

	return caps;
}

static void *
create_node(GstDspBase *base)
{
	GstDspADec *self;
	struct td_codec *codec;
	int dsp_handle;
	struct dsp_node *node;

	const struct dsp_uuid usn_uuid = { 0x79A3C8B3, 0x95F2, 0x403F, 0x9A, 0x4B,
		{ 0xCF, 0x80, 0x57, 0x73, 0x05, 0x41 } };

	self = GST_DSP_ADEC(base);
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
			.priority = 10,
			.timeout = 10000,
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

	if (codec->send_params)
		codec->send_params(base, node);

	if (codec->setup_params)
		codec->setup_params(base);

	base->flush_buffer = codec->flush_buffer;

	return node;
}

static inline void
configure_caps(GstDspADec *self,
	       GstCaps *in,
	       GstCaps *out)
{
	GstDspBase *base;
	GstStructure *out_struc, *in_struc;
	uint channels;


	base = GST_DSP_BASE(self);

	in_struc = gst_caps_get_structure(in, 0);

	out_struc = gst_structure_new("audio/x-raw-int",
				"endianness", G_TYPE_INT, G_BYTE_ORDER,
				"signed", G_TYPE_BOOLEAN, TRUE,
				"width", G_TYPE_INT, 16,
				"depth", G_TYPE_INT, 16,
				NULL);

	if (gst_structure_get_int(in_struc, "channels", &channels))
		gst_structure_set(out_struc, "channels", G_TYPE_INT, channels, NULL);

	if (gst_structure_get_int(in_struc, "rate", &self->samplerate))
		gst_structure_set(out_struc, "rate", G_TYPE_INT, self->samplerate, NULL);

	if (base->alg == GSTDSP_AACDEC) {
		const char *fmt;
		gst_structure_get_boolean(in_struc, "framed", &self->packetised);
		fmt = gst_structure_get_string(in_struc, "stream-format");
		self->raw = strcmp(fmt, "raw") == 0;
	}

	base->output_buffer_size = 4 * 1024;

	gst_caps_append_structure(out, out_struc);
}

static gboolean
sink_setcaps(GstPad *pad,
	     GstCaps *caps)
{
	GstDspADec *self;
	GstDspBase *base;
	GstStructure *in_struc;
	const char *name;
	GstCaps *out_caps;

	self = GST_DSP_ADEC(GST_PAD_PARENT(pad));
	base = GST_DSP_BASE(self);

	{
		gchar *str = gst_caps_to_string(caps);
		pr_info(self, "sink caps: %s", str);
		g_free(str);
	}

	in_struc = gst_caps_get_structure(caps, 0);

	name = gst_structure_get_name(in_struc);
	if (strcmp(name, "audio/mpeg") == 0) {
		int version = 1;

		gst_structure_get_int(in_struc, "mpegversion", &version);
		if (version == 2 || version == 4) {
			base->alg = GSTDSP_AACDEC;
			base->codec = &td_aacdec_codec;
		}
	}

	du_port_alloc_buffers(base->ports[0], 4);
	du_port_alloc_buffers(base->ports[1], 4);

	out_caps = gst_caps_new_empty();
	configure_caps(self, caps, out_caps);
	base->tmp_caps = out_caps;

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

	gst_pad_set_setcaps_function(base->sinkpad, sink_setcaps);
}

static void
base_init(gpointer g_class)
{
	GstElementClass *element_class;
	GstPadTemplate *template;

	element_class = GST_ELEMENT_CLASS(g_class);

	gst_element_class_set_details_simple(element_class,
					     "DSP audio decoder",
					     "Codec/Decoder/Audio",
					     "Decodes audio with TI's DSP algorithms",
					     "Víctor M. Jáquez Leal");

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
gst_dsp_adec_get_type(void)
{
	static GType type;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo type_info = {
			.class_size = sizeof(GstDspADecClass),
			.class_init = class_init,
			.base_init = base_init,
			.instance_size = sizeof(GstDspADec),
			.instance_init = instance_init,
		};

		type = g_type_register_static(GST_DSP_BASE_TYPE, "GstDspADec", &type_info, 0);
	}

	return type;
}
