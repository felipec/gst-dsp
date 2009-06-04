/*
 * Copyright (C) 2009 Marco Ballesio
 * Copyright (C) 2007-2009 Nokia Corporation
 *
 * Authors:
 * Marco Ballesio <marco.ballesio@gmail.com>
 * Felipe Contreras <felipe.contreras@nokia.com>
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

#include <gst/gst.h>
#include "gstdspbase.h"
#include "gstdspvdec.h"

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

	base->output_buffer_size = width * height * 2;
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
