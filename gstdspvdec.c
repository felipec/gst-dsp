/*
 * Copyright (C) 2009 Felipe Contreras
 *
 * Authors:
 * Felipe Contreras <felipe.contreras@gmail.com>
 * Juha Alanen <juha.m.alanen@nokia.com>
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

#include "gstdspvdec.h"
#include "plugin.h"
#include "util.h"

#include "dsp_bridge.h"

#include <string.h> /* for memcpy */

#include "log.h"

#define GST_CAT_DEFAULT gstdsp_debug

enum {
	GSTDSP_MPEG4VDEC,
	GSTDSP_H264DEC,
};

static GstElementClass *parent_class;

static inline GstCaps *
generate_sink_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();

	struc = gst_structure_new("video/mpeg",
				  "mpegversion", G_TYPE_INT, 4,
				  "systemstream", G_TYPE_BOOLEAN, FALSE,
				  NULL);

	gst_caps_append_structure(caps, struc);

	struc = gst_structure_new("video/x-divx",
				  "divxversion", GST_TYPE_INT_RANGE, 4, 5,
				  NULL);

	gst_caps_append_structure(caps, struc);

	struc = gst_structure_new("video/x-xvid",
				  NULL);

	gst_caps_append_structure(caps, struc);

	struc = gst_structure_new("video/x-3ivx",
				  NULL);

	gst_caps_append_structure(caps, struc);

	struc = gst_structure_new("video/x-h263",
				  "variant", G_TYPE_STRING, "itu",
				  NULL);

	gst_caps_append_structure(caps, struc);

	struc = gst_structure_new("video/x-h264",
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
				  "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC('U', 'Y', 'V', 'Y'),
				  NULL);

	gst_caps_append_structure(caps, struc);

	return caps;
}

struct foo_data {
	unsigned long size;
	unsigned short data[42];
};

struct mp4vdec_args
{
	uint16_t num_streams;

	uint16_t in_id;
	uint16_t in_type;
	uint16_t in_count;

	uint16_t out_id;
	uint16_t out_type;
	uint16_t out_count;

	uint16_t reserved;

	uint32_t max_width;
	uint32_t max_height;
	uint32_t color_format;
	uint32_t max_framerate;
	uint32_t max_bitrate;
	uint32_t endianness;
	uint32_t profile;
	int32_t max_level;
	uint32_t mode;
	int32_t preroll;
	uint32_t display_width;
};

static inline void *
get_mp4v_args(GstDspVDec *self)
{
	GstDspBase *base = GST_DSP_BASE(self);

	struct mp4vdec_args args = {
		.num_streams = 2,
		.in_id = 0,
		.in_type = 0,
		.in_count = 1,
		.out_id = 1,
		.out_type = 0,
		.out_count = 1,
		.max_width = base->width,
		.max_height = base->height,
		.color_format = 4,
		.max_framerate = 0,
		.max_bitrate = -1,
		.endianness = 1,
		.profile = 0,
		.max_level = -1,
		.mode = 0,
		.preroll = 0,
		.display_width = 0,
	};

	struct foo_data *cb_data;

	cb_data = malloc(sizeof(*cb_data));
	cb_data->size = sizeof(args);
	memcpy(&cb_data->data, &args, sizeof(args));

	return cb_data;
}

struct h264vdec_args
{
	uint16_t num_streams;

	uint16_t in_id;
	uint16_t in_type;
	uint16_t in_count;

	uint16_t out_id;
	uint16_t out_type;
	uint16_t out_count;

	uint16_t reserved;

	uint32_t max_width;
	uint32_t max_height;
	uint32_t color_format;
	uint32_t max_framerate;
	uint32_t max_bitrate;
	uint32_t endianness;
	uint32_t profile;
	int32_t max_level;
	uint32_t mode;
	int32_t preroll;
	uint32_t stream_format;
	uint32_t display_width;
};

static inline void *
get_h264_args(GstDspVDec *self)
{
	GstDspBase *base = GST_DSP_BASE(self);

	struct h264vdec_args args = {
		.num_streams = 2,
		.in_id = 0,
		.in_type = 0,
		.in_count = 1,
		.out_id = 1,
		.out_type = 0,
		.out_count = 1,
		.max_width = base->width,
		.max_height = base->height,
		.color_format = 1,
		.max_framerate = 0,
		.max_bitrate = -1,
		.endianness = 1,
		.profile = 0,
		.max_level = -1,
		.mode = 0,
		.preroll = 0,
		.stream_format = 0,
		.display_width = 0,
	};

	struct foo_data *cb_data;

	cb_data = malloc(sizeof(*cb_data));
	cb_data->size = sizeof(args);
	memcpy(&cb_data->data, &args, sizeof(args));

	return cb_data;
}

static inline void *
create_node(GstDspVDec *self)
{
	GstDspBase *base;
	int dsp_handle;
	void *node;
	const dsp_uuid_t *alg_uuid;
	const char *alg_fn;
	const dsp_uuid_t mp4v_dec_uuid = { 0x7e4b8541, 0x47a1, 0x11d6, 0xb1, 0x56,
		{ 0x00, 0xb0, 0xd0, 0x17, 0x67, 0x4b } };

	const dsp_uuid_t h264v_dec_uuid = { 0xCB1E9F0F, 0x9D5A, 0x4434, 0x84, 0x49,
	    { 0x1F, 0xED, 0x2F, 0x99, 0x2D, 0xF7 } };

	const dsp_uuid_t usn_uuid = { 0x79A3C8B3, 0x95F2, 0x403F, 0x9A, 0x4B,
		{ 0xCF, 0x80, 0x57, 0x73, 0x05, 0x41 } };

	const dsp_uuid_t ringio_uuid = { 0x47698bfb, 0xa7ee, 0x417e, 0xa6, 0x7a,
		{ 0x41, 0xc0, 0x27, 0x9e, 0xb8, 0x05 } };

	base = GST_DSP_BASE(self);
	dsp_handle = base->dsp_handle;

	if (!gstdsp_register(dsp_handle, &ringio_uuid, DSP_DCD_LIBRARYTYPE, "ringio.dll64P")) {
		pr_err(self, "failed to register ringio node library");
		return NULL;
	}

	if (!gstdsp_register(dsp_handle, &usn_uuid, DSP_DCD_LIBRARYTYPE, "usn.dll64P")) {
		pr_err(self, "failed to register usn node library");
		return NULL;
	}

	switch (base->alg) {
		case GSTDSP_MPEG4VDEC:
			alg_uuid = &mp4v_dec_uuid;
			alg_fn = "mp4vdec_sn.dll64P";
			break;
		case GSTDSP_H264DEC:
			alg_uuid = &h264v_dec_uuid;
			alg_fn = "h264vdec_sn.dll64P";
			break;
		default:
			pr_err(self, "unknown algorithm");
			return NULL;
	}

	if (!gstdsp_register(dsp_handle, alg_uuid, DSP_DCD_LIBRARYTYPE, alg_fn)) {
		pr_err(self, "failed to register algo node library");
		return NULL;
	}

	if (!gstdsp_register(dsp_handle, alg_uuid, DSP_DCD_NODETYPE, alg_fn)) {
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
			case GSTDSP_MPEG4VDEC:
				if (base->width * base->height > 640 * 480)
					attrs.profile_id = 4;
				else if (base->width * base->height > 352 * 288)
					attrs.profile_id = 3;
				else if (base->width * base->height > 176 * 144)
					attrs.profile_id = 2;
				else
					attrs.profile_id = 1;
				cb_data = get_mp4v_args(self);
				break;
			case GSTDSP_H264DEC:
				if (base->width * base->height > 352 * 288)
					attrs.profile_id = 3;
				else if (base->width * base->height > 176 * 144)
					attrs.profile_id = 2;
				else
					attrs.profile_id = 1;
				cb_data = get_h264_args(self);
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

static inline bool
destroy_node(GstDspVDec *self,
	     int dsp_handle,
	     void *node)
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

static gboolean
sink_setcaps(GstPad *pad,
	     GstCaps *caps)
{
	GstDspVDec *self;
	GstDspBase *base;
	GstStructure *in_struc;
	GstCaps *out_caps;
	GstStructure *out_struc;
	const char *name;

	self = GST_DSP_VDEC(GST_PAD_PARENT(pad));
	base = GST_DSP_BASE(self);

	{
		gchar *str = gst_caps_to_string(caps);
		pr_info(self, "sink caps: %s", str);
		g_free(str);
	}

	in_struc = gst_caps_get_structure(caps, 0);

	name = gst_structure_get_name(in_struc);
	if (strcmp(name, "video/x-h264") == 0)
		base->alg = GSTDSP_H264DEC;
	else
		base->alg = GSTDSP_MPEG4VDEC;

	out_caps = gst_caps_new_empty();

	out_struc = gst_structure_new("video/x-raw-yuv",
				      "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC('U', 'Y', 'V', 'Y'),
				      NULL);

	if (gst_structure_get_int(in_struc, "width", &base->width))
		gst_structure_set(out_struc, "width", G_TYPE_INT, base->width, NULL);
	if (gst_structure_get_int(in_struc, "height", &base->height))
		gst_structure_set(out_struc, "height", G_TYPE_INT, base->height, NULL);

	base->output_buffer_size = base->width * base->height * 2;

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

	base->use_pad_alloc = TRUE;

	gst_pad_set_setcaps_function(base->sinkpad, sink_setcaps);
}

static void
base_init(gpointer g_class)
{
	GstElementClass *element_class;
	GstPadTemplate *template;
	GstElementDetails details;

	element_class = GST_ELEMENT_CLASS(g_class);

	details.longname = "DSP video decoder";
	details.klass = "Codec/Decoder/Video";
	details.description = "Decodes video with TI's DSP algorithms";
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
gst_dsp_vdec_get_type(void)
{
	static GType type = 0;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo *type_info;

		type_info = g_new0(GTypeInfo, 1);
		type_info->class_size = sizeof(GstDspVDecClass);
		type_info->class_init = class_init;
		type_info->base_init = base_init;
		type_info->instance_size = sizeof(GstDspVDec);
		type_info->instance_init = instance_init;

		type = g_type_register_static(GST_DSP_BASE_TYPE, "GstDspVDec", type_info, 0);
		g_free(type_info);
	}

	return type;
}
