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
#include "gstdspvpp.h"

#include <stdio.h> /* for snprintf */

struct create_args {
	uint32_t size;
	uint16_t num_streams;

	uint16_t in_id;
	uint16_t in_type;
	uint16_t in_count;

	uint16_t ov_id;
	uint16_t ov_type;
	uint16_t ov_count;

	uint16_t rgb_id;
	uint16_t rgb_type;
	uint16_t rgb_count;

	uint16_t yuv_id;
	uint16_t yuv_type;
	uint16_t yuv_count;

	uint16_t alpha_id;
	uint16_t alpha_type;
	uint16_t alpha_count;

	char value[52];
};

static void create_args(GstDspBase *base, unsigned *profile_id, void **arg_data)
{
	GstDspVpp *self = GST_DSP_VPP(base);
	struct create_args args = {
		.size = sizeof(args) - 4,
		.num_streams = 5,
		.in_id = 0,
		.in_type = 0,
		.in_count = base->ports[0]->num_buffers,
		.ov_id = 1,
		.ov_type = 0,
		.ov_count = 1,
		.rgb_id = 2,
		.rgb_type = 0,
		.rgb_count = 1,
		.yuv_id = 3,
		.yuv_type = 0,
		.yuv_count = base->ports[1]->num_buffers,
		.alpha_id = 4,
		.alpha_type = 0,
		.alpha_count = 1,
	};

	snprintf(args.value, sizeof(args.value),
			":%i:%i:0:1:1:0:0\n", self->width, self->out_width);

	*profile_id = 0;

	*arg_data = malloc(sizeof(args));
	memcpy(*arg_data, &args, sizeof(args));
}

struct in_params {
	uint32_t width;
	uint32_t height;
	uint32_t offset;

	/* crop */
	uint32_t x_start;
	uint32_t x_size;
	uint32_t y_start;
	uint32_t y_size;

	/* zoom */
	uint32_t zoom_factor;
	uint32_t zoom_limit;
	uint32_t zoom_speed;

	/* stabilisation */
	uint32_t x_offset;
	uint32_t y_offset;

	/* gain and contrast */
	uint32_t contrast;
	uint32_t gain;

	/* effect */
	uint32_t frosted_glass;
	uint32_t light_chroma;
	uint32_t locked_ratio;
	uint32_t mirror;
	uint32_t rgb_rotation;
	uint32_t yuv_rotation;

	uint32_t io_range;
	uint32_t ditheright;
	uint32_t pitch;
	uint32_t alpha;
};

struct out_params {
	uint32_t width;
	uint32_t height;
	uint32_t offset;
};

static void setup_in_params(GstDspBase *base, dmm_buffer_t *tmp)
{
	struct in_params *in_param;
	GstDspVpp *self = GST_DSP_VPP(base);

	in_param = tmp->data;
	in_param->width = self->width;
	in_param->height = self->height;

	in_param->zoom_factor = 1 << 10;
	in_param->zoom_limit = 1 << 10;

	in_param->gain = 1 << 6;
	in_param->light_chroma = 1;
}

static void setup_out_params(GstDspBase *base, dmm_buffer_t *tmp)
{
	struct out_params *out_param;
	GstDspVpp *self = GST_DSP_VPP(base);

	out_param = tmp->data;
	out_param->width = self->out_width;
	out_param->height = self->out_height;
	out_param->offset = self->out_width * self->out_height;
}

static void setup_params(GstDspBase *base)
{
	struct in_params *in_param;
	struct out_params *out_param;
	du_port_t *p;

	p = base->ports[0];
	gstdsp_port_setup_params(base, p, sizeof(*in_param), setup_in_params);

	p = base->ports[1];
	gstdsp_port_setup_params(base, p, sizeof(*out_param), setup_out_params);
}

struct td_codec td_vpp_codec = {
	.uuid = &(const struct dsp_uuid) { 0xfbb1c6fc, 0x8d9d, 0x4ac3, 0x80, 0x3f,
		{ 0x99, 0x7b, 0xd6, 0x17, 0xd8, 0xb7 } },
	.filename = "vpp_sn.dll64P",
	.setup_params = setup_params,
	.create_args = create_args,
};
