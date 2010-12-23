/*
 * Copyright (C) 2010 Víctor M. Jáquez Leal
 *
 * Author: Víctor M. Jáquez Leal <vjaquez@igalia.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef GST_DSP_ADEC_H
#define GST_DSP_ADEC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_ADEC(obj) (GstDspADec *)(obj)
#define GST_DSP_ADEC_TYPE (gst_dsp_adec_get_type())
#define GST_DSP_ADEC_CLASS(obj) (GstDspADecClass *)(obj)

typedef struct GstDspADec GstDspADec;
typedef struct GstDspADecClass GstDspADecClass;

#include "gstdspbase.h"

enum {
	GSTDSP_AACDEC,
};

struct GstDspADec {
	GstDspBase element;
	guint samplerate;
	gboolean parametric_stereo;
	gboolean packetised;
	gboolean raw;
};

struct GstDspADecClass {
	GstDspBaseClass parent_class;
};

GType gst_dsp_adec_get_type(void);

G_END_DECLS

#endif /* GST_DSP_ADEC_H */
