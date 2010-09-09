/*
 * Copyright (C) 2007-2010 Nokia Corporation
 * Copyright (C) 2009 Marco Ballesio
 *
 * Authors:
 * Marco Ballesio <marco.ballesio@gmail.com>
 * Felipe Contreras <felipe.contreras@nokia.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include <gst/gst.h>
#include "gstdspbase.h"
#include "gstdspvdec.h"
#include "gstdspparse.h"

static inline void
set_framesize(GstDspBase *base,
		int width, int height,
		int par_num, int par_den)
{
	GstDspVDec *vdec = GST_DSP_VDEC(base);

	/* update the framesize only if it hasn't been set yet. */
	if (vdec->crop_width == 0 || vdec->crop_height == 0) {
		GstCaps *out_caps;
		GstStructure *struc;

		out_caps = base->tmp_caps;
		struc = gst_caps_get_structure(out_caps, 0);
		gst_structure_set(struc,
				"width", G_TYPE_INT, width,
				"height", G_TYPE_INT, height, NULL);
		if (par_num && par_den)
			gst_structure_set(struc,
					"pixel-aspect-ratio", GST_TYPE_FRACTION,
					par_num, par_den, NULL);
		vdec->crop_width = width;
		vdec->crop_height = height;
	}

	if (vdec->color_format == GST_MAKE_FOURCC('U', 'Y', 'V', 'Y'))
		base->output_buffer_size = width * height * 2;
	else
		base->output_buffer_size = width * height * 3 / 2;
	vdec->width = width;
	vdec->height = height;
}

bool gst_dsp_h263_parse(GstDspBase *base, GstBuffer *buf)
{
	struct get_bit_context s;
	unsigned bits;
	unsigned type;
	bool baseline = true;
	int width, height;
	int par_num = 11, par_den = 12;
	struct size {
		int width;
		int height;
	} sizes[] = {
		{ 0, 0 },
		{ 128, 96 },
		{ 176, 144 },
		{ 352, 288 },
		{ 704, 576 },
		{ 1408, 1152 },
	};
	struct par {
		int num;
		int den;
	} pars[] = {
		{ 0, 0 },
		{ 1, 1 },
		{ 12, 11 },
		{ 10, 11 },
		{ 16, 11 },
		{ 40, 33 },
	};

	init_get_bits(&s, buf->data, buf->size * 8);

	if (get_bits_left(&s) < 38)
		goto not_enough;

	/* picture start code */
	if (get_bits(&s, 22) != 0x20)
		goto bail;

	/* temporal reference */
	skip_bits(&s, 8);

	type = get_bits(&s, 8) & 0x7;
	switch (type) {
	case 0:
	case 6:
		/* forbidden or reserved */
		goto bail;
	case 7: {
		unsigned custom_pf;
		bool extended_par = false;

		/* extended */
		baseline = false;

		if (get_bits_left(&s) < 54)
			goto not_enough;

		/* Updated Full Extended PTYPE */

		if (get_bits(&s, 3) != 1) {
			/* spec wise, should be present at start */
			goto bail;
		}

		type = get_bits(&s, 18) >> 15;
		if (type == 0 || type == 7) {
			goto bail;
		} else if (type != 6) {
			pr_debug(base, "format=%d", type);
			width = sizes[type].width;
			height = sizes[type].height;
			/* have all we need, exit */
			goto exit;
		}

		/* mandatory PLUSPTYPE part */

		skip_bits(&s, 9);

		/* CPM */
		if (get_bits1(&s)) {
			if (get_bits_left(&s) < 25)
				goto not_enough;

			/* PSBI */
			skip_bits(&s, 2);
		}

		custom_pf = get_bits(&s, 4);
		if (custom_pf == 0x0F) {
			extended_par = true;
		} else if (custom_pf && custom_pf < 6) {
			par_num = pars[custom_pf].num;
			par_den = pars[custom_pf].den;
		}

		bits = get_bits(&s, 19);
		height = (bits & 0x1FF) * 4;
		bits >>= 10;
		width = ((bits & 0x1FF) + 1) * 4;
		if (!extended_par)
			goto exit;

		if (get_bits_left(&s) < 16)
			goto not_enough;

		bits = get_bits(&s, 16);
		if (bits) {
			par_num = bits >> 8;
			par_den = bits & 0x0F;
		}
		break;
	}
	default:
		/* regular */

		pr_debug(base, "format=%d", type);
		width = sizes[type].width;
		height = sizes[type].height;

		if (get_bits_left(&s) < 11)
			goto not_enough;

		/* optional PTYPE part */
		baseline = !(get_bits(&s, 5) & 1);

		/* PQUANT */
		skip_bits(&s, 5);

		/* CPM */
		baseline = baseline && !get_bits1(&s);
		break;
	}

exit:
	/* TODO use to decide on node */
	pr_debug(base, "baseline=%u", baseline);
	pr_debug(base, "width=%u, height=%u, par=%d:%d", width, height,
			par_num, par_den);

	set_framesize(base, width, height, par_num, par_den);
	return true;

not_enough:
	pr_err(base, "not enough data");
bail:
	return false;
}
