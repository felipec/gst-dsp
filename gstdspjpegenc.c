/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "gstdspjpegenc.h"
#include "plugin.h"

#include "util.h"
#include "dsp_bridge.h"

#include "log.h"

#define GST_CAT_DEFAULT gstdsp_debug

static inline GstCaps *
generate_src_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();

	struc = gst_structure_new("image/jpeg",
				  NULL);

	gst_caps_append_structure(caps, struc);

	return caps;
}

static void
instance_init(GTypeInstance *instance,
	      gpointer g_class)
{
	GstDspBase *base = GST_DSP_BASE(instance);
	base->alg = GSTDSP_JPEGENC;
}

static void
base_init(gpointer g_class)
{
	GstElementClass *element_class;
	GstPadTemplate *template;
	GstCaps *caps;

	element_class = GST_ELEMENT_CLASS(g_class);

	gst_element_class_set_details_simple(element_class,
					     "DSP video encoder",
					     "Codec/Encoder/Image",
					     "Encodes JPEG images with TI's DSP algorithms",
					     "Felipe Contreras");

	template = gst_pad_template_new("src", GST_PAD_SRC,
					GST_PAD_ALWAYS,
					generate_src_template());

	gst_element_class_add_pad_template(element_class, template);
	gst_object_unref(template);

	/* publicly announce specific w/h restrictions */
	template = gst_element_class_get_pad_template(element_class, "sink");
	caps = gst_pad_template_get_caps(template);
	gst_caps_set_simple(caps,
			    "width", GST_TYPE_INT_RANGE, 16, JPEGENC_MAX_WIDTH,
			    "height", GST_TYPE_INT_RANGE, 16, JPEGENC_MAX_HEIGHT,
			    NULL);
}

GType
gst_dsp_jpegenc_get_type(void)
{
	static GType type;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo type_info = {
			.class_size = sizeof(GstDspJpegEncClass),
			.base_init = base_init,
			.instance_size = sizeof(GstDspJpegEnc),
			.instance_init = instance_init,
		};

		type = g_type_register_static(GST_DSP_VENC_TYPE, "GstDspJpegEnc", &type_info, 0);
	}

	return type;
}
