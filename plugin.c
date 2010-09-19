/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "plugin.h"

#include "gstdspdummy.h"
#include "gstdspvdec.h"
#include "gstdsph263enc.h"
#include "gstdspmp4venc.h"
#include "gstdspjpegenc.h"
#include "gstdsph264enc.h"

GstDebugCategory *gstdsp_debug;

static gboolean
plugin_init(GstPlugin *plugin)
{
#ifndef GST_DISABLE_GST_DEBUG
	gstdsp_debug = _gst_debug_category_new("dsp", 0, "DSP stuff");
#endif

	if (!gst_element_register(plugin, "dspdummy", GST_RANK_NONE, GST_DSP_DUMMY_TYPE))
		return FALSE;

	if (!gst_element_register(plugin, "dspvdec", GST_RANK_PRIMARY, GST_DSP_VDEC_TYPE))
		return FALSE;

	if (!gst_element_register(plugin, "dsph263enc", GST_RANK_PRIMARY, GST_DSP_H263ENC_TYPE))
		return FALSE;

	if (!gst_element_register(plugin, "dspmp4venc", GST_RANK_PRIMARY, GST_DSP_MP4VENC_TYPE))
		return FALSE;

	if (!gst_element_register(plugin, "dspjpegenc", GST_RANK_PRIMARY, GST_DSP_JPEGENC_TYPE))
		return FALSE;

	if (!gst_element_register(plugin, "dsph264enc", GST_RANK_PRIMARY, GST_DSP_H264ENC_TYPE))
		return FALSE;

	return TRUE;
}

GstPluginDesc gst_plugin_desc = {
	.major_version = 0,
	.minor_version = 10,
	.name = "dsp",
	.description = (gchar *) "Texas Instruments DSP elements",
	.plugin_init = plugin_init,
	.version = VERSION,
	.license = "LGPL",
	.source = "none",
	.package = "none",
	.origin = "none",
};
