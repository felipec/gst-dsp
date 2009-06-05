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
	GstElementDetails details;

	element_class = GST_ELEMENT_CLASS(g_class);

	details.longname = "DSP video encoder";
	details.klass = "Codec/Encoder/Image";
	details.description = "Encodes JPEG images with TI's DSP algorithms";
	details.author = "Felipe Contreras";

	gst_element_class_set_details(element_class, &details);

	template = gst_pad_template_new("src", GST_PAD_SRC,
					GST_PAD_ALWAYS,
					generate_src_template());

	gst_element_class_add_pad_template(element_class, template);
}

GType
gst_dsp_jpegenc_get_type(void)
{
	static GType type = 0;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo *type_info;

		type_info = g_new0(GTypeInfo, 1);
		type_info->class_size = sizeof(GstDspJpegEncClass);
		type_info->base_init = base_init;
		type_info->instance_size = sizeof(GstDspJpegEnc);
		type_info->instance_init = instance_init;

		type = g_type_register_static(GST_DSP_VENC_TYPE, "GstDspJpegEnc", type_info, 0);
		g_free(type_info);
	}

	return type;
}
