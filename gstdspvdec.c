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
#include "gstdspparse.h"
#include "plugin.h"
#include "util.h"

#include "dsp_bridge.h"

#include <string.h> /* for memcpy */

#include "log.h"

#define GST_CAT_DEFAULT gstdsp_debug

enum {
	GSTDSP_MPEG4VDEC,
	GSTDSP_H264DEC,
	GSTDSP_H263DEC,
	GSTDSP_WMVDEC,
};

static GstDspBaseClass *parent_class;

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

	struc = gst_structure_new("video/x-wmv",
				  "wmvversion", G_TYPE_INT, 3,
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

struct mp4vdec_args {
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
		.in_count = base->ports[0]->num_buffers,
		.out_id = 1,
		.out_type = 0,
		.out_count = base->ports[1]->num_buffers,
		.max_width = self->width,
		.max_height = self->height,
		.color_format = 4,
		.max_framerate = 1,
		.max_bitrate = 1,
		.endianness = 1,
		.profile = 0,
		.max_level = -1,
		.mode = 0,
		.preroll = 0,
		.display_width = 0,
	};

	if (base->alg == GSTDSP_H263DEC)
		args.profile = 8;

	struct foo_data *cb_data;

	cb_data = malloc(sizeof(*cb_data));
	cb_data->size = sizeof(args);
	memcpy(&cb_data->data, &args, sizeof(args));

	return cb_data;
}

struct mp4vdec_in_params {
	uint32_t frame_index;
	int32_t buf_count;
	uint32_t ring_io_block_size;
	int32_t performance_mode;
};

struct mp4vdec_out_params {
	uint32_t frame_index;
	uint32_t bytes_consumed;
	int32_t error_code;
	uint32_t frame_type;
	uint32_t qp[(720 * 576) / 256];
	int32_t mb_error_buf_flag;
	uint8_t mb_error_buf[(720 * 576) / 256];
};

static void
mp4vdec_out_recv_cb(GstDspBase *base,
		   du_port_t *port,
		   dmm_buffer_t *p,
		   dmm_buffer_t *b)
{
	GstDspVDec *self = GST_DSP_VDEC(base);
	struct mp4vdec_out_params *param;
	param = p->data;

	b->keyframe = (param->frame_type == 0);

	pr_debug(self, "error: 0x%x, frame number: %u, frame type: %u",
		param->error_code, param->frame_index, param->frame_type);
}

static void
setup_mp4vparams_in(GstDspBase *base, dmm_buffer_t *tmp)
{
	struct mp4vdec_in_params *in_param;

	in_param = tmp->data;
	in_param->performance_mode = 2;
}

static inline void
setup_mp4vdec_params(GstDspBase *base)
{
	struct mp4vdec_in_params *in_param;
	struct mp4vdec_out_params *out_param;
	du_port_t *p;

	p = base->ports[0];
	gstdsp_port_setup_params(base, p, sizeof(*in_param), setup_mp4vparams_in);

	p = base->ports[1];
	gstdsp_port_setup_params(base, p, sizeof(*out_param), NULL);
	p->recv_cb = mp4vdec_out_recv_cb;
}

struct h264vdec_args {
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
		.in_count = base->ports[0]->num_buffers,
		.out_id = 1,
		.out_type = 0,
		.out_count = base->ports[1]->num_buffers,
		.max_width = self->width,
		.max_height = self->height,
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

struct wmvdec_args {
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
	uint32_t process_mode;
	int32_t preroll;
	int32_t stream_format;
	int32_t stride_width;
};

static inline void *
get_wmv_args(GstDspVDec *self)
{
	GstDspBase *base = GST_DSP_BASE(self);

	struct wmvdec_args args = {
		.num_streams = 2,
		.in_id = 0,
		.in_type = 0,
		.in_count = base->ports[0]->num_buffers,
		.out_id = 1,
		.out_type = 0,
		.out_count = base->ports[1]->num_buffers,
		.max_width = self->width,
		.max_height = self->height,
		.color_format = 4,
		.max_framerate = 0,
		.max_bitrate = 0,
		.endianness = 1,
		.profile = -1,
		.max_level = -1,
		.process_mode = 0,
		.preroll = 0,
		.stream_format = self->wmv_is_vc1 ? 1 : 2, /* 1 = wvc1, 2 = wmv3 */
		.stride_width = 0,
	};

	struct foo_data *cb_data;

	cb_data = malloc(sizeof(*cb_data));
	cb_data->size = sizeof(args);
	memcpy(&cb_data->data, &args, sizeof(args));

	return cb_data;
}

struct wmvdec_in_params {
	int32_t buf_count;
};

struct wmvdec_out_params {
	uint32_t display_id;
	uint32_t bytes_consumed;
	int32_t  error_code;
	uint32_t frame_type;
};

struct wmvdec_dyn_params {
	int32_t size;
	uint32_t decode_header;
	uint32_t display_width;
	uint32_t frame_skip_mode;
	uint32_t pp_type;
	uint16_t stream_format;
};

struct wmvdec_rcv_struct {
	uint32_t num_frames:24;
	uint32_t frame_type:8;
	uint32_t id;
	uint32_t codec_data;
	uint32_t height;
	uint32_t width;
};

static inline void
wmvdec_create_rcv_buffer(GstDspBase *base,
			 GstBuffer **buf)
{
	GstDspVDec *self;
	GstBuffer *rcv_buf;
	struct wmvdec_rcv_struct *rcv_struct;
	guint8 *codec_data;

	self = GST_DSP_VDEC(base);

	rcv_buf = gst_buffer_new_and_alloc(sizeof(*rcv_struct));
	rcv_struct = (struct wmvdec_rcv_struct *) GST_BUFFER_DATA(rcv_buf);
	codec_data = GST_BUFFER_DATA(*buf);

	rcv_struct->num_frames = 0xFFFFFF;
	rcv_struct->frame_type = 0x85;
	rcv_struct->id = 0x04;
	rcv_struct->codec_data = codec_data[0] << 0  |
				 codec_data[1] << 8  |
				 codec_data[2] << 16 |
				 codec_data[3] << 24;
	rcv_struct->height = self->height;
	rcv_struct->width = self->width;

	gst_buffer_replace(buf, rcv_buf);
}

static inline void
wmvdec_prefix_vc1(GstDspVDec *self,
		  dmm_buffer_t *b)
{
	guint8 *input_data, *output_data, *alloc_data;
	gint input_size, output_size;

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
		output_size = GST_BUFFER_SIZE(self->codec_data) + 4 + input_size;
		dmm_buffer_allocate(b, output_size);
		output_data = b->data;

		/* copy codec data to the beginning of the first buffer */
		memcpy(output_data, GST_BUFFER_DATA(self->codec_data),
		       GST_BUFFER_SIZE(self->codec_data));
		output_data += GST_BUFFER_SIZE(self->codec_data);

		/* prefix frame data with 0x0000010d */
		GST_WRITE_UINT32_BE(output_data, 0x10d);
		output_data += 4;
		memcpy(output_data, input_data, input_size);

		self->codec_data_sent = TRUE;
		gst_buffer_unref(self->codec_data);
		self->codec_data = NULL;
	}
	b->len = output_size;

	/* release original data */
	if (b->user_data) {
		gst_buffer_unref(b->user_data);
		b->user_data = NULL;
	}
	g_free(alloc_data);
	return;
}

static void
wmvdec_in_send_cb(GstDspBase *base,
		  du_port_t *port,
		  dmm_buffer_t *p,
		  dmm_buffer_t *b)
{
	GstDspVDec *self = GST_DSP_VDEC(base);
	struct wmvdec_in_params *param;
	param = p->data;

	if (self->wmv_is_vc1)
		wmvdec_prefix_vc1(self, b);

	param->buf_count = g_atomic_int_exchange_and_add(&self->frame_index, 1);
}

static void
wmvdec_out_recv_cb(GstDspBase *base,
		   du_port_t *port,
		   dmm_buffer_t *p,
		   dmm_buffer_t *b)
{
	GstDspVDec *self = GST_DSP_VDEC(base);
	struct wmvdec_out_params *param;
	param = p->data;

	if (param->frame_type == 0xFFFFFFFF)
		pr_warning(self, "empty frame received, frame number: %d",
			   param->display_id);

	if (param->error_code != 0)
		pr_debug(self, "error in decoding: 0x%x, frame number: %d frame type: %u",
			 param->error_code, param->display_id, param->frame_type);
}

static inline void
wmvdec_send_params(GstDspBase *base,
		   dsp_node_t *node)
{
	struct wmvdec_dyn_params *params;
	dmm_buffer_t *b;
	GstDspVDec *self = GST_DSP_VDEC(base);

	b = dmm_buffer_calloc(base->dsp_handle, base->proc,
			      sizeof(*params), DMA_TO_DEVICE);

	params = b->data;
	params->size = (int32_t) sizeof(*params);
	params->stream_format = self->wmv_is_vc1 ? 1 : 2;

	gstdsp_send_alg_ctrl(base, node, b);
}

static inline void
setup_wmvparams(GstDspBase *base)
{
	struct wmvdec_in_params *in_param;
	struct wmvdec_out_params *out_param;
	du_port_t *p;

	p = base->ports[0];
	gstdsp_port_setup_params(base, p, sizeof(*in_param), NULL);
	p->send_cb = wmvdec_in_send_cb;

	p = base->ports[1];
	gstdsp_port_setup_params(base, p, sizeof(*out_param), NULL);
	p->recv_cb = wmvdec_out_recv_cb;
}

static GstBuffer *
h264dec_transform_codec_data(GstDspVDec *self,
			     GstBuffer *buf)
{
	guint8 *data, *outdata;
	guint size, total_size = 0, len, num_sps, num_pps;
	guint lol;
	guint val;
	guint i;
	GstBuffer *new;

	/* extract some info and transform into codec expected format, which is:
	 * lol bytes (BE) SPS size, SPS, lol bytes (BE) PPS size, PPS */

	data = GST_BUFFER_DATA(buf);
	size = GST_BUFFER_SIZE(buf);

	if (size < 8)
		goto fail;

	lol = (data[4] & (0x3)) + 1;
	num_sps = data[5] & 0x1f;
	data += 6;
	size -= 6;
	for (i = 0; i < num_sps; i++) {
		len = GST_READ_UINT16_BE(data);
		total_size += len + lol;
		data += len + 2;
		if (size < len + 2)
			goto fail;
		size -= len + 2;
	}
	num_pps = data[0];
	data++;
	size++;
	for (i = 0; i < num_pps; i++) {
		len = GST_READ_UINT16_BE(data);
		total_size += len + lol;
		data += len + 2;
		if (size < len + 2)
			goto fail;
		size -= len + 2;
	}

	/* save original data */
	new = gst_buffer_new_and_alloc(total_size);
	data = GST_BUFFER_DATA(buf);
	outdata = GST_BUFFER_DATA(new);

	data += 6;
	for (i = 0; i < num_sps; ++i) {
		len = GST_READ_UINT16_BE(data);
		val = len << (8 * (4 - lol));
		GST_WRITE_UINT32_BE(outdata, len);
		memcpy(outdata + lol, data + 2, len);
		outdata += len + lol;
		data += 2 + len;
	}
	data += 1;
	for (i = 0; i < num_pps; ++i) {
		len = GST_READ_UINT16_BE(data);
		val = len << (8 * (4 - lol));
		GST_WRITE_UINT32_BE(outdata, len);
		memcpy(outdata + lol, data + 2, len);
		outdata += len + lol;
		data += 2 + len;
	}

	pr_debug(self, "lol: %d", lol);
	self->priv.h264.lol = lol;

	return new;

fail:
	pr_warning(self, "failed to transform h264 to codec format");
	return NULL;
}

static void
h264dec_transform_nal_encoding(GstDspVDec *self,
			       dmm_buffer_t *b)
{
	guint8 *data;
	gint size;
	gint lol;
	guint val, nal;

	data = b->data;
	size = b->size;
	lol = self->priv.h264.lol;

	nal = 0;
	while (size) {
		if (size < lol)
			goto fail;

		/* get NAL size encoded in BE lol bytes */
		val = GST_READ_UINT32_BE(data);
		val >>= ((4 - lol) << 3);
		if (lol == 4)
			/* blank size prefix with 00 00 00 01 */
			GST_WRITE_UINT32_BE(data, 0x01);
		else if (lol == 3)
			/* blank size prefix with 00 00 01 */
			GST_WRITE_UINT24_BE(data, 0x01);
		else
			nal++;
		data += lol + val;
		size -= lol + val;
	}

	if (lol < 3) {
		/* slower, but unlikely path; need to copy stuff to make room for sync */
		guint8 *odata, *alloc_data;
		gint osize;

		/* set up for next run */
		data = b->data;
		size = b->size;
		osize = size + nal * (4 - lol);
		/* save this so it is not free'd by subsequent allocate */
		alloc_data = b->allocated_data;
		b->allocated_data = NULL;
		dmm_buffer_allocate(b, osize);
		b->len = osize;

		odata = b->data;
		while (size) {
			if (size < lol)
				goto fail;

			/* get NAL size encoded in BE lol bytes */
			val = GST_READ_UINT32_BE(data);
			val >>= ((4 - lol) << 3);
			GST_WRITE_UINT32_BE(odata, 0x01);
			odata += 4;
			data += lol;
			memcpy(odata, data, val);
			odata += val;
			data += val;
			size -= lol + val;
		}
		/* now release original data */
		if (b->user_data) {
			gst_buffer_unref(b->user_data);
			b->user_data = NULL;
		}
		free(alloc_data);
	}
	return;

fail:
	pr_warning(self, "failed to transform h264 to codec format");
	return;
}

struct h264dec_out_stream_params {
	uint32_t display_id;
	uint32_t bytes_consumed;
	int32_t error_code;
	uint32_t frame_type;
	uint32_t num_of_nalu;
	int32_t mb_err_status_flag;
	int8_t mb_err_status_out[1620];
};

static void
h264dec_out_recv_cb(GstDspBase *base,
		    du_port_t *port,
		    dmm_buffer_t *p,
		    dmm_buffer_t *b)
{
	struct h264dec_out_stream_params *param;
	param = p->data;

	pr_debug(base, "receive %d/%d",
		 b->len, base->output_buffer_size);
	pr_debug(base, "error: 0x%x, frame type: %d",
		 param->error_code, param->frame_type);
	if (param->error_code & 0xffff)
		pr_err(base, "decode error");
}

static void
h264dec_in_send_cb(GstDspBase *base,
		   du_port_t *port,
		   dmm_buffer_t *p,
		   dmm_buffer_t *b)
{
	GstDspVDec *vdec = GST_DSP_VDEC(base);
	/* transform MP4 format to bytestream format */
	if (G_LIKELY(vdec->priv.h264.lol)) {
		pr_debug(base, "transforming H264 buffer data");
		/* intercept and transform into dsp expected format */
		h264dec_transform_nal_encoding(vdec, b);
	} else {
		/* no more need for callback */
		port->send_cb = NULL;
	}
}

static inline void
setup_h264params(GstDspBase *base)
{
	struct h264dec_out_stream_params *out_param;
	du_port_t *p;

	p = base->ports[0];
	p->send_cb = h264dec_in_send_cb;

	p = base->ports[1];
	gstdsp_port_setup_params(base, p, sizeof(*out_param), NULL);
	p->recv_cb = h264dec_out_recv_cb;
}

static void *
create_node(GstDspBase *base)
{
	GstDspVDec *self;
	int dsp_handle;
	dsp_node_t *node;
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

	const dsp_uuid_t wmv_dec_uuid = { 0x609DAB97, 0x3DFC, 0x471F, 0x8A, 0xB9,
		{ 0x4E, 0x56, 0xE8, 0x34, 0x50, 0x1B } };

#if SN_API >= 1
	const dsp_uuid_t conversions_uuid = { 0x722DD0DA, 0xF532, 0x4238, 0xB8, 0x46,
		{ 0xAB, 0xFF, 0x5D, 0xA4, 0xBA, 0x02 } };
#endif

	self = GST_DSP_VDEC(base);
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
	case GSTDSP_H263DEC:
		alg_uuid = &mp4v_dec_uuid;
		alg_fn = "mp4vdec_sn.dll64P";
		break;
	case GSTDSP_H264DEC:
		alg_uuid = &h264v_dec_uuid;
		alg_fn = "h264vdec_sn.dll64P";
		break;
	case GSTDSP_WMVDEC:
		alg_uuid = &wmv_dec_uuid;
		alg_fn = "wmv9dec_sn.dll64P";
		break;
	default:
		pr_err(self, "unknown algorithm");
		return NULL;
	}

#if SN_API >= 1
	if (!gstdsp_register(dsp_handle, &conversions_uuid, DSP_DCD_LIBRARYTYPE, "conversions.dll64P")) {
		pr_err(self, "failed to register conversions node library");
		return NULL;
	}
#endif

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
		case GSTDSP_H263DEC:
			if (self->width * self->height > 640 * 480)
				attrs.profile_id = 4;
			else if (self->width * self->height > 352 * 288)
				attrs.profile_id = 3;
			else if (self->width * self->height > 176 * 144)
				attrs.profile_id = 2;
			else
				attrs.profile_id = 1;
			cb_data = get_mp4v_args(self);
			break;
		case GSTDSP_H264DEC:
			if (self->width * self->height > 352 * 288)
				attrs.profile_id = 3;
			else if (self->width * self->height > 176 * 144)
				attrs.profile_id = 2;
			else
				attrs.profile_id = 1;
			cb_data = get_h264_args(self);
			break;
		case GSTDSP_WMVDEC:
			if (self->width * self->height > 640 * 480)
				attrs.profile_id = 4;
			else if (self->width * self->height > 352 * 288)
				attrs.profile_id = 3;
			else if (self->width * self->height > 176 * 144)
				attrs.profile_id = 2;
			else
				attrs.profile_id = 1;
			cb_data = get_wmv_args(self);
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
		dsp_node_free(dsp_handle, node);
		return NULL;
	}

	pr_info(self, "dsp node created");

	switch (base->alg) {
	case GSTDSP_WMVDEC:
		setup_wmvparams(base);
		wmvdec_send_params(base, node);
		break;
	case GSTDSP_H264DEC:
		setup_h264params(base);
		break;
	case GSTDSP_MPEG4VDEC:
		setup_mp4vdec_params(base);
		break;
	default:
		break;
	}

	return node;
}

static inline bool
destroy_node(GstDspVDec *self,
	     int dsp_handle,
	     dsp_node_t *node)
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

static inline gboolean
handle_codec_data(GstDspVDec *self,
		  GstStructure *in_struc)
{
	GstDspBase *base = GST_DSP_BASE(self);
	const GValue *codec_data;
	GstBuffer *buf;

	codec_data = gst_structure_get_value(in_struc, "codec_data");
	if (!codec_data)
		return TRUE;

	buf = gst_value_get_buffer(codec_data);

	switch (base->alg) {
	case GSTDSP_MPEG4VDEC:
		base->skip_hack++;
		break;
	case GSTDSP_WMVDEC:
		if (!self->wmv_is_vc1) {
			wmvdec_create_rcv_buffer(base, &buf);
		} else {
			self->codec_data = gst_buffer_ref(buf);
			return TRUE;
		}
		break;
	case GSTDSP_H264DEC:
		buf = h264dec_transform_codec_data(self, buf);
		if (!buf) {
			gstdsp_got_error(base, 0, "invalid codec_data");
			return FALSE;
		}
		break;
	default:
		break;
	}
	return gstdsp_send_codec_data(base, buf);
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
	gboolean ret;

	self = GST_DSP_VDEC(GST_PAD_PARENT(pad));
	base = GST_DSP_BASE(self);

	{
		gchar *str = gst_caps_to_string(caps);
		pr_info(self, "sink caps: %s", str);
		g_free(str);
	}

	in_struc = gst_caps_get_structure(caps, 0);

	name = gst_structure_get_name(in_struc);
	if (strcmp(name, "video/x-h264") == 0) {
		base->alg = GSTDSP_H264DEC;
		self->priv.h264.lol = 0;
	}
	else if (strcmp(name, "video/x-h263") == 0) {
		base->alg = GSTDSP_H263DEC;
		base->parse_func = gst_dsp_h263_parse;
	}
	else if (strcmp(name, "video/x-wmv") == 0) {
		guint32 fourcc;
		base->alg = GSTDSP_WMVDEC;

		if (gst_structure_get_fourcc(in_struc, "fourcc", &fourcc) ||
		    gst_structure_get_fourcc(in_struc, "format", &fourcc))
		{
			if (fourcc == GST_MAKE_FOURCC('W', 'V', 'C', '1'))
				self->wmv_is_vc1 = TRUE;
			else
				self->wmv_is_vc1 = FALSE;
		}
	}
	else
		base->alg = GSTDSP_MPEG4VDEC;

	out_caps = gst_caps_new_empty();

	out_struc = gst_structure_new("video/x-raw-yuv",
				      "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC('U', 'Y', 'V', 'Y'),
				      NULL);

	if (gst_structure_get_int(in_struc, "width", &self->width))
		gst_structure_set(out_struc, "width", G_TYPE_INT, self->width, NULL);
	if (gst_structure_get_int(in_struc, "height", &self->height))
		gst_structure_set(out_struc, "height", G_TYPE_INT, self->height, NULL);

	base->output_buffer_size = self->width * self->height * 2;

	{
		const GValue *framerate = NULL;
		framerate = gst_structure_get_value(in_struc, "framerate");
		if (framerate)
			gst_structure_set_value(out_struc, "framerate", framerate);
		else
			/* FIXME this is a workaround for xvimagesink */
			gst_structure_set(out_struc, "framerate",
					  GST_TYPE_FRACTION, 0, 1, NULL);
	}

	gst_caps_append_structure(out_caps, out_struc);
	base->tmp_caps = out_caps;

	ret = gst_pad_set_caps(pad, caps);

	if (!ret)
		return FALSE;

	return handle_codec_data(self, in_struc);
}

static void
instance_init(GTypeInstance *instance,
	      gpointer g_class)
{
	GstDspBase *base;

	base = GST_DSP_BASE(instance);

	base->use_pad_alloc = TRUE;
	base->create_node = create_node;

	base->ports[0] = du_port_new(0, 2, DMA_TO_DEVICE);
	base->ports[1] = du_port_new(1, 2, DMA_FROM_DEVICE);

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
gst_dsp_vdec_get_type(void)
{
	static GType type;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo type_info = {
			.class_size = sizeof(GstDspVDecClass),
			.class_init = class_init,
			.base_init = base_init,
			.instance_size = sizeof(GstDspVDec),
			.instance_init = instance_init,
		};

		type = g_type_register_static(GST_DSP_BASE_TYPE, "GstDspVDec", &type_info, 0);
	}

	return type;
}
