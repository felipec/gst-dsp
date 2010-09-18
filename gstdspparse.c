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
	      gint width,
	      gint height)
{
	GstDspVDec *vdec = GST_DSP_VDEC(base);
	/* update the framesize only if it hasn't been set yet. */
	if (vdec->width == 0 || vdec->height == 0) {
		GstCaps *out_caps;
		GstStructure *struc;

		out_caps = base->tmp_caps;
		struc = gst_caps_get_structure(out_caps, 0);
		gst_structure_set(struc,
				  "width", G_TYPE_INT, width,
				  "height", G_TYPE_INT, height, NULL);
	}

	if (vdec->color_format == GST_MAKE_FOURCC('U', 'Y', 'V', 'Y'))
		base->output_buffer_size = width * height * 2;
	else
		base->output_buffer_size = width * height * 3 / 2;
	vdec->width = width;
	vdec->height = height;
}

gboolean gst_dsp_h263_parse(GstDspBase *base, GstBuffer *buf)
{
	guint format;
	gint width, height;
	/* Used to multiplex from the format in the codec_data extra information */
	gint h263_formats[8][2] = {
		{ 0, 0 },
		{ 128, 96 },
		{ 176, 144 },
		{ 352, 288 },
		{ 704, 576 },
		{ 1408, 1152 },
	};

	/* The format is obtained from the 3 bits starting in offset 35 of the video header */
	format = (GST_BUFFER_DATA(buf)[4] & 0x1C) >> 2;

	pr_debug(base, "format=%u", format);

	if (format == 7 || format == 6)
		return FALSE;

	width = h263_formats[format][0];
	height = h263_formats[format][1];

	set_framesize(base, width, height);

	return TRUE;
}
