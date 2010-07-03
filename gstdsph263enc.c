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

#include "gstdsph263enc.h"
#include "plugin.h"

#include "util.h"
#include "dsp_bridge.h"

#include "log.h"

#define GST_CAT_DEFAULT gstdsp_debug

/*
 * H.263 supported levels
 * source: http://www.itu.int/rec/T-REC-H.263/  , page 208- table of levels.
 */

static struct gstdsp_codec_level levels[] = {
	{10,  1485,    64000 },        /* Level 10 - QCIF@15fps */
	{20,  5940,   128000 },        /* Level 20 - CIF@15fps */
	{30, 11880,   384000 },        /* Level 30 - CIF@30fps */
	{40, 11880,  2048000 },        /* Level 40 - CIF@30fps */
	{45,  1485,   128000 },        /* Level 45 - QCIF@15fps */
	{50, 19800,  4096000 },        /* Level 50 - CIF@50fps */
	{60, 40500,  8192000 },        /* Level 60 - 720x288@50fps */
	{70, 81000, 16384000 },        /* Level 70 - 4CIF@50fps */
};

static inline GstCaps *
generate_src_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();

	struc = gst_structure_new("video/x-h263",
				  "variant", G_TYPE_STRING, "itu",
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
	base->alg = GSTDSP_H263ENC;

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
					     "DSP video encoder",
					     "Codec/Encoder/Video",
					     "Encodes H.263 video with TI's DSP algorithms",
					     "Felipe Contreras");

	template = gst_pad_template_new("src", GST_PAD_SRC,
					GST_PAD_ALWAYS,
					generate_src_template());

	gst_element_class_add_pad_template(element_class, template);
	gst_object_unref(template);
}

GType
gst_dsp_h263enc_get_type(void)
{
	static GType type;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo type_info = {
			.class_size = sizeof(GstDspH263EncClass),
			.base_init = base_init,
			.instance_size = sizeof(GstDspH263Enc),
			.instance_init = instance_init,
		};

		type = g_type_register_static(GST_DSP_VENC_TYPE, "GstDspH263Enc", &type_info, 0);
	}

	return type;
}
