/*
 * Copyright (C) 2010 Igalia S.L.
 *
 * Author: Víctor Manuel Jáquez Leal <vjaquez@igalia.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "dsp_bridge.h"
#include "dmm_buffer.h"

#include "gstdspbase.h"
#include "gstdspvdec.h"

struct jpegdec_args {
	uint32_t size;
	uint16_t num_streams;

	uint16_t in_id;
	uint16_t in_type;
	uint16_t in_count;

	uint16_t out_id;
	uint16_t out_type;
	uint16_t out_count;

	uint16_t max_height;
	uint16_t max_width;
	uint16_t progressive;

	uint16_t color_format;
	uint16_t unknown;
	uint16_t sections_input;
	uint16_t sections_output;

	uint16_t is_argb32;
};

struct jpegdec_in_params {
	int32_t buf_count;
	uint32_t frame_count;
	uint32_t frame_align;
	uint32_t frame_size;
	uint32_t display_width;
	uint32_t reserved_0;
	uint32_t reserved_1;
	uint32_t reserved_2;
	uint32_t reserved_3;
	uint32_t resize_option;
	uint32_t num_mcu;
	uint32_t decode_header;
	uint32_t max_height;
	uint32_t max_width;
	uint32_t max_scans;
	uint32_t endianness;
	uint32_t color_format;
	uint32_t rgb_format;
	uint32_t num_mcu_row;
	uint32_t x_org;
	uint32_t y_org;
	uint32_t x_lenght;
	uint32_t y_length;
	uint32_t argb;
	uint32_t total_size;
};

struct jpegdec_out_params {
	int32_t buf_count;
	uint32_t frame_count;
	uint32_t frame_align;
	uint32_t frame_size;
	uint32_t img_format;
	uint32_t width;
	uint32_t height;
	uint32_t progressive;
	uint32_t error_code;
	uint32_t reserved_0;
	uint32_t reserved_1;
	uint32_t reserved_2;
	uint32_t last_mcu;
	uint32_t stride[3];
	uint32_t output_height;
	uint32_t output_width;
	uint32_t total_au;
	uint32_t bytes_consumed;
	uint32_t current_au;
	uint32_t current_scan;
	int32_t dsp_error;
};

static void
create_jpegdec_args(GstDspBase *base, unsigned *profile_id, void **arg_data)
{
	GstDspVDec *self = GST_DSP_VDEC(base);

	struct jpegdec_args args = {
		.size = sizeof(args) - 4,
		.num_streams = 2,
		.in_id = 0,
		.in_type = 0,
		.in_count = base->ports[0]->num_buffers,
		.out_id = 1,
		.out_type = 0,
		.out_count = base->ports[1]->num_buffers,
		.max_height = self->height,
		.max_width = self->width,
		.progressive = self->jpeg_is_interlaced ? 1 : 0,
		.color_format = self->color_format == GST_MAKE_FOURCC('U', 'Y', 'V', 'Y') ? 4 : 1,
	};

	if (self->jpeg_is_interlaced) {
		if (self->width * self->height > 2560 * 2048)
			*profile_id = 9;
		else if (self->width * self->height > 2560 * 1600)
			*profile_id = 8;
		else if (self->width * self->height > 2048 * 1536)
			*profile_id = 7;
		else if (self->width * self->height > 1920 * 1200)
			*profile_id = 6;
		else if (self->width * self->height > 1280 * 1024)
			*profile_id = 5;
		else if (self->width * self->height > 800 * 600)
			*profile_id = 4;
		else if (self->width * self->height > 640 * 480)
			*profile_id = 3;
		else if (self->width * self->height > 352 * 288)
			*profile_id = 2;
		else if (self->width * self->height > 176 * 144)
			*profile_id = 1;
		else
			*profile_id = 0;
	}
	else
		*profile_id = -1;

	*arg_data = malloc(sizeof(args));
	memcpy(*arg_data, &args, sizeof(args));
}

static void
setup_jpegparams_in(GstDspBase *base, dmm_buffer_t *tmp)
{
	struct jpegdec_in_params *in_param;
	GstDspVDec *self = GST_DSP_VDEC(base);

	in_param = tmp->data;
	in_param->frame_count = 1;
	in_param->frame_align = 4;
	in_param->display_width = 1600;
	in_param->color_format = self->color_format == GST_MAKE_FOURCC('U', 'Y', 'V', 'Y') ? 4 : 1;
	in_param->rgb_format = 9;
}

static void
setup_jpegdec_params(GstDspBase *base)
{
	struct jpegdec_in_params *in_param;
	struct jpegdec_out_params *out_param;
	du_port_t *p;

	p = base->ports[0];
	gstdsp_port_setup_params(base, p, sizeof(*in_param), setup_jpegparams_in);

	p = base->ports[1];
	gstdsp_port_setup_params(base, p, sizeof(*out_param), NULL);
}

struct td_codec td_jpegdec_codec = {
	.uuid = &(const struct dsp_uuid) { 0x5D9CB711, 0x4645, 0x11d6, 0xb0, 0xdc,
		{ 0x00, 0xc0, 0x4f, 0x1f, 0xc0, 0x36 } },
	.filename = "jpegdec_sn.dll64P",
	.setup_params = setup_jpegdec_params,
	.create_args = create_jpegdec_args,
};
