/*
 * Copyright (C) 2009 Marco Ballesio
 *
 * Authors:
 * Marco Ballesio <marco.ballesio@gmail.com>
 * Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef GST_DSP_PARSE_H
#define GST_DSP_PARSE_H

bool gst_dsp_h263_parse(GstDspBase *base, GstBuffer *buf);
bool gst_dsp_mpeg4_parse(GstDspBase *base, GstBuffer *buf);

#endif
