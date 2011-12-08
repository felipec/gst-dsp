/*
 * Copyright (C) 2009-2010 Felipe Contreras
 * Copyright (C) 2009-2010 Nokia Corporation
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
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
		.vbv_size = 112,
		.color_format = (self->color_format == GST_MAKE_FOURCC('U','Y','V','Y') ? 2 : 0),
		.unrestricted_mv = 1,
		.framerate = self->framerate,
		.qp_first = 12,
		.profile = 1,
		.max_delay = 300,
		.vbv_enable = 1,
		.use_vos = 1,
	};

	args.is_mpeg4 = base->alg == GSTDSP_MP4VENC ? 1 : 0;

	if (base->alg == GSTDSP_MP4VENC)
		args.level = 5;
	else
		args.level = 20;

	if (self->mode == 0) {
		args.gob_interval = 0;
		args.hec = 0;
		args.resync_marker = 0;
		args.rate_control = 2;
	} else {
		args.gob_interval = 1;
		args.hec = 1;
		args.resync_marker = 1;
		args.rate_control = 1;
	}

	if (self->width * self->height > 720 * 480)
		*profile_id = 4;
	else if (self->width * self->height > 640 * 480)
		*profile_id = 3;
	else if (self->width * self->height > 352 * 288)
		*profile_id = 2;
	else if (self->width * self->height > 176 * 144)
		*profile_id = 1;
	else
		*profile_id = 0;

	*arg_data = malloc(sizeof(args));
	memcpy(*arg_data, &args, sizeof(args));
}

static void try_extract_codec_data(GstDspBase *base, dmm_buffer_t *b)
{
	GstDspVEnc *self = GST_DSP_VENC(base);
	guint8 gov[] = { 0x0, 0x0, 0x1, 0xB3 };
	guint8 vop[] = { 0x0, 0x0, 0x1, 0xB6 };
	guint8 *data;
	GstBuffer *codec_buf;

	if (G_LIKELY(self->priv.mpeg4.codec_data_done))
		return;

	if (!b->len)
		return;

	/* only mind codec-data for storage */
	if (self->mode)
		goto done;

	/*
	 * Codec data expected in first frame,
	 * and runs from VOSH to GOP (not including); so locate the latter one.
	 */
	data = memmem(b->data, b->len, gov, 4);

	if (!data) {
		/* maybe no GOP is in the stream, look for first VOP */
		data = memmem(b->data, b->len, vop, 4);
	}

	if (!data) {
		pr_err(self, "failed to extract mpeg4 codec-data");
		goto done;
	}

	codec_buf = gst_buffer_new();
	GST_BUFFER_DATA(codec_buf) = b->data;
	GST_BUFFER_SIZE(codec_buf) = data - (guint8 *) b->data;
	gstdsp_set_codec_data_caps(base, codec_buf);
	gst_buffer_unref(codec_buf);
done:
	self->priv.mpeg4.codec_data_done = TRUE;
}

struct in_params {
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

	/* SN_API >= 1 */
	uint32_t qp_max;
	uint32_t qp_min;
};

struct out_params {
	uint32_t bitstream_size;
	uint32_t frame_type;
	uint32_t mv_data_size;
	uint32_t num_packets;
	uint8_t mv_data[12960];
	uint8_t resync_data[6480];

	/* SN_API >= 1 */
	uint32_t frame_index;
	uint32_t error_code;
};

static void out_recv_cb(GstDspBase *base, struct td_buffer *tb)
{
	struct out_params *param;
	param = tb->params->data;
	tb->keyframe = (param->frame_type == 1);
	if (base->alg == GSTDSP_MP4VENC)
		try_extract_codec_data(base, tb->data);
}

static void in_send_cb(GstDspBase *base, struct td_buffer *tb)
{
	struct in_params *param;
	GstDspVEnc *self = GST_DSP_VENC(base);

	param = tb->params->data;
	param->frame_index = g_atomic_int_exchange_and_add(&self->frame_index, 1);
	param->bitrate = g_atomic_int_get(&self->bitrate);
	g_mutex_lock(self->keyframe_mutex);
	param->force_i_frame = self->keyframe_event ? 1 : 0;
	if (self->keyframe_event) {
		gst_pad_push_event(base->srcpad, self->keyframe_event);
		self->keyframe_event = NULL;
	}
	g_mutex_unlock(self->keyframe_mutex);
}

static void setup_in_params(GstDspBase *base, dmm_buffer_t *tmp)
{
	struct in_params *in_param;
	GstDspVEnc *self = GST_DSP_VENC(base);

	in_param = tmp->data;
	in_param->framerate = self->framerate;
	in_param->bitrate = self->bitrate;
	in_param->i_frame_interval = self->keyframe_interval * self->framerate;
	in_param->air_rate = 10;
	in_param->qp_intra = 8;
	in_param->f_code = 6;
	in_param->half_pel = 1;
	in_param->qp_inter = 8;

	if (base->alg == GSTDSP_MP4VENC)
		in_param->ac_pred = 1;

	if (self->mode == 0) {
		in_param->use_umv = 1;
	} else {
		in_param->resync_interval = 1024;
		in_param->hec_interval = 3;
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

struct td_codec td_mp4venc_codec = {
	.uuid = &(const struct dsp_uuid) { 0x98c2e8d8, 0x4644, 0x11d6, 0x81, 0x18,
		{ 0x00, 0xb0, 0xd0, 0x8d, 0x72, 0x9f } },
	.filename = "m4venc_sn.dll64P",
	.setup_params = setup_params,
	.create_args = create_args,
};
