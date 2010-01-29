/*
 * Copyright (C) 2009 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
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

#ifndef GST_DSP_JPEGENC_H
#define GST_DSP_JPEGENC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_JPEGENC(obj) (GstDspJpegEnc *)(obj)
#define GST_DSP_JPEGENC_TYPE (gst_dsp_jpegenc_get_type())
#define GST_DSP_JPEGENC_CLASS(obj) (GstDspJpegEncClass *)(obj)

typedef struct GstDspJpegEnc GstDspJpegEnc;
typedef struct GstDspJpegEncClass GstDspJpegEncClass;

#include "gstdspvenc.h"

#define JPEGENC_MAX_WIDTH 2592
#define JPEGENC_MAX_HEIGHT 1968

struct GstDspJpegEnc {
	GstDspVEnc element;
};

struct GstDspJpegEncClass {
	GstDspVEncClass parent_class;
};

GType gst_dsp_jpegenc_get_type(void);

G_END_DECLS

#endif /* GST_DSP_JPEGENC_H */
