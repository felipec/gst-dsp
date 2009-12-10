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

#include "gstdspvenc.h"
#include "plugin.h"

#include "util.h"
#include "dsp_bridge.h"

#include <string.h> /* for memcpy */

#include "log.h"

#define GST_CAT_DEFAULT gstdsp_debug

static GstElementClass *parent_class;

enum {
    ARG_0,
    ARG_BITRATE,
};

#define DEFAULT_BITRATE 0

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

struct jpegenc_args {
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
		.vbv_size = 112,
		.color_format = 2,
		.data_part = 0,
		.reversible_vlc = 0,
		.unrestricted_mv = 1,
		.framerate = self->framerate,
		.rate_control = 2, /* low delay = 1, storage = 2, none = 4 */
		.qp_first = 12,
		.profile = 1,
		.max_delay = 300,
		.vbv_enable = 1,
		.h263_slice_mode = 0,
		.use_gov = 0,
		.use_vos = 1,
		.h263_annex_i = 0,
		.h263_annex_j = 0,
		.h263_annex_t = 0,
	};

	args.is_mpeg4 = base->alg == GSTDSP_MP4VENC ? 1 : 0;

	if (base->alg == GSTDSP_MP4VENC) {
		args.level = 5;
		args.gob_interval = 0;
		args.hec = 0;
		args.resync_marker = 0;
	} else {
		args.level = 20;
		args.gob_interval = 1;
		args.hec = 1;
		args.resync_marker = 1;
	}

	struct foo_data *cb_data;

	cb_data = malloc(sizeof(*cb_data));
	cb_data->size = sizeof(args);
	memcpy(&cb_data->data, &args, sizeof(args));

	return cb_data;
}

struct h264venc_args {
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
	uint32_t bitstream_buf_size;
	uint32_t intra_frame_period;
	uint32_t framerate;

	uint8_t  yuv_format;
	uint8_t  unrestricted_mv; /* not supported */
	uint8_t  num_ref_frames; /* not supported */
	uint8_t  rc_algorithm;
	uint8_t  idr_enable; /* not used */
	uint8_t  deblocking_enable;
	uint8_t  mv_range;
	uint8_t  qpi_frame;
	uint8_t  profile;
	uint8_t  level;

	uint16_t nal_mode;

	uint32_t encoding_preset;
	uint32_t rc_algo;
};

static inline void *
get_h264venc_args(GstDspVEnc *self)
{
	GstDspBase *base = GST_DSP_BASE(self);

	struct h264venc_args args = {
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
		.bitstream_buf_size = base->output_buffer_size,
		.intra_frame_period = self->framerate,
		.framerate = self->framerate * 1000,
		.yuv_format = 2,
		.unrestricted_mv = 0, /* not supported */
		.num_ref_frames = 1, /* not supported */
		.rc_algorithm = 1, /* 0 = var, 1 == constant, 2 == none */
		.idr_enable = 0, /* not used */
		.deblocking_enable = 1,
		.mv_range = 64,
		.qpi_frame = 28,
		.profile = 66, /* Baseline profile */
		.level = 13,
		.nal_mode = self->priv.h264.bytestream ? 0 : 1, /* 0 == bytestream, 1 == NALU */
		.encoding_preset = 3,
		.rc_algo = 0,
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
	dsp_node_t *node;
	const dsp_uuid_t *alg_uuid;
	const char *alg_fn;

	const dsp_uuid_t usn_uuid = { 0x79A3C8B3, 0x95F2, 0x403F, 0x9A, 0x4B,
		{ 0xCF, 0x80, 0x57, 0x73, 0x05, 0x41 } };

	const dsp_uuid_t jpeg_enc_uuid = { 0xcb70c0c1, 0x4c85, 0x11d6, 0xb1, 0x05,
		{ 0x00, 0xc0, 0x4f, 0x32, 0x90, 0x31 } };

	const dsp_uuid_t mp4v_enc_uuid = { 0x98c2e8d8, 0x4644, 0x11d6, 0x81, 0x18,
		{ 0x00, 0xb0, 0xd0, 0x8d, 0x72, 0x9f } };

	const dsp_uuid_t h264_enc_uuid = { 0x63A3581A, 0x09D7, 0x4AD0, 0x80, 0xB8,
		{ 0x5F, 0x2C, 0x4D, 0x4D, 0x59, 0xC9 } };

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
		case GSTDSP_H264ENC:
			alg_uuid = &h264_enc_uuid;
			alg_fn = "h264venc_sn.dll64P";
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
			case GSTDSP_H264ENC:
				if (self->width * self->height > 352 * 288)
					attrs.profile_id = 2;
				else if (self->width * self->height > 176 * 144)
					attrs.profile_id = 1;
				else
					attrs.profile_id = 0;
				cb_data = get_h264venc_args(self);
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

struct jpegenc_dyn_params {
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
jpegenc_send_params(GstDspBase *base)
{
	struct jpegenc_dyn_params *params;
	dmm_buffer_t *b;

	GstDspVEnc *self = GST_DSP_VENC(base);
	b = dmm_buffer_new(base->dsp_handle, base->proc);
	dmm_buffer_allocate(b, sizeof(*params));

	params = b->data;
	params->num_au = 0;
	params->size = sizeof(*params);
	params->color_format = 4;
	params->width = self->width;
	params->height = self->height;
	params->capture_width = self->width;
	params->capture_height = self->height;
	params->gen_header = 0;
	params->quality = self->quality;
	params->dri_interval = 0;
	params->huffman_table = 0;
	params->quant_table = 0;
	dmm_buffer_clean(b, sizeof(*params));

	base->alg_ctrl = b;

	dsp_send_message(base->dsp_handle, base->node, 0x0400, 3, (uint32_t) b->map);
}

struct h264venc_in_stream_params {
	uint32_t params_size;
	uint32_t input_height;
	uint32_t input_width;
	uint32_t ref_framerate;
	uint32_t target_framerate;
	uint32_t target_bitrate;
	uint32_t intra_frame_interval;
	uint32_t generate_header;
	uint32_t capture_width;
	uint32_t force_i_frame;

	uint32_t qp_intra;
	uint32_t qp_inter;
	uint32_t qp_max;
	uint32_t qp_min;
	uint32_t lf_disable_idc;
	uint32_t quarter_pel_disable;
	uint32_t air_mb_period;
	uint32_t max_mbs_per_slice;
	uint32_t max_bytes_per_slice;
	uint32_t slice_refresh_row_start_number;
	uint32_t slice_refresh_row_number;
	uint32_t filter_offset_a;
	uint32_t filter_offset_b;
	uint32_t log2MaxFNumMinus4;
	uint32_t chroma_qpi_index_offset;
	uint32_t constrained_intra_pred_enable;
	uint32_t pic_order_count_type;
	uint32_t max_mv_per_mb;
	uint32_t intra_4x4_enable_idc;
	uint32_t mv_data_enable;
	uint32_t hier_coding_enable;
	uint32_t stream_format;
	uint32_t intra_refresh_method;
	uint32_t perceptual_quant;
	uint32_t scene_change_det;

	void (*nal_callback_func)(uint32_t *buf, uint32_t *buf_size, void *context);
	void *context;

	uint32_t num_slice_aso;
	uint32_t aso_slice_order[8]; /* MAXNUMSLCGPS = 8 */
	uint32_t num_slice_groups;
	uint32_t slice_group_map_type;
	uint32_t slice_group_change_direction_flag;
	uint32_t slice_group_change_rate;
	uint32_t slice_group_change_cycle;
	uint32_t slice_group_params[8]; /* MAXNUMSLCGPS = 8 */

	uint32_t frame_index;
};

struct h264venc_out_stream_params {
	uint32_t bitstream_size;
	int32_t frame_type;
	uint32_t nalus_per_frame;
	uint32_t nalu_sizes[1618];
	uint32_t frame_index; /* Gives the number of the input frame which NAL unit belongs */
	uint32_t nalu_index; /* Number of current NAL unit inside the frame */
};

static inline void
h264venc_send_cb(GstDspBase *base,
		 du_port_t *port,
		 dmm_buffer_t *p,
		 dmm_buffer_t *b)
{
	struct h264venc_in_stream_params *param;
	GstDspVEnc *self = GST_DSP_VENC(base);
	param = p->data;
	param->frame_index = g_atomic_int_exchange_and_add(&self->frame_index, 1);
	dmm_buffer_clean(p, sizeof(*param));
}

static inline gboolean
gst_dsp_set_codec_data_caps(GstDspBase *base,
			    GstBuffer *buf)
{
	GstCaps *caps = NULL;
	GstStructure *structure;
	GValue value = { .g_type = 0 };

	caps = gst_pad_get_negotiated_caps(base->srcpad);
	caps = gst_caps_make_writable(caps);
	structure = gst_caps_get_structure(caps, 0);

	g_value_init(&value, GST_TYPE_BUFFER);

	gst_value_set_buffer(&value, buf);
	gst_structure_set_value(structure, "codec_data", &value);
	g_value_unset(&value);

	return gst_pad_set_caps(base->srcpad, caps);
}

static inline void
gst_dsp_h264venc_create_codec_data(GstDspBase *base)
{
	GstDspVEnc *self;
	guint8 *sps, *pps, *codec_data;
	guint16 sps_size, pps_size, offset;

	self = GST_DSP_VENC(base);

	sps = GST_BUFFER_DATA(self->priv.h264.sps);
	pps = GST_BUFFER_DATA(self->priv.h264.pps);

	sps_size = GST_BUFFER_SIZE(self->priv.h264.sps);
	pps_size = GST_BUFFER_SIZE(self->priv.h264.pps);

	offset = 0;

	self->priv.h264.codec_data = gst_buffer_new_and_alloc(sps_size + pps_size + 11);
	codec_data = GST_BUFFER_DATA(self->priv.h264.codec_data);

	codec_data[offset++] = 0x01;
	codec_data[offset++] = sps[1]; /* AVCProfileIndication*/
	codec_data[offset++] = sps[2]; /* profile_compatibility*/
	codec_data[offset++] = sps[3]; /* AVCLevelIndication */
	codec_data[offset++] = 0xff;
	codec_data[offset++] = 0xe1;
	codec_data[offset++] = (sps_size >> 8) & 0xff;
	codec_data[offset++] = sps_size & 0xff;

	memcpy(codec_data + offset, sps, sps_size);
	offset += sps_size;

	codec_data[offset++] = 0x1;
	codec_data[offset++] = (pps_size >> 8) & 0xff;
	codec_data[offset++] = pps_size & 0xff;

	memcpy(codec_data + offset, pps, pps_size);
}

static inline void
h264venc_recv_cb(GstDspBase *base,
		 du_port_t *port,
		 dmm_buffer_t *p,
		 dmm_buffer_t *b)
{
	GstDspVEnc *self = GST_DSP_VENC(base);
	struct h264venc_out_stream_params *param;
	param = p->data;

	dmm_buffer_invalidate(p, sizeof(*param));

	g_atomic_int_set(&base->keyframe, param->frame_type == 1);

	if (b->len == 0 || self->priv.h264.bytestream)
	       return;

	if (G_LIKELY(self->priv.h264.codec_data_done)) {
		/* prefix the NALU with a lenght field, don't count the start code */
		uint32_t len = b->len - 4;
		((uint8_t *)b->data)[3] = len & 0xff;
		((uint8_t *)b->data)[2] = (len >> 8) & 0xff;
		((uint8_t *)b->data)[1] = (len >> 16) & 0xff;
		((uint8_t *)b->data)[0] = (len >> 24) & 0xff;
	}
	else {
		if (!self->priv.h264.sps_received) {
			/* skip the start code 0x00000001 when storing SPS */
			self->priv.h264.sps = gst_buffer_new_and_alloc(b->len - 4);
			memcpy(GST_BUFFER_DATA(self->priv.h264.sps), b->data + 4, b->len - 4);
			self->priv.h264.sps_received = TRUE;
		} else if (!self->priv.h264.pps_received) {
			/* skip the start code 0x00000001 when storing PPS */
			self->priv.h264.pps = gst_buffer_new_and_alloc(b->len - 4);
			memcpy(GST_BUFFER_DATA(self->priv.h264.pps), b->data + 4, b->len - 4);
			self->priv.h264.pps_received = TRUE;
		}

		if (self->priv.h264.pps_received && self->priv.h264.sps_received) {
			gst_dsp_h264venc_create_codec_data(base);
			if (gst_dsp_set_codec_data_caps(base, self->priv.h264.codec_data)) {
				self->priv.h264.codec_data_done = TRUE;
				gst_buffer_unref(self->priv.h264.sps);
				gst_buffer_unref(self->priv.h264.pps);
				gst_buffer_unref(self->priv.h264.codec_data);
			}
		}
		base->skip_hack_2++;
	}
}

static inline dmm_buffer_t *
setup_h264params_in(GstDspBase *base)
{
	struct h264venc_in_stream_params *in_param;
	GstDspVEnc *self = GST_DSP_VENC(base);
	dmm_buffer_t *tmp;

	tmp = dmm_buffer_new(base->dsp_handle, base->proc);
	dmm_buffer_allocate(tmp, sizeof(*in_param));

	in_param = tmp->data;
	in_param->params_size = sizeof(*in_param);
	in_param->input_height = self->height;
	in_param->input_width = self->width;
	in_param->ref_framerate = self->framerate * 1000;
	in_param->target_framerate = self->framerate * 1000;
	in_param->target_bitrate = self->bitrate;
	in_param->intra_frame_interval = self->framerate;
	in_param->generate_header = 0;
	in_param->capture_width = 0;
	in_param->force_i_frame = 0;

	in_param->qp_intra = 0x1c;
	in_param->qp_inter = 0x1c;
	in_param->qp_max = 0x33;
	in_param->qp_min = 0;
	in_param->lf_disable_idc = 0;
	in_param->quarter_pel_disable = 0;
	in_param->air_mb_period = 0;
	in_param->max_mbs_per_slice = 3620;
	in_param->max_bytes_per_slice = 327680;
	in_param->slice_refresh_row_start_number = 0;
	in_param->slice_refresh_row_number = 0;
	in_param->filter_offset_a = 0;
	in_param->filter_offset_b = 0;
	in_param->log2MaxFNumMinus4 = 0;
	in_param->chroma_qpi_index_offset= 0;
	in_param->constrained_intra_pred_enable = 0;
	in_param->pic_order_count_type = 0;
	in_param->max_mv_per_mb = 4;
	in_param->intra_4x4_enable_idc = 2;
	in_param->mv_data_enable = 0;
	in_param->hier_coding_enable = 0;
	in_param->stream_format = 0; /* byte stream */
	in_param->intra_refresh_method = 0;
	in_param->perceptual_quant = 0;
	in_param->scene_change_det = 0;

	in_param->nal_callback_func = NULL;
	in_param->context = NULL;
	in_param->num_slice_aso = 0;
	{
		int i;
		for (i = 0; i < 8; i++)
			in_param->aso_slice_order[i] = 0; /* MAXNUMSLCGPS = 8 */
	}
	in_param->num_slice_groups = 0;
	in_param->slice_group_map_type = 0;
	in_param->slice_group_change_direction_flag = 0;
	in_param->slice_group_change_rate = 0;
	in_param->slice_group_change_cycle = 0;
	{
		int i;
		for (i = 0; i < 8; i++)
			in_param->slice_group_params[i] = 0; /* MAXNUMSLCGPS = 8 */
	}

	in_param->frame_index = 0;

	dmm_buffer_clean(tmp, sizeof(*in_param));

	return tmp;
}

static inline void
setup_h264params(GstDspBase *base)
{
	struct h264venc_out_stream_params *out_param;
	unsigned int i;

	for (i = 0; i < base->ports[0]->num_buffers; i++)
		base->ports[0]->params[i] = setup_h264params_in(base);

	base->ports[0]->send_cb = h264venc_send_cb;

	for (i = 0; i < base->ports[1]->num_buffers; i++) {
		dmm_buffer_t *tmp;
		tmp = dmm_buffer_new(base->dsp_handle, base->proc);
		dmm_buffer_allocate(tmp, sizeof(*out_param));
		base->ports[1]->params[i] = tmp;
	}
	base->ports[1]->recv_cb = h264venc_recv_cb;
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
	GstDspVEnc *self = GST_DSP_VENC(base);
	param = p->data;
	param->frame_index = g_atomic_int_exchange_and_add(&self->frame_index, 1);
	dmm_buffer_clean(p, sizeof(*param));
}

static inline dmm_buffer_t *
setup_mp4param_in(GstDspBase *base)
{
	struct mp4venc_in_stream_params *in_param;
	GstDspVEnc *self = GST_DSP_VENC(base);
	dmm_buffer_t *tmp;

	tmp = dmm_buffer_new(base->dsp_handle, base->proc);
	dmm_buffer_allocate(tmp, sizeof(*in_param));

	in_param = tmp->data;
	in_param->frame_index = 0;
	in_param->framerate = self->framerate;
	in_param->bitrate = self->bitrate;
	in_param->i_frame_interval = self->framerate;
	in_param->generate_header = 0;
	in_param->force_i_frame = 0;
	in_param->air_rate = 10;
	in_param->mir_rate = 0;
	in_param->qp_intra = 8;
	in_param->f_code = 6;
	in_param->half_pel = 1;
	in_param->mv = 0;
	in_param->mv_data_enable = 0;
	in_param->resync_data_enable = 0;
	in_param->qp_inter = 8;
	in_param->last_frame = 0;
	in_param->width = 0;

	if (base->alg == GSTDSP_MP4VENC) {
		in_param->ac_pred = 1;
		in_param->resync_interval = 0;
		in_param->hec_interval = 0;
		in_param->use_umv = 1;
	} else {
		in_param->ac_pred = 0;
		in_param->resync_interval = 1024;
		in_param->hec_interval = 3;
		in_param->use_umv = 0;
	}

	dmm_buffer_clean(tmp, sizeof(*in_param));

	return tmp;
}

static inline void
setup_mp4params(GstDspBase *base)
{
	struct mp4venc_out_stream_params *out_param;
	unsigned int i;

	for (i = 0; i < base->ports[0]->num_buffers; i++)
		base->ports[0]->params[i] = setup_mp4param_in(base);
	base->ports[0]->send_cb = mp4venc_send_cb;

	for (i = 0; i < base->ports[1]->num_buffers; i++) {
		dmm_buffer_t *tmp;
		tmp = dmm_buffer_new(base->dsp_handle, base->proc);
		dmm_buffer_allocate(tmp, sizeof(*out_param));
		base->ports[1]->params[i] = tmp;
	}
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
		case GSTDSP_H264ENC:
			out_struc = gst_structure_new("video/x-h264",
						      NULL);
			break;
		default:
			return FALSE;
	}

	if (gst_structure_get_int(in_struc, "width", &width))
		gst_structure_set(out_struc, "width", G_TYPE_INT, width, NULL);
	if (gst_structure_get_int(in_struc, "height", &height))
		gst_structure_set(out_struc, "height", G_TYPE_INT, height, NULL);

	switch (base->alg) {
		case GSTDSP_H263ENC:
		case GSTDSP_MP4VENC:
		case GSTDSP_H264ENC:
			base->output_buffer_size = width * height / 2;
			break;
		case GSTDSP_JPEGENC:
			base->input_buffer_size = ROUND_UP(width, 16) * ROUND_UP(height, 16) * 2;
			base->output_buffer_size = width * height;
			self->quality = 90;
			if (self->quality < 10)
				base->output_buffer_size /= 10;
			else if (self->quality < 100)
				base->output_buffer_size /= (100 / self->quality);
			break;
		default:
			break;
	}

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

	if (self->bitrate == 0)
		self->bitrate = calculate_bitrate(self);

	gst_caps_append_structure(out_caps, out_struc);

	{
		gchar *str = gst_caps_to_string(out_caps);
		pr_info(self, "src caps: %s", str);
		g_free(str);
	}

	if (!gst_pad_set_caps(base->srcpad, out_caps))
		return FALSE;

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
		case GSTDSP_H264ENC:
			setup_h264params(base);
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
			jpegenc_send_params(base);
			break;
		default:
			break;
	}

	return TRUE;
}

static void
set_property(GObject *obj,
	     guint prop_id,
	     const GValue *value,
	     GParamSpec *pspec)
{
	GstDspVEnc *self;

	self = GST_DSP_VENC(obj);

	switch (prop_id) {
	case ARG_BITRATE:
		self->bitrate = g_value_get_uint(value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}

static void
get_property(GObject *obj,
	     guint prop_id,
	     GValue *value,
	     GParamSpec *pspec)
{
	GstDspVEnc *self;

	self = GST_DSP_VENC(obj);

	switch (prop_id) {
	case ARG_BITRATE:
		g_value_set_uint(value, self->bitrate);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}

static void
instance_init(GTypeInstance *instance,
	      gpointer g_class)
{
	GstDspBase *base;
	GstDspVEnc *self;

	base = GST_DSP_BASE(instance);
	self = GST_DSP_VENC(instance);

	base->ports[0] = du_port_new(0, 2);
	base->ports[1] = du_port_new(1, 2);

	gst_pad_set_setcaps_function(base->sinkpad, sink_setcaps);

	self->bitrate = DEFAULT_BITRATE;
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
	gst_object_unref(template);
}

static void
class_init(gpointer g_class,
	   gpointer class_data)
{
	GObjectClass *gobject_class;

	parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
	gobject_class = G_OBJECT_CLASS(g_class);

	gobject_class->set_property = set_property;
	gobject_class->get_property = get_property;

	g_object_class_install_property(gobject_class, ARG_BITRATE,
					g_param_spec_uint("bitrate", "Bit-rate",
							  "Encoding bit-rate (0 for auto)",
							  0, G_MAXUINT, DEFAULT_BITRATE,
							  G_PARAM_READWRITE));
}

GType
gst_dsp_venc_get_type(void)
{
	static GType type = 0;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo type_info = {
			.class_size = sizeof(GstDspVEncClass),
			.class_init = class_init,
			.base_init = base_init,
			.instance_size = sizeof(GstDspVEnc),
			.instance_init = instance_init,
		};

		type = g_type_register_static(GST_DSP_BASE_TYPE, "GstDspVEnc", &type_info, 0);
	}

	return type;
}
