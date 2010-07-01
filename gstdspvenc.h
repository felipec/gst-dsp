/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
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
	struct {
		gboolean codec_data_done;
	} mpeg4;
};

struct gstdsp_codec_level {
	gint id;
	gint mbps; /* macroblocks per second */
	gint bitrate;
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
	guint32 color_format;
	gint max_bitrate;
	gint bitrate;
	gint framerate;
	gint quality;
	struct gstdsp_codec_level *supported_levels;
	guint nr_supported_levels;
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
