/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef GST_DSP_VDEC_H
#define GST_DSP_VDEC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_VDEC(obj) (GstDspVDec *)(obj)
#define GST_DSP_VDEC_TYPE (gst_dsp_vdec_get_type())
#define GST_DSP_VDEC_CLASS(obj) (GstDspVDecClass *)(obj)

typedef struct GstDspVDec GstDspVDec;
typedef struct GstDspVDecClass GstDspVDecClass;

#include "gstdspbase.h"

union vdec_priv_data {
	struct {
		gint lol;
	} h264;
};

struct GstDspVDec {
	GstDspBase element;
	gint width, height;
	gint frame_index;
	gboolean wmv_is_vc1;
	gboolean jpeg_is_interlaced;
	GstBuffer *codec_data;
	gboolean codec_data_sent;
	guint32 color_format;

	union vdec_priv_data priv;
};

struct GstDspVDecClass {
	GstDspBaseClass parent_class;
};

GType gst_dsp_vdec_get_type(void);

G_END_DECLS

#endif /* GST_DSP_VDEC_H */
