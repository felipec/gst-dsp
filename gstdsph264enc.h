/*
 * Copyright (C) 2009-2010 Felipe Contreras
 * Copyright (C) 2009-2010 Nokia Corporation
 *
 * Authors:
 * Juha Alanen <juha.m.alanen@nokia.com>
 * Felipe Contreras <felipe.contreras@nokia.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
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
