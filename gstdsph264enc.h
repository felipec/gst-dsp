/*
 * Copyright (C) 2009-2010 Felipe Contreras
 * Copyright (C) 2009-2010 Nokia Corporation
 *
 * Authors:
 * Juha Alanen <juha.m.alanen@nokia.com>
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

#ifndef GST_DSP_H264ENC_H
#define GST_DSP_H264ENC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_H264ENC(obj) (GstDspH264Enc *)(obj)
#define GST_DSP_H264ENC_TYPE (gst_dsp_h264enc_get_type())
#define GST_DSP_H264ENC_CLASS(obj) (GstDspH264EncClass *)(obj)

typedef struct GstDspH264Enc GstDspH264Enc;
typedef struct GstDspH264EncClass GstDspH264EncClass;

#include "gstdspvenc.h"

struct GstDspH264Enc {
	GstDspVEnc element;
};

struct GstDspH264EncClass {
	GstDspVEncClass parent_class;
};

GType gst_dsp_h264enc_get_type(void);

G_END_DECLS

#endif /* GST_DSP_H264ENC_H */
