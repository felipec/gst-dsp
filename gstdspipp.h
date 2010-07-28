/*
 * Copyright (C) 2010 Nokia Corporation
 *
 * Authors:
 * Elamparithi Shanmugam <parithi@ti.com>
 * Felipe Contreras <felipe.contreras@nokia.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef GST_DSP_IPP_H
#define GST_DSP_IPP_H

#include "gstdspbase.h"

G_BEGIN_DECLS

#define GST_DSP_IPP(obj) (GstDspIpp *)(obj)
#define GST_DSP_IPP_TYPE (gst_dsp_ipp_get_type())
#define GST_DSP_IPP_CLASS(obj) (GstDspIppClass *)(obj)

typedef struct GstDspIpp GstDspIpp;
typedef struct GstDspIppClass GstDspIppClass;

struct GstDspIpp {
	GstDspBase element;
};

struct GstDspIppClass {
	GstDspBaseClass parent_class;
};

GType gst_dsp_ipp_get_type(void);

G_END_DECLS

#endif /* GST_DSP_IPP_H */
