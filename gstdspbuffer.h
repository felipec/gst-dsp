/*
 * Copyright (C) 2011 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef GST_DSP_BUFFER_H
#define GST_DSP_BUFFER_H

#include <gst/gst.h>

#include "gstdspbase.h"

GType gst_dsp_buffer_get_type(void);

GstBuffer *gst_dsp_buffer_new(GstDspBase *base, struct td_buffer *tb);

#endif /* GST_DSP_BASE_H */
