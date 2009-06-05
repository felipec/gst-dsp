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

#ifndef GST_DSP_H263ENC_H
#define GST_DSP_H263ENC_H

#include <gst/gst.h>

G_BEGIN_DECLS

#define GST_DSP_H263ENC(obj) (GstDspH263Enc *)(obj)
#define GST_DSP_H263ENC_TYPE (gst_dsp_h263enc_get_type())
#define GST_DSP_H263ENC_CLASS(obj) (GstDspH263EncClass *)(obj)

typedef struct GstDspH263Enc GstDspH263Enc;
typedef struct GstDspH263EncClass GstDspH263EncClass;

#include "gstdspvenc.h"

struct GstDspH263Enc
{
	GstDspVEnc element;
};

struct GstDspH263EncClass
{
	GstDspVEncClass parent_class;
};

GType gst_dsp_h263enc_get_type(void);

G_END_DECLS

#endif /* GST_DSP_H263ENC_H */
