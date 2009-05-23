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

#include "gstdspvenc.h"
#include "plugin.h"

#include "dsp_bridge.h"

#include <string.h> /* for memcpy */

#include "log.h"

#define GST_CAT_DEFAULT gstdsp_debug

enum {
	GSTDSP_JPEGENC,
};

static GstElementClass *parent_class;

static inline GstCaps *
generate_sink_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();

	struc = gst_structure_new("video/x-raw-yuv",
				  "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC('U', 'Y', 'V', 'Y'),
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

	struc = gst_structure_new("image/jpeg",
				  NULL);

	gst_caps_append_structure(caps, struc);

	return caps;
}

struct foo_data {
	unsigned long size;
	unsigned short data[42];
};

struct jpegenc_args
{
	uint16_t num_streams;

	uint16_t in_id;
	uint16_t in_type;
	uint16_t in_count;

	uint16_t out_id;
	uint16_t out_type;
	uint16_t out_count;

	uint16_t max_width;
	uint16_t max_height;
	uint16_t color_format;
	uint16_t max_app0_width;
	uint16_t max_app0_height;
	uint16_t max_app1_width;
	uint16_t max_app1_height;
	uint16_t max_app13_width;
	uint16_t max_app13_height;
	uint16_t scans;
};

static inline void *
get_jpegenc_args(GstDspVEnc *self)
{
	struct jpegenc_args args = {
		.num_streams = 2,
		.in_id = 0,
		.in_type = 0,
		.in_count = 1,
		.out_id = 1,
		.out_type = 0,
		.out_count = 1,
		.max_width = 3000,
		.max_height = 1000,
		.color_format = 1,
		.max_app0_width = 0,
		.max_app0_height = 0,
		.max_app1_width = 0,
		.max_app1_height = 0,
		.max_app13_width = 0,
		.max_app13_height = 0,
		.scans = 1,
	};

	struct foo_data *cb_data;

	cb_data = malloc(sizeof(*cb_data));
	cb_data->size = sizeof(args);
	memcpy(&cb_data->data, &args, sizeof(args));

	return cb_data;
}

static inline void *
create_node(GstDspVEnc *self)
{
	GstDspBase *base;
	int dsp_handle;
	void *node;
	const dsp_uuid_t *alg_uuid;
	const char *alg_fn;

	const dsp_uuid_t usn_uuid = { 0x79A3C8B3, 0x95F2, 0x403F, 0x9A, 0x4B,
		{ 0xCF, 0x80, 0x57, 0x73, 0x05, 0x41 } };

	const dsp_uuid_t jpeg_enc_uuid = { 0xcb70c0c1, 0x4c85, 0x11d6, 0xb1, 0x05,
		{ 0x00, 0xc0, 0x4f, 0x32, 0x90, 0x31 } };

	base = GST_DSP_BASE(self);
	dsp_handle = base->dsp_handle;

	if (!dsp_register(dsp_handle, &usn_uuid, DSP_DCD_LIBRARYTYPE, "/lib/dsp/usn.dll64P")) {
		pr_err(self, "failed to register usn node library");
		return NULL;
	}

	switch (base->alg) {
		case GSTDSP_JPEGENC:
			alg_uuid = &jpeg_enc_uuid;
			alg_fn = "/lib/dsp/jpegenc_sn.dll64P";
			break;
		default:
			pr_err(self, "unknown algorithm");
			return NULL;
	}

	if (!dsp_register(dsp_handle, alg_uuid, DSP_DCD_LIBRARYTYPE, alg_fn)) {
		pr_err(self, "failed to register algo node library");
		return NULL;
	}

	if (!dsp_register(dsp_handle, alg_uuid, DSP_DCD_NODETYPE, alg_fn)) {
		pr_err(self, "failed to register algo node");
		return NULL;
	}

	{
		struct dsp_node_attr_in attrs = {
			.cb = sizeof(attrs),
			.priority = 5,
			.timeout = 1000,
			.heap_size = 0,
			.gpp_va = 0,
		};
		void *cb_data;

		switch (base->alg) {
			case GSTDSP_JPEGENC:
				attrs.profile_id = 10;
				cb_data = get_jpegenc_args(self);
				break;
			default:
				cb_data = NULL;
		}

		if (!dsp_node_allocate(dsp_handle, base->proc, alg_uuid, cb_data, &attrs, &node)) {
			pr_err(self, "dsp node allocate failed");
			free(cb_data);
			return NULL;
		}
		free(cb_data);
	}

	if (!dsp_node_create(dsp_handle, node)) {
		pr_err(self, "dsp node create failed");
		return NULL;
	}

	pr_info(self, "dsp node created");

	return node;
}

static gboolean
sink_setcaps(GstPad *pad,
	     GstCaps *caps)
{
	GstDspVEnc *self;
	GstDspBase *base;
	GstStructure *in_struc;
	GstCaps *out_caps;
	GstStructure *out_struc;
	const char *name;

	self = GST_DSP_VENC(GST_PAD_PARENT(pad));
	base = GST_DSP_BASE(self);

	{
		gchar *str = gst_caps_to_string(caps);
		pr_info(self, "sink caps: %s", str);
		g_free(str);
	}

	in_struc = gst_caps_get_structure(caps, 0);

	name = gst_structure_get_name(in_struc);
	base->alg = GSTDSP_JPEGENC;

	out_caps = gst_caps_new_empty();

	out_struc = gst_structure_new("image/jpeg",
				      NULL);

	{
		gint width = 0, height = 0;
		if (gst_structure_get_int(in_struc, "width", &width))
			gst_structure_set(out_struc, "width", G_TYPE_INT, width, NULL);
		if (gst_structure_get_int(in_struc, "height", &height))
			gst_structure_set(out_struc, "height", G_TYPE_INT, height, NULL);

		base->output_buffer_size = width * height * 2;
	}

	{
		const GValue *framerate = NULL;
		framerate = gst_structure_get_value(in_struc, "framerate");
		if (framerate)
			gst_structure_set_value(out_struc, "framerate", framerate);
	}

	gst_caps_append_structure(out_caps, out_struc);

	{
		gchar *str = gst_caps_to_string(out_caps);
		pr_info(self, "src caps: %s", str);
		g_free(str);
	}

	gst_pad_set_caps(base->srcpad, out_caps);

	base->node = create_node(self);
	if (!base->node) {
		pr_err(self, "dsp node creation failed");
		return FALSE;
	}

	if (!gstdsp_start(base)) {
		pr_err(self, "dsp start failed");
		return FALSE;
	}

	return gst_pad_set_caps(pad, caps);
}

static void
instance_init(GTypeInstance *instance,
	      gpointer g_class)
{
	GstDspBase *base;

	base = GST_DSP_BASE(instance);

	gst_pad_set_setcaps_function(base->sinkpad, sink_setcaps);
}

static void
base_init(gpointer g_class)
{
	GstElementClass *element_class;
	GstPadTemplate *template;
	GstElementDetails details;

	element_class = GST_ELEMENT_CLASS(g_class);

	details.longname = "DSP video encoder";
	details.klass = "Codec/Encoder/Video";
	details.description = "Encodes image with TI's DSP algorithms";
	details.author = "Felipe Contreras";

	gst_element_class_set_details(element_class, &details);

	template = gst_pad_template_new("src", GST_PAD_SRC,
					GST_PAD_ALWAYS,
					generate_src_template());

	gst_element_class_add_pad_template(element_class, template);

	template = gst_pad_template_new("sink", GST_PAD_SINK,
					GST_PAD_ALWAYS,
					generate_sink_template());

	gst_element_class_add_pad_template(element_class, template);
}

static void
class_init(gpointer g_class,
	   gpointer class_data)
{
	parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}

GType
gst_dsp_venc_get_type(void)
{
	static GType type = 0;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo *type_info;

		type_info = g_new0(GTypeInfo, 1);
		type_info->class_size = sizeof(GstDspVEncClass);
		type_info->class_init = class_init;
		type_info->base_init = base_init;
		type_info->instance_size = sizeof(GstDspVEnc);
		type_info->instance_init = instance_init;

		type = g_type_register_static(GST_DSP_BASE_TYPE, "GstDspVEnc", type_info, 0);
		g_free(type_info);
	}

	return type;
}
