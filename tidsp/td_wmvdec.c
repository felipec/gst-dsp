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
#include "gstdspvdec.h"

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

	uint32_t max_width;
	uint32_t max_height;
	uint32_t color_format;
	uint32_t max_framerate;
	uint32_t max_bitrate;
	uint32_t endianness;
	int32_t profile;
	int32_t max_level;
	uint32_t process_mode;
	int32_t preroll;
	uint32_t stream_format;
	uint32_t stride_width;
};

static void create_args(GstDspBase *base, unsigned *profile_id, void **arg_data)
{
	GstDspVDec *self = GST_DSP_VDEC(base);

	struct create_args args = {
		.size = sizeof(args) - 4,
		.num_streams = 2,
		.in_id = 0,
		.in_type = 0,
		.in_count = base->ports[0]->num_buffers,
		.out_id = 1,
		.out_type = 0,
		.out_count = base->ports[1]->num_buffers,
		.max_width = self->width,
		.max_height = self->height,
		.color_format = self->color_format == GST_MAKE_FOURCC('U', 'Y', 'V', 'Y') ? 4 : 1,
		.endianness = 1,
		.profile = -1,
		.max_level = -1,
		.stream_format = self->wmv_is_vc1 ? 1 : 2, /* 1 = wvc1, 2 = wmv3 */
	};

	if (self->width * self->height > 640 * 480)
		*profile_id = 4;
	else if (self->width * self->height > 352 * 288)
		*profile_id = 3;
	else if (self->width * self->height > 176 * 144)
		*profile_id = 2;
	else
		*profile_id = 1;

	*arg_data = malloc(sizeof(args));
	memcpy(*arg_data, &args, sizeof(args));
}

struct in_params {
	int32_t buf_count;
	uint32_t frame_index;
};

struct out_params {
	uint32_t display_id;
	uint32_t bytes_consumed;
	int32_t  error_code;
	uint32_t frame_type;
};

struct dyn_params {
	int32_t size;
	uint32_t decode_header;
	uint32_t display_width;
	uint32_t frame_skip_mode;
	uint32_t pp_type;
	uint16_t stream_format;
};

struct rcv_struct {
	uint32_t num_frames:24;
	uint32_t frame_type:8;
	uint32_t id;
	uint32_t codec_data;
	uint32_t height;
	uint32_t width;
};

static inline void send_rcv_buffer(GstDspBase *base, GstBuffer *buf)
{
	GstDspVDec *self;
	GstBuffer *rcv_buf;
	struct rcv_struct *rcv_struct;
	guint8 *codec_data;

	self = GST_DSP_VDEC(base);

	rcv_buf = gst_buffer_new_and_alloc(sizeof(*rcv_struct));
	rcv_struct = (struct rcv_struct *) GST_BUFFER_DATA(rcv_buf);
	codec_data = GST_BUFFER_DATA(buf);

	rcv_struct->num_frames = 0xFFFFFF;
	rcv_struct->frame_type = 0x85;
	rcv_struct->id = 0x04;
	rcv_struct->codec_data = codec_data[0] << 0  |
		codec_data[1] << 8  |
		codec_data[2] << 16 |
		codec_data[3] << 24;
	rcv_struct->height = self->height;
	rcv_struct->width = self->width;

	(void) gstdsp_send_codec_data(base, rcv_buf);
	gst_buffer_unref(rcv_buf);
}

static inline void prefix_vc1(GstDspVDec *self, struct td_buffer *tb)
{
	guint8 *input_data, *output_data, *alloc_data;
	gint input_size, output_size;
	dmm_buffer_t *b = tb->data;

	input_data = b->data;
	input_size = b->size;

	/* save this so it is not freed by subsequent allocate */
	alloc_data = b->allocated_data;
	b->allocated_data = NULL;

	if (G_LIKELY(self->codec_data_sent)) {
		output_size = input_size + 4;
		dmm_buffer_allocate(b, output_size);
		output_data = b->data;

		/* prefix buffer with 0x0000010d */
		GST_WRITE_UINT32_BE(output_data, 0x10d);
		output_data += 4;
		memcpy(output_data, input_data, input_size);
	} else {
		GstBuffer *buf = self->codec_data;

		self->codec_data = NULL;

		output_size = GST_BUFFER_SIZE(buf) + 4 + input_size;
		dmm_buffer_allocate(b, output_size);
		output_data = b->data;

		/* copy codec data to the beginning of the first buffer */
		memcpy(output_data, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
		output_data += GST_BUFFER_SIZE(buf);

		/* prefix frame data with 0x0000010d */
		GST_WRITE_UINT32_BE(output_data, 0x10d);
		output_data += 4;
		memcpy(output_data, input_data, input_size);

		self->codec_data_sent = TRUE;
		gst_buffer_unref(buf);
	}
	b->len = output_size;

	/* release original data */
	if (tb->user_data) {
		gst_buffer_unref(tb->user_data);
		tb->user_data = NULL;
	}
	g_free(alloc_data);
	return;
}

static void in_send_cb(GstDspBase *base, struct td_buffer *tb)
{
	GstDspVDec *self = GST_DSP_VDEC(base);
	struct in_params *param;
	param = tb->params->data;

	if (self->wmv_is_vc1)
		prefix_vc1(self, tb);

	param->frame_index = g_atomic_int_exchange_and_add(&self->frame_index, 1);
}

static void out_recv_cb(GstDspBase *base, struct td_buffer *tb)
{
	GstDspVDec *self = GST_DSP_VDEC(base);
	struct out_params *param;
	param = tb->params->data;

	if (param->frame_type == 0xFFFFFFFF)
		pr_warning(self, "empty frame received, frame number: %d",
				param->display_id);

	if (param->error_code != 0)
		pr_debug(self, "error in decoding: 0x%x, frame number: %d frame type: %u",
				param->error_code, param->display_id, param->frame_type);
}

static void setup_params(GstDspBase *base)
{
	GstDspVDec *self = GST_DSP_VDEC(base);
	struct in_params *in_param;
	struct out_params *out_param;
	du_port_t *p;

	self->frame_index = 1;

	p = base->ports[0];
	gstdsp_port_setup_params(base, p, sizeof(*in_param), NULL);
	p->send_cb = in_send_cb;

	p = base->ports[1];
	gstdsp_port_setup_params(base, p, sizeof(*out_param), NULL);
	p->recv_cb = out_recv_cb;
}

static bool handle_extra_data(GstDspBase *base, GstBuffer *buf)
{
	GstDspVDec *self = GST_DSP_VDEC(base);
	if (self->wmv_is_vc1)
		self->codec_data = gst_buffer_ref(buf);
	else
		send_rcv_buffer(base, buf);
	return true;
}

static void send_params(GstDspBase *base, struct dsp_node *node)
{
	struct dyn_params *params;
	dmm_buffer_t *b;
	GstDspVDec *self = GST_DSP_VDEC(base);

	b = dmm_buffer_calloc(base->dsp_handle, base->proc,
			sizeof(*params), DMA_TO_DEVICE);

	params = b->data;
	params->size = (int32_t) sizeof(*params);
	params->stream_format = self->wmv_is_vc1 ? 1 : 2;

	gstdsp_send_alg_ctrl(base, node, b);
}

struct td_codec td_wmvdec_codec = {
	.uuid = &(const struct dsp_uuid) { 0x609DAB97, 0x3DFC, 0x471F, 0x8A, 0xB9,
		{ 0x4E, 0x56, 0xE8, 0x34, 0x50, 0x1B } },
	.filename = "wmv9dec_sn.dll64P",
	.setup_params = setup_params,
	.create_args = create_args,
	.handle_extra_data = handle_extra_data,
	.send_params = send_params,
	.flush_buffer = gstdsp_base_flush_buffer,
};
