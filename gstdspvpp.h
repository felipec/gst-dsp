/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef GST_DSP_VPP_H
#define GST_DSP_VPP_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_VPP(obj) (GstDspVpp *)(obj)
#define GST_DSP_VPP_TYPE (gst_dsp_vpp_get_type())
#define GST_DSP_VPP_CLASS(obj) (GstDspVppClass *)(obj)

typedef struct GstDspVpp GstDspVpp;
typedef struct GstDspVppClass GstDspVppClass;

#include "gstdspbase.h"

struct GstDspVpp {
	GstDspBase element;
	int width, height;
	int out_width, out_height;
};

struct GstDspVppClass {
	GstDspBaseClass parent_class;
};

GType gst_dsp_vpp_get_type(void);

G_END_DECLS

#endif /* GST_DSP_VPP_H */
