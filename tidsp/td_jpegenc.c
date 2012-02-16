/*
 * Copyright (C) 2009-2010 Felipe Contreras
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
#include "gstdspjpegenc.h"

struct create_args {
	uint32_t size;
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

	/* SN_API >= 1 */
	uint16_t convert;
	uint16_t max_app5_width;
	uint16_t max_app5_height;
};

static void create_args(GstDspBase *base, unsigned *profile_id, void **arg_data)
{
	struct create_args args = {
		.size = sizeof(args) - 4,
		.num_streams = 2,
		.in_id = 0,
		.in_type = 0,
		.in_count = base->ports[0]->num_buffers,
		.out_id = 1,
		.out_type = 0,
		.out_count = base->ports[1]->num_buffers,
		.max_width = JPEGENC_MAX_WIDTH + 32,
		.max_height = JPEGENC_MAX_HEIGHT + 32,
		.color_format = 1,
	};

	*profile_id = 1;

	*arg_data = malloc(sizeof(args));
	memcpy(*arg_data, &args, sizeof(args));
}

struct dyn_params {
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

#if SN_API > 1
	/* apparently only sizes 32 and 52 work. */
	uint32_t resize;
#endif
};

static void send_params(GstDspBase *base, struct dsp_node *node)
{
	struct dyn_params *params;
	dmm_buffer_t *b;
	GstDspVEnc *self = GST_DSP_VENC(base);

	b = dmm_buffer_calloc(base->dsp_handle, base->proc,
			sizeof(*params), DMA_TO_DEVICE);

	params = b->data;
	params->size = sizeof(*params);
	params->color_format = (self->color_format == GST_MAKE_FOURCC('U','Y','V','Y') ? 4 : 1);
	params->width = self->width;
	params->height = self->height;
	params->capture_width = self->width;
	params->quality = self->quality;

	params->capture_height = self->height;

	gstdsp_send_alg_ctrl(base, base->node, b);
}

struct in_params {
	uint32_t size;
};

struct out_params {
	uint32_t errorcode;
};

static void setup_in_params(GstDspBase *base, dmm_buffer_t *tmp)
{
	struct in_params *in_param;

	in_param = tmp->data;
	in_param->size = sizeof(*in_param);
}

static void setup_params(GstDspBase *base)
{
	struct in_params *in_param;
	struct out_params *out_param;
	du_port_t *p;

	p = base->ports[0];
	gstdsp_port_setup_params(base, p, sizeof(*in_param), setup_in_params);

	p = base->ports[1];
	gstdsp_port_setup_params(base, p, sizeof(*out_param), NULL);
}

struct td_codec td_jpegenc_codec = {
	.uuid = &(const struct dsp_uuid) { 0xcb70c0c1, 0x4c85, 0x11d6, 0xb1, 0x05,
		{ 0x00, 0xc0, 0x4f, 0x32, 0x90, 0x31 } },
	.filename = "jpegenc_sn.dll64P",
	.setup_params = setup_params,
	.create_args = create_args,
	.send_params = send_params,
};
