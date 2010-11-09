/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef GST_DSP_JPEGENC_H
#define GST_DSP_JPEGENC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_JPEGENC(obj) (GstDspJpegEnc *)(obj)
#define GST_DSP_JPEGENC_TYPE (gst_dsp_jpegenc_get_type())
#define GST_DSP_JPEGENC_CLASS(obj) (GstDspJpegEncClass *)(obj)

typedef struct _GstDspJpegEnc GstDspJpegEnc;
typedef struct _GstDspJpegEncClass GstDspJpegEncClass;

#include "gstdspvenc.h"

#define JPEGENC_MAX_WIDTH 4096
#define JPEGENC_MAX_HEIGHT 4096

struct _GstDspJpegEnc {
	GstDspVEnc element;
};

struct _GstDspJpegEncClass {
	GstDspVEncClass parent_class;
};

GType gst_dsp_jpegenc_get_type(void);

G_END_DECLS

#endif /* GST_DSP_JPEGENC_H */
