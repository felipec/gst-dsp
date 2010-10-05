/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "gstdspmp4venc.h"
#include "plugin.h"

#include "util.h"
#include "dsp_bridge.h"

#include "log.h"

#define GST_CAT_DEFAULT gstdsp_debug

/*
 * MPEG4-SP supported levels
 * source: ISO/IEC 14496-2:2004/Cor 3:2008
 * Level 5 is not the one defined by the standard, this is the default for
 * this particular encoder.
 */

static struct gstdsp_codec_level levels[] = {
	{0,   99,  1485,   64000 },        /* Level 0  - QCIF@15fps */
	{0,   99,  1485,  128000 },        /* Level 0b - CIF@15fps  */
	{1,   99,  1485,   64000 },        /* Level 1  - CIF@30fps  */
	{2,  396,  5940,  128000 },        /* Level 2  - CIF@30fps  */
	{3,  396, 11880,  384000 },        /* Level 3  - QCIF@15fps */
	{4, 1200, 36000, 4000000 },        /* Level 4a - VGA@30fps  */
	{5, 1620, 47700, 5000000 },        /* Level 5  - WVGA@30fps */
};

static inline GstCaps *
generate_src_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();

	struc = gst_structure_new("video/mpeg",
				  "mpegversion", G_TYPE_INT, 4,
				  "systemstream", G_TYPE_BOOLEAN, FALSE,
				  NULL);

	gst_caps_append_structure(caps, struc);

	return caps;
}

static void
instance_init(GTypeInstance *instance,
	      gpointer g_class)
{
	GstDspBase *base = GST_DSP_BASE(instance);
	GstDspVEnc *self = GST_DSP_VENC(instance);

	base->alg = GSTDSP_MP4VENC;
	self->supported_levels = levels;
	self->nr_supported_levels = ARRAY_SIZE(levels);
}

static void
base_init(gpointer g_class)
{
	GstElementClass *element_class;
	GstPadTemplate *template;

	element_class = GST_ELEMENT_CLASS(g_class);

	gst_element_class_set_details_simple(element_class,
					     "DSP MPEG-4 video encoder",
					     "Codec/Encoder/Video",
					     "Encodes MPEG-4 video with TI's DSP algorithms",
					     "Felipe Contreras");

	template = gst_pad_template_new("src", GST_PAD_SRC,
					GST_PAD_ALWAYS,
					generate_src_template());

	gst_element_class_add_pad_template(element_class, template);
	gst_object_unref(template);
}

GType
gst_dsp_mp4venc_get_type(void)
{
	static GType type;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo type_info = {
			.class_size = sizeof(GstDspMp4VEncClass),
			.base_init = base_init,
			.instance_size = sizeof(GstDspMp4VEnc),
			.instance_init = instance_init,
		};

		type = g_type_register_static(GST_DSP_VENC_TYPE, "GstDspMp4VEnc", &type_info, 0);
	}

	return type;
}
