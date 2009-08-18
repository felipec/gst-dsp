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

#include "util.h"
#include "dsp_bridge.h"

#include <string.h> /* for memcpy */

#include "log.h"

#define GST_CAT_DEFAULT gstdsp_debug

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
	GstDspBase *base = GST_DSP_BASE(self);

	struct jpegenc_args args = {
		.num_streams = 2,
		.in_id = 0,
		.in_type = 0,
		.in_count = base->ports[0]->num_buffers,
		.out_id = 1,
		.out_type = 0,
		.out_count = base->ports[1]->num_buffers,
		.max_width = 2592 + 32,
		.max_height = 1968 + 32,
		.color_format = 1,
		.max_app0_width = 0,
		.max_app0_height = 0,
		.max_app1_width = 0,
		.max_app1_height = 0,
		.max_app13_width = 0,
		.max_app13_height = 0,
		.scans = 0,
	};

	struct foo_data *cb_data;

	cb_data = malloc(sizeof(*cb_data));
	cb_data->size = sizeof(args);
	memcpy(&cb_data->data, &args, sizeof(args));

	return cb_data;
}

struct mp4venc_args {
	uint16_t num_streams;

	uint16_t in_id;
	uint16_t in_type;
	uint16_t in_count;

	uint16_t out_id;
	uint16_t out_type;
	uint16_t out_count;
	uint16_t reserved;

	uint32_t width;
	uint32_t height;
	uint32_t bitrate;
	uint32_t vbv_size;
	uint32_t gob_interval;

	uint8_t is_mpeg4;
	uint8_t color_format;
	uint8_t hec;
	uint8_t resync_marker;
	uint8_t data_part;
	uint8_t reversible_vlc;
	uint8_t unrestricted_mv;
	uint8_t framerate;
	uint8_t rate_control;
	uint8_t qp_first;
	uint8_t profile;
	uint8_t level;
	uint32_t max_delay;

	uint32_t vbv_enable;
	uint32_t h263_slice_mode;

	uint32_t use_gov;
	uint32_t use_vos;
	uint32_t h263_annex_i;
	uint32_t h263_annex_j;
	uint32_t h263_annex_t;
};

static inline void *
get_mp4venc_args(GstDspVEnc *self)
{
	GstDspBase *base = GST_DSP_BASE(self);

	struct mp4venc_args args = {
		.num_streams = 2,
		.in_id = 0,
		.in_type = 0,
		.in_count = base->ports[0]->num_buffers,
		.out_id = 1,
		.out_type = 0,
		.out_count = base->ports[1]->num_buffers,
		.reserved = 0,
		.width = self->width,
		.height = self->height,
		.bitrate = self->bitrate,
		.vbv_size = 120,
		.gob_interval = 1,
		.color_format = 2,
		.hec = 1,
		.resync_marker = 1,
		.data_part = 0,
		.reversible_vlc = 0,
		.unrestricted_mv = 0,
		.framerate = self->framerate,
		.rate_control = 1, /* low delay */
		.qp_first = 12,
		.profile = 1,
		.max_delay = 300,
		.vbv_enable = 0,
		.h263_slice_mode = 0,
		.use_gov = 0,
		.use_vos = 0,
		.h263_annex_i = 0,
		.h263_annex_j = 0,
		.h263_annex_t = 0,
	};

	args.is_mpeg4 = base->alg == GSTDSP_MP4VENC ? 1 : 0;

	if (base->alg == GSTDSP_MP4VENC)
		args.level = 5;
	else
		args.level = 20;

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

	const dsp_uuid_t mp4v_enc_uuid = { 0x98c2e8d8, 0x4644, 0x11d6, 0x81, 0x18,
		{ 0x00, 0xb0, 0xd0, 0x8d, 0x72, 0x9f } };

	base = GST_DSP_BASE(self);
	dsp_handle = base->dsp_handle;

	if (!gstdsp_register(dsp_handle, &usn_uuid, DSP_DCD_LIBRARYTYPE, "usn.dll64P")) {
		pr_err(self, "failed to register usn node library");
		return NULL;
	}

	switch (base->alg) {
		case GSTDSP_JPEGENC:
			alg_uuid = &jpeg_enc_uuid;
			alg_fn = "jpegenc_sn.dll64P";
			break;
		case GSTDSP_H263ENC:
		case GSTDSP_MP4VENC:
			alg_uuid = &mp4v_enc_uuid;
			alg_fn = "m4venc_sn.dll64P";
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
			case GSTDSP_JPEGENC:
				attrs.profile_id = 10;
				cb_data = get_jpegenc_args(self);
				break;
			case GSTDSP_H263ENC:
			case GSTDSP_MP4VENC:
				if (self->width * self->height > 720 * 480)
					attrs.profile_id = 4;
				else if (self->width * self->height > 640 * 480)
					attrs.profile_id = 3;
				else if (self->width * self->height > 352 * 288)
					attrs.profile_id = 2;
				else if (self->width * self->height > 176 * 144)
					attrs.profile_id = 1;
				else
					attrs.profile_id = 0;
				cb_data = get_mp4venc_args(self);
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

struct jpegenc_dyn_params
{
	uint32_t size;
	uint32_t num_au; /* set to 0 */
	uint32_t color_format;
	uint32_t height;
	uint32_t width;
	uint32_t capture_width;
	uint32_t gen_header;
	uint32_t quality;
	uint32_t capture_height;
	uint32_t dri_interval;
	uint32_t huffman_table;
	uint32_t quant_table;
};

static inline void
jpegenc_send_params(GstDspBase *base,
		    guint width,
		    guint height)
{
	struct jpegenc_dyn_params *params;
	dmm_buffer_t *b;

	b = dmm_buffer_new(base->dsp_handle, base->proc);
	dmm_buffer_allocate(b, sizeof(*params));

	params = b->data;
	params->num_au = 0;
	params->size = sizeof(*params);
	params->color_format = 4;
	params->width = width;
	params->height = height;
	params->capture_width = width;
	params->capture_height = height;
	params->gen_header = 0;
	params->quality = 90;
	params->dri_interval = 0;
	params->huffman_table = 0;
	params->quant_table = 0;

	base->alg_ctrl = b;

	dsp_send_message(base->dsp_handle, base->node, 0x0400, 3, (uint32_t) b->map);
}

struct mp4venc_in_stream_params {
	uint32_t frame_index;
	uint32_t framerate;
	uint32_t bitrate;
	uint32_t i_frame_interval;
	uint32_t generate_header;
	uint32_t force_i_frame;

	uint32_t resync_interval;
	uint32_t hec_interval;
	uint32_t air_rate;
	uint32_t mir_rate;
	uint32_t qp_intra;
	uint32_t f_code;
	uint32_t half_pel;
	uint32_t ac_pred;
	uint32_t mv;
	uint32_t use_umv;
	uint32_t mv_data_enable;
	uint32_t resync_data_enable;

	uint32_t qp_inter;
	uint32_t last_frame;
	uint32_t width;
};

struct mp4venc_out_stream_params {
	uint32_t bitstream_size;
	uint32_t frame_type;
	uint32_t mv_data_size;
	uint32_t num_packets;
	uint8_t mv_data[12960];
	uint8_t resync_data[6480];
};

static void mp4venc_recv_cb(GstDspBase *base,
			    du_port_t *port,
			    dmm_buffer_t *p,
			    dmm_buffer_t *b)
{
	struct mp4venc_out_stream_params *param;
	param = p->data;

	dmm_buffer_invalidate(p, sizeof(*param));
	g_atomic_int_set(&base->keyframe, param->frame_type == 1);
}

static void mp4venc_send_cb(GstDspBase *base,
			    du_port_t *port,
			    dmm_buffer_t *p,
			    dmm_buffer_t *b)
{
	struct mp4venc_in_stream_params *param;
	param = p->data;
	param->frame_index++;
	dmm_buffer_clean(p, sizeof(*param));
}

static inline void
setup_mp4params(GstDspBase *base)
{
	struct mp4venc_in_stream_params *in_param;
	struct mp4venc_out_stream_params *out_param;

	GstDspVEnc *self = GST_DSP_VENC(base);
	dmm_buffer_t *tmp;

	tmp = dmm_buffer_new(base->dsp_handle, base->proc);
	dmm_buffer_allocate(tmp, sizeof(*in_param));

	in_param = tmp->data;
	in_param->frame_index = 0;
	in_param->framerate = self->framerate;
	in_param->bitrate = self->bitrate;
	in_param->i_frame_interval = 5;
	in_param->generate_header = 0;
	in_param->force_i_frame = 0;
	in_param->resync_interval = 1024;
	in_param->hec_interval = 3;
	in_param->air_rate = 10;
	in_param->mir_rate = 0;
	in_param->qp_intra = 0;
	in_param->f_code = 5;
	in_param->half_pel = 0;
	in_param->ac_pred = 0;
	in_param->mv = 0;
	in_param->use_umv = 0;
	in_param->mv_data_enable = 0;
	in_param->resync_data_enable = 0;
	in_param->qp_inter = 8;
	in_param->last_frame = 0;
	in_param->width = 0;

	if (base->alg == GSTDSP_MP4VENC)
		in_param->ac_pred = 1;
	else
		in_param->ac_pred = 0;

	dmm_buffer_clean(tmp, sizeof(*in_param));

	base->ports[0]->param = tmp;
	base->ports[0]->send_cb = mp4venc_send_cb;

	tmp = dmm_buffer_new(base->dsp_handle, base->proc);
	dmm_buffer_allocate(tmp, sizeof(*out_param));
	dmm_buffer_invalidate(tmp, sizeof(*out_param));

	base->ports[1]->param = tmp;
	base->ports[1]->recv_cb = mp4venc_recv_cb;
}

static inline int calculate_bitrate(GstDspVEnc* self)
{
	GstDspBase *base = GST_DSP_BASE(self);
	float coeff, scale;
	int bitrate, ref_bitrate;
	const int reference_fps = 15;
	const float twiddle = 1.2;

	switch (base->alg) {
		case GSTDSP_MP4VENC:
			coeff = 0.2;
			break;
		case GSTDSP_H263ENC:
			coeff = 0.3;
			break;
		case GSTDSP_H264ENC:
			coeff = 0.35;
			break;
		default:
			coeff = 0.1;
			break;
	}

	ref_bitrate = (self->width * self->height) / coeff;
	scale = 1 + ((float) self->framerate / reference_fps - 1) * twiddle;

	bitrate = ref_bitrate * scale;

	pr_info(self, "bitrate: %d", bitrate);

	return bitrate;
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
	gint width = 0, height = 0;

	self = GST_DSP_VENC(GST_PAD_PARENT(pad));
	base = GST_DSP_BASE(self);

	{
		gchar *str = gst_caps_to_string(caps);
		pr_info(self, "sink caps: %s", str);
		g_free(str);
	}

	in_struc = gst_caps_get_structure(caps, 0);

	out_caps = gst_caps_new_empty();

	switch (base->alg) {
		case GSTDSP_JPEGENC:
			out_struc = gst_structure_new("image/jpeg",
						      NULL);
			break;
		case GSTDSP_H263ENC:
			out_struc = gst_structure_new("video/x-h263",
						      "variant", G_TYPE_STRING, "itu",
						      NULL);
			break;
		case GSTDSP_MP4VENC:
			out_struc = gst_structure_new("video/mpeg",
						      "mpegversion", G_TYPE_INT, 4,
						      "systemstream", G_TYPE_BOOLEAN, FALSE,
						      NULL);
			break;
		default:
			return FALSE;
	}

	if (gst_structure_get_int(in_struc, "width", &width))
		gst_structure_set(out_struc, "width", G_TYPE_INT, width, NULL);
	if (gst_structure_get_int(in_struc, "height", &height))
		gst_structure_set(out_struc, "height", G_TYPE_INT, height, NULL);

	/** @todo calculate a smaller output buffer size */
	base->output_buffer_size = width * height;
	if (base->alg == GSTDSP_JPEGENC)
		base->input_buffer_size = ROUND_UP(width, 16) * ROUND_UP(height, 16) * 2;

	self->width = width;
	self->height = height;

	{
		const GValue *framerate = NULL;
		framerate = gst_structure_get_value(in_struc, "framerate");
		if (framerate) {
			gst_structure_set_value(out_struc, "framerate", framerate);
			/* calculate nearest integer */
			self->framerate = (gst_value_get_fraction_numerator(framerate) * 2 /
				gst_value_get_fraction_denominator(framerate) + 1) / 2;
		}
	}

	self->bitrate = calculate_bitrate(self);

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

	/* setup stream params */
	switch (base->alg) {
		case GSTDSP_H263ENC:
		case GSTDSP_MP4VENC:
			setup_mp4params(base);
			break;
		default:
			break;
	}

	if (!gstdsp_start(base)) {
		pr_err(self, "dsp start failed");
		return FALSE;
	}

	/* send dynamic params */
	switch (base->alg) {
		case GSTDSP_JPEGENC:
			jpegenc_send_params(base, width, height);
			break;
		default:
			break;
	}

	return gst_pad_set_caps(pad, caps);
}

static void
instance_init(GTypeInstance *instance,
	      gpointer g_class)
{
	GstDspBase *base;

	base = GST_DSP_BASE(instance);

	base->ports[0] = du_port_new(0, 1);
	base->ports[1] = du_port_new(1, 1);

	gst_pad_set_setcaps_function(base->sinkpad, sink_setcaps);
}

static void
base_init(gpointer g_class)
{
	GstElementClass *element_class;
	GstPadTemplate *template;

	element_class = GST_ELEMENT_CLASS(g_class);

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
