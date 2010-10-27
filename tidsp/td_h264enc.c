/*
 * Copyright (C) 2009-2010 Felipe Contreras
 * Copyright (C) 2009-2010 Nokia Corporation
 *
 * Authors:
 * Felipe Contreras <felipe.contreras@gmail.com>
 * Juha Alanen <juha.m.alanen@nokia.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "dsp_bridge.h"
#include "dmm_buffer.h"

#include "gstdspbase.h"
#include "gstdspvenc.h"

struct create_args {
	uint32_t size;
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
	uint32_t max_bitrate;
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

static void create_args(GstDspBase *base, unsigned *profile_id, void **arg_data)
{
	GstDspVEnc *self = GST_DSP_VENC(base);

	struct create_args args = {
		.size = sizeof(args) - 4,
		.num_streams = 2,
		.in_id = 0,
		.in_type = 0,
		.in_count = base->ports[0]->num_buffers,
		.out_id = 1,
		.out_type = 0,
		.out_count = base->ports[1]->num_buffers,
		.width = self->width,
		.height = self->height,
		.max_bitrate = self->max_bitrate,
		.bitstream_buf_size = base->output_buffer_size,
		.intra_frame_period = self->framerate,
		.framerate = self->framerate * 1000,
		.yuv_format = (self->color_format == GST_MAKE_FOURCC('U','Y','V','Y') ? 2 : 0),
		.num_ref_frames = 1, /* not supported */
		.rc_algorithm = 1, /* 0 = var, 1 == constant, 2 == none */
		.deblocking_enable = 1,
		.mv_range = 64,
		.qpi_frame = 28,
		.profile = 66, /* Baseline profile */
		.level = self->level,
		.nal_mode = self->priv.h264.bytestream ? 2 : 1, /* 0 == bytestream, 1 == NALU, 2 == bytestream, with NALU sizes */
		.encoding_preset = 3,
	};

	if (self->mode == 0)
		args.rc_algorithm = 0; /* storage VBR */
	else
		args.rc_algorithm = 1; /* low delay CBR */

	if (self->width * self->height > 352 * 288)
		*profile_id = 2;
	else if (self->width * self->height > 176 * 144)
		*profile_id = 1;
	else
		*profile_id = 0;

	*arg_data = malloc(sizeof(args));
	memcpy(*arg_data, &args, sizeof(args));
}

struct in_params {
	uint32_t params_size;
	uint32_t input_height;
	uint32_t input_width;
	uint32_t ref_framerate;
	uint32_t framerate;
	uint32_t bitrate;
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

struct out_params {
	uint32_t bitstream_size;
	int32_t frame_type;
	uint32_t nalus_per_frame;
#if SN_API >= 1
	uint32_t nalu_sizes[240];
#else
	uint32_t nalu_sizes[1618];
#endif
	uint32_t frame_index; /* Gives the number of the input frame which NAL unit belongs */
	uint32_t nalu_index; /* Number of current NAL unit inside the frame */
#if SN_API >= 1
	int32_t error_code;
#endif
};

static void in_send_cb(GstDspBase *base,
		du_port_t *port,
		dmm_buffer_t *p,
		dmm_buffer_t *b)
{
	struct in_params *param;
	GstDspVEnc *self = GST_DSP_VENC(base);
	param = p->data;
	param->frame_index = g_atomic_int_exchange_and_add(&self->frame_index, 1);
	param->bitrate = g_atomic_int_get(&self->bitrate);
	g_mutex_lock(self->keyframe_mutex);
	param->force_i_frame = self->keyframe_event ? 1 : 0;
	if (self->keyframe_event) {
		gst_pad_push_event(base->srcpad, self->keyframe_event);
		self->keyframe_event = NULL;
	}
	g_mutex_unlock(self->keyframe_mutex);

	/* hack to manually force keyframes */
	if (!(self->mode == 1 && self->intra_refresh)) {
		param->force_i_frame |= self->force_i_frame_counter >=
			self->keyframe_interval * self->framerate;
		if (param->force_i_frame)
			self->force_i_frame_counter = 0;
		self->force_i_frame_counter++;
	}
}

static void create_codec_data(GstDspBase *base)
{
	GstDspVEnc *self = GST_DSP_VENC(base);
	guint8 *sps, *pps, *codec_data;
	guint16 sps_size, pps_size, offset;

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

static void strip_sps_pps_header(GstDspBase *base,
		dmm_buffer_t *b,
		struct out_params *param)
{
	GstDspVEnc *self = GST_DSP_VENC(base);
	char *data = b->data;
	unsigned i;

	if (param->nalus_per_frame <= 1)
		return;

	if (!self->priv.h264.sps_received) {
		self->priv.h264.sps_received = TRUE;
		return;
	}

	for (i = 0; i < param->nalus_per_frame; i++) {
		if ((data[4] & 0x1f) == 7 || (data[4] & 0x1f) == 8) {
			data += param->nalu_sizes[i];
			b->data = data;
			b->len -= param->nalu_sizes[i];
		}
	}
}

static void ignore_sps_pps_header(GstDspBase *base, dmm_buffer_t *b)
{
	char *data = b->data;

	if ((data[4] & 0x1f) == 7 || (data[4] & 0x1f) == 8)
		base->skip_hack_2++;
}

static void out_recv_cb(GstDspBase *base,
		du_port_t *port,
		dmm_buffer_t *p,
		dmm_buffer_t *b)
{
	GstDspVEnc *self = GST_DSP_VENC(base);
	struct out_params *param;
	param = p->data;

	pr_debug(base, "frame type: %d", param->frame_type);
	b->keyframe = (param->frame_type == 1 || param->frame_type == 4);

	if (b->len == 0)
		return;

	if (self->priv.h264.bytestream) {
		if (self->mode == 1 && b->keyframe)
			strip_sps_pps_header(base, b, param);
		return;
	}

	if (G_LIKELY(self->priv.h264.codec_data_done)) {
		/* prefix the NALU with a lenght field, not counting the start code */
		*(uint32_t*)b->data = GINT_TO_BE(b->len - 4);
		if (!self->priv.h264.bytestream)
			ignore_sps_pps_header(base, b);
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
			create_codec_data(base);
			if (gstdsp_set_codec_data_caps(base, self->priv.h264.codec_data)) {
				self->priv.h264.codec_data_done = TRUE;
				gst_buffer_replace(&self->priv.h264.sps, NULL);
				gst_buffer_replace(&self->priv.h264.pps, NULL);
				gst_buffer_replace(&self->priv.h264.codec_data, NULL);
			}
		}
		base->skip_hack_2++;
	}
}

static void setup_in_params(GstDspBase *base, dmm_buffer_t *tmp)
{
	struct in_params *in_param;
	GstDspVEnc *self = GST_DSP_VENC(base);
	int bits_per_mb;

	in_param = tmp->data;
	in_param->params_size = sizeof(*in_param);
	in_param->input_height = self->height;
	in_param->input_width = self->width;
	in_param->ref_framerate = self->framerate * 1000;
	in_param->framerate = self->framerate * 1000;
	in_param->bitrate = self->bitrate;

	/* QP selection for the first frame */
	bits_per_mb = (in_param->bitrate / self->framerate) /
		(in_param->input_width * in_param->input_height / 256);
	if (bits_per_mb >= 50) {
		in_param->qp_intra = 0x1c;
		in_param->qp_inter = 0x1c;
	} else {
		in_param->qp_intra = 0x28;
		in_param->qp_inter = 0x28;
	}

	in_param->qp_max = 0x33;
	in_param->max_mbs_per_slice = 3620;
	in_param->max_bytes_per_slice = 327680;
	in_param->max_mv_per_mb = 4;
	in_param->intra_4x4_enable_idc = 2;

	if (self->mode == 1 && self->intra_refresh) {
		unsigned pixels;

		/* Max delay in CBR, unit is is 1/30th of a second */
		in_param->intra_frame_interval = 0;

		/* For refreshing all the macroblocks in 3 sec */
		in_param->air_mb_period = self->framerate * 3;

		/* At least two intra macroblocks per frame */
		pixels = self->width * self->height;
		if (in_param->air_mb_period > pixels / (256 * 2))
			in_param->air_mb_period = pixels / (256 * 2);

		/*
		 * Intra refresh methods:
		 * 0 Doesn't insert forcefully intra macro blocks
		 * 1 Inserts intra macro blocks in a cyclic fashion :
		 *   cyclic interval is equal to airMbPeriod
		 * 3 Position of intra macro blocks is intelligently
		 *   chosen by encoder, but the number of forcely coded
		 *   intra macro blocks in a frame is guaranteed to be
		 *   equal to totalMbsInFrame/airMbPeriod
		 */
		in_param->intra_refresh_method = 1;
	}
}

static void setup_params(GstDspBase *base)
{
	struct in_params *in_param;
	struct out_params *out_param;
	du_port_t *p;

	p = base->ports[0];
	gstdsp_port_setup_params(base, p, sizeof(*in_param), setup_in_params);
	p->send_cb = in_send_cb;

	p = base->ports[1];
	gstdsp_port_setup_params(base, p, sizeof(*out_param), NULL);
	p->recv_cb = out_recv_cb;
}

struct td_codec td_h264enc_codec = {
	.uuid = &(const struct dsp_uuid) { 0x63A3581A, 0x09D7, 0x4AD0, 0x80, 0xB8,
		{ 0x5F, 0x2C, 0x4D, 0x4D, 0x59, 0xC9 } },
	.filename = "h264venc_sn.dll64P",
	.setup_params = setup_params,
	.create_args = create_args,
};
