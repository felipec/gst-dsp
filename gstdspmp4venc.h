/*
 * Copyright (C) 2009-2010 Felipe Contreras
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

#ifndef GST_DSP_MP4VENC_H
#define GST_DSP_MP4VENC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_MP4VENC(obj) (GstDspMp4VEnc *)(obj)
#define GST_DSP_MP4VENC_TYPE (gst_dsp_mp4venc_get_type())
#define GST_DSP_MP4VENC_CLASS(obj) (GstDspMp4VEncClass *)(obj)

typedef struct GstDspMp4VEnc GstDspMp4VEnc;
typedef struct GstDspMp4VEncClass GstDspMp4VEncClass;

#include "gstdspvenc.h"

struct GstDspMp4VEnc {
	GstDspVEnc element;
};

struct GstDspMp4VEncClass {
	GstDspVEncClass parent_class;
};

GType gst_dsp_mp4venc_get_type(void);

G_END_DECLS

#endif /* GST_DSP_MP4VENC_H */
