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
#include "gstdspadec.h"

#include "util.h"

struct create_args {
	uint32_t size;
	uint16_t num_streams;

	uint16_t in_id;
	uint16_t in_type;
	uint16_t in_count;

	uint16_t out_id;
	uint16_t out_type;
	uint16_t out_count;

	uint16_t out_is_24bps;
	uint16_t in_is_framed;
};

static void create_args(GstDspBase *base, unsigned *profile_id, void **arg_data)
{
	GstDspADec *self = GST_DSP_ADEC(base);

	struct create_args args = {
		.size = 9 * sizeof(uint16_t), /* sizeof(args)-4 => alignment issue */
		.num_streams = 2,
		.in_id = 0,
		.in_type = 0,
		.in_count = base->ports[0]->num_buffers,
		.out_id = 1,
		.in_type = 0,
		.out_count = base->ports[1]->num_buffers,
		.out_is_24bps = 0,
		.in_is_framed = self->packetised ? 1 : 0,
	};

	*profile_id = -1;

	*arg_data = malloc(sizeof(args));
	memcpy(*arg_data, &args, sizeof(args));
}

struct dyn_params {
	uint32_t size;
	uint32_t out_format;   /* 1 = interleaved / 0 = block */
	uint32_t enable_ds;    /* do enable down sampler? */
	uint32_t enable_ps;    /* do enable parametric stereo? */
	uint32_t samplerate;  /* sample rate index */
	uint32_t is_raw;       /* is raw format? */
	uint32_t is_dual_mono; /* is dual mono? */
};

static inline int
get_sample_rate_index(int rate)
{
	int i;
	int rates[] = { 96000, 88200, 64000, 48000, 44100, 32000,
			24000, 22050, 16000, 12000, 11025, 8000 };

	for (i = 0; i < ARRAY_SIZE(rates); i++)
		if (rate == rates[i])
			return i;

	return 0; /* 96000 by default */
}

static void send_params(GstDspBase *base, struct dsp_node *node)
{
	struct dyn_params *params;
	dmm_buffer_t *b;
	GstDspADec *self = GST_DSP_ADEC(base);

	b = dmm_buffer_calloc(base->dsp_handle, base->proc,
			sizeof(*params), DMA_TO_DEVICE);

	params = b->data;
	params->size = sizeof(*params);
	params->out_format    = 1; /* Interleaved */
	params->enable_ds     = 1; /* Always on to avoid upsample issues */
	params->enable_ps     = self->parametric_stereo ? 1 : 0;
	params->samplerate    = get_sample_rate_index(self->samplerate);
	params->is_raw        = self->raw ? 1 : 0;
	params->is_dual_mono  = 0; /* ??? */

	gstdsp_send_alg_ctrl(base, node, b);
}

struct in_params {
	uint16_t is_last;
	uint16_t is_conceal;
	uint32_t index;
};

struct out_params {
	uint32_t count;
	uint32_t is_last;
	uint32_t index;
};

static void setup_in_params(GstDspBase *base, dmm_buffer_t *tmp)
{
	struct in_params *in_param;

	in_param = tmp->data;
	in_param->is_last = 0;
	in_param->is_conceal = 0;
	in_param->index = 0;
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

static inline void update_caps(GstDspBase *base, struct dsp_node *node)
{
	GstDspADec *self = GST_DSP_ADEC(base);
	GstCaps *caps;

	send_params(base, base->node);

	pr_info(self, "new sample rate %u", self->samplerate);
	caps = gst_pad_get_negotiated_caps(base->srcpad);
	caps = gst_caps_make_writable(caps);
	gst_caps_set_simple(caps, "rate", G_TYPE_INT, self->samplerate, NULL);
	gst_pad_take_caps(base->srcpad, caps);
}

static void update_params(GstDspBase *base, struct dsp_node *node, uint32_t msg)
{
	GstDspADec *self = GST_DSP_ADEC(base);

	if (msg == 0x0601 && self->parametric_stereo) { /* SBR */
		self->parametric_stereo = FALSE;
		self->samplerate /= 2;
		update_caps(base, node);
	} else if (msg == 0x0602 && !self->parametric_stereo) { /* PS */
		self->parametric_stereo = TRUE;
		self->samplerate *= 2;
		update_caps(base, node);
	}
}

struct td_codec td_aacdec_codec = {
	.uuid = &(const struct dsp_uuid) { 0x5c89a1f1, 0x3d83, 0x11d6, 0xb0, 0xd7,
		{ 0x00, 0xc0, 0x4f, 0x1f, 0xc0, 0x36 } },
	.filename = "mpeg4aacdec_sn.dll64P",
	.setup_params = setup_params,
	.create_args = create_args,
	.send_params = send_params,
	.update_params = update_params,
};
