/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef GST_DSP_MP4VENC_H
#define GST_DSP_MP4VENC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_MP4VENC(obj) (GstDspMp4VEnc *)(obj)
#define GST_DSP_MP4VENC_TYPE (gst_dsp_mp4venc_get_type())
#define GST_DSP_MP4VENC_CLASS(obj) (GstDspMp4VEncClass *)(obj)

typedef struct _GstDspMp4VEnc GstDspMp4VEnc;
typedef struct _GstDspMp4VEncClass GstDspMp4VEncClass;

#include "gstdspvenc.h"

struct _GstDspMp4VEnc {
	GstDspVEnc element;
};

struct _GstDspMp4VEncClass {
	GstDspVEncClass parent_class;
};

GType gst_dsp_mp4venc_get_type(void);

G_END_DECLS

#endif /* GST_DSP_MP4VENC_H */
