/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef GST_DSP_H263ENC_H
#define GST_DSP_H263ENC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_H263ENC(obj) (GstDspH263Enc *)(obj)
#define GST_DSP_H263ENC_TYPE (gst_dsp_h263enc_get_type())
#define GST_DSP_H263ENC_CLASS(obj) (GstDspH263EncClass *)(obj)

typedef struct _GstDspH263Enc GstDspH263Enc;
typedef struct _GstDspH263EncClass GstDspH263EncClass;

#include "gstdspvenc.h"

struct _GstDspH263Enc {
	GstDspVEnc element;
};

struct _GstDspH263EncClass {
	GstDspVEncClass parent_class;
};

GType gst_dsp_h263enc_get_type(void);

G_END_DECLS

#endif /* GST_DSP_H263ENC_H */
