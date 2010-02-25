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

#ifndef GST_DSP_VENC_H
#define GST_DSP_VENC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_VENC(obj) (GstDspVEnc *)(obj)
#define GST_DSP_VENC_TYPE (gst_dsp_venc_get_type())
#define GST_DSP_VENC_CLASS(obj) (GstDspVEncClass *)(obj)

typedef struct GstDspVEnc GstDspVEnc;
typedef struct GstDspVEncClass GstDspVEncClass;

#include "gstdspbase.h"

union venc_priv_data {
	struct {
		gboolean bytestream;
		gboolean sps_received, pps_received, codec_data_done;
		GstBuffer *sps, *pps, *codec_data;
	} h264;
};

enum {
	GSTDSP_JPEGENC,
	GSTDSP_H263ENC,
	GSTDSP_MP4VENC,
	GSTDSP_H264ENC,
};

struct GstDspVEnc {
	GstDspBase element;
	gint width, height;
	gint bitrate;
	gint framerate;
	gint quality;
	union venc_priv_data priv;
	gint frame_index;
	GstEvent *keyframe_event;
	GMutex *keyframe_mutex;
	gint mode;
	gint keyframe_interval;
};

struct GstDspVEncClass {
	GstDspBaseClass parent_class;
};

GType gst_dsp_venc_get_type(void);

G_END_DECLS

#endif /* GST_DSP_VENC_H */
