/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Authors:
 * Felipe Contreras <felipe.contreras@gmail.com>
 * Juha Alanen <juha.m.alanen@nokia.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "gstdspvenc.h"
#include "plugin.h"

#include "util.h"
#include "dsp_bridge.h"

#include <string.h> /* for memcpy */

#include "log.h"

#define GST_CAT_DEFAULT gstdsp_debug

static GstDspBaseClass *parent_class;

enum {
	ARG_0,
	ARG_BITRATE,
	ARG_MODE,
	ARG_KEYFRAME_INTERVAL,
	ARG_MAX_BITRATE,
	ARG_INTRA_REFRESH,
};

#define DEFAULT_BITRATE 0
#define DEFAULT_MAX_BITRATE 0
#define DEFAULT_MODE 0
#define DEFAULT_KEYFRAME_INTERVAL 1
#define DEFAULT_INTRA_REFRESH (DEFAULT_MODE == 1)

#define GST_TYPE_DSPVENC_MODE gst_dspvenc_mode_get_type()
static GType
gst_dspvenc_mode_get_type(void)
{
	static GType gst_dspvenc_mode_type;

	if (!gst_dspvenc_mode_type) {
		static GEnumValue modes[] = {
			{0, "Storage", "storage"},
			{1, "Streaming", "streaming"},
			{0, NULL, NULL},
		};

		gst_dspvenc_mode_type = g_enum_register_static("GstDspVEncMode", modes);
	}

	return gst_dspvenc_mode_type;
}

static inline GstCaps *
generate_sink_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();

	struc = gst_structure_new("video/x-raw-yuv",
				  "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC('U', 'Y', 'V', 'Y'),
				  NULL);

	gst_caps_append_structure(caps, struc);

	struc = gst_structure_new("video/x-raw-yuv",
				  "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC('I', '4', '2', '0'),
				  NULL);

	gst_caps_append_structure(caps, struc);

	return caps;
}

static inline void *
create_node(GstDspVEnc *self)
{
	GstDspBase *base;
	int dsp_handle;
	struct dsp_node *node;
	struct td_codec *codec;

	const struct dsp_uuid usn_uuid = { 0x79A3C8B3, 0x95F2, 0x403F, 0x9A, 0x4B,
		{ 0xCF, 0x80, 0x57, 0x73, 0x05, 0x41 } };

	const struct dsp_uuid conversions_uuid = { 0x722DD0DA, 0xF532, 0x4238, 0xB8, 0x46,
		{ 0xAB, 0xFF, 0x5D, 0xA4, 0xBA, 0x02 } };

	base = GST_DSP_BASE(self);
	dsp_handle = base->dsp_handle;

	if (!gstdsp_register(dsp_handle, &usn_uuid, DSP_DCD_LIBRARYTYPE, "usn.dll64P")) {
		pr_err(self, "failed to register usn node library");
		return NULL;
	}

	codec = base->codec;
	if (!codec) {
		pr_err(self, "unknown algorithm");
		return NULL;
	}

	if (!gstdsp_register(dsp_handle, codec->uuid, DSP_DCD_LIBRARYTYPE, codec->filename)) {
		pr_err(self, "failed to register algo node library");
		return NULL;
	}

	if (!gstdsp_register(dsp_handle, codec->uuid, DSP_DCD_NODETYPE, codec->filename)) {
		pr_err(self, "failed to register algo node");
		return NULL;
	}

	/* only needed for jpegenc */
	if (base->alg == GSTDSP_JPEGENC) {
		/* SN_API == 0 doesn't have it, so don't fail */
		(void) gstdsp_register(dsp_handle, &conversions_uuid, DSP_DCD_LIBRARYTYPE, "conversions.dll64P");
	}

	{
		struct dsp_node_attr_in attrs = {
			.cb = sizeof(attrs),
			.priority = 5,
			.timeout = 1000,
		};
		void *arg_data;

		codec->create_args(base, &attrs.profile_id, &arg_data);

		if (!arg_data)
			return NULL;

		if (!dsp_node_allocate(dsp_handle, base->proc, codec->uuid, arg_data, &attrs, &node)) {
			pr_err(self, "dsp node allocate failed");
			free(arg_data);
			return NULL;
		}
		free(arg_data);
	}

	if (!dsp_node_create(dsp_handle, node)) {
		pr_err(self, "dsp node create failed");
		dsp_node_free(dsp_handle, node);
		return NULL;
	}

	pr_info(self, "dsp node created");

	return node;
}

static void check_supported_levels(GstDspVEnc *self, gint tgt_level)
{
	guint i;
	gint tgt_mbps, tgt_mbpf, tgt_bitrate;
	struct gstdsp_codec_level *cur, *level;

	if (!self->supported_levels)
		return;

	tgt_bitrate = self->user_max_bitrate;
	tgt_mbpf = ROUND_UP(self->width, 16) / 16 * ROUND_UP(self->height, 16) / 16;
	tgt_mbps = tgt_mbpf * self->framerate;

	if (!tgt_bitrate)
		tgt_bitrate =  self->bitrate;

search:
	level = cur = self->supported_levels;

	for (i = 0; i < self->nr_supported_levels; i++, cur++) {
		bool ok = false;

		/* is this the level we want? */
		if (tgt_level > 0 && tgt_level != cur->id)
			continue;

		/* is the bitrate enough? (and doesn't overshoot) */
		if (tgt_bitrate && cur->bitrate >= tgt_bitrate && level->bitrate < tgt_bitrate)
			ok = true;

		/* are the mbps enough? (and don't overshoot) */
		if (cur->mbps >= tgt_mbps && level->mbps < tgt_mbps)
			ok = true;

		/* at same level of mbps, get the biggest bitrate */
		if (cur->mbps >= tgt_mbps && cur->mbps == level->mbps &&
				!tgt_bitrate && cur->bitrate >= level->bitrate)
			ok = true;

		/* are the mbpf enough? (and don't overshoot) */
		if (cur->mbpf >= tgt_mbpf && level->mbpf < tgt_mbpf)
			ok = true;

		if (!ok)
			continue;

		/* we got a winner */
		level = cur;
	}

	if (tgt_level > 0 && tgt_level != level->id) {
		pr_warning(self, "invalid level: %d; ignoring", tgt_level);
		tgt_level = -1;
		goto search;
	}

	/* went beyond the table looking for a target; pick last one */
	if ((tgt_bitrate && level->bitrate < tgt_bitrate) || (level->mbps < tgt_mbps)) {
		if (tgt_level > 0) {
			pr_warning(self, "invalid level: %d; ignoring", tgt_level);
			tgt_level = -1;
			goto search;
		} else
			level = --cur;
	}

	if (!self->user_max_bitrate || self->user_max_bitrate >= level->bitrate)
		self->max_bitrate = level->bitrate;
	else
		self->max_bitrate = self->user_max_bitrate;

	self->level = level->id;

	pr_info(self, "level bitrate: %d", level->bitrate);
	pr_info(self, "max bitrate: %d", self->max_bitrate);
	pr_info(self, "level: %d", self->level);
}

static gboolean
sink_setcaps(GstPad *pad,
	     GstCaps *caps)
{
	GstDspVEnc *self;
	GstDspBase *base;
	GstStructure *in_struc;
	GstCaps *out_caps;
	GstStructure *out_struc;
	gint width = 0, height = 0;
	GstCaps *allowed_caps;
	gint tgt_level = -1;
	struct td_codec *codec;

	self = GST_DSP_VENC(GST_PAD_PARENT(pad));
	base = GST_DSP_BASE(self);
	codec = base->codec;

	if (!codec)
		return FALSE;

#ifdef DEBUG
	{
		gchar *str = gst_caps_to_string(caps);
		pr_info(self, "sink caps: %s", str);
		g_free(str);
	}
#endif

	in_struc = gst_caps_get_structure(caps, 0);

	out_caps = gst_caps_new_empty();

	switch (base->alg) {
	case GSTDSP_JPEGENC:
		out_struc = gst_structure_new("image/jpeg",
					      NULL);
		break;
	case GSTDSP_H263ENC:
		out_struc = gst_structure_new("video/x-h263",
					      "variant", G_TYPE_STRING, "itu",
					      NULL);
		break;
	case GSTDSP_MP4VENC:
		out_struc = gst_structure_new("video/mpeg",
					      "mpegversion", G_TYPE_INT, 4,
					      "systemstream", G_TYPE_BOOLEAN, FALSE,
					      NULL);
		break;
	case GSTDSP_H264ENC:
		out_struc = gst_structure_new("video/x-h264",
					      NULL);
		break;
	default:
		return FALSE;
	}

	if (gst_structure_get_int(in_struc, "width", &width))
		gst_structure_set(out_struc, "width", G_TYPE_INT, width, NULL);
	if (gst_structure_get_int(in_struc, "height", &height))
		gst_structure_set(out_struc, "height", G_TYPE_INT, height, NULL);
	gst_structure_get_fourcc(in_struc, "format", &self->color_format);

	switch (base->alg) {
	case GSTDSP_H263ENC:
	case GSTDSP_MP4VENC:
	case GSTDSP_H264ENC:
		base->output_buffer_size = width * height / 2;
		break;
	case GSTDSP_JPEGENC:
		if (width % 2 || height % 2)
			return FALSE;
		if (self->color_format == GST_MAKE_FOURCC('I', '4', '2', '0'))
			base->input_buffer_size = ROUND_UP(width, 16) * ROUND_UP(height, 16) * 3 / 2;
		else
			base->input_buffer_size = ROUND_UP(width, 16) * ROUND_UP(height, 16) * 2;
		base->output_buffer_size = width * height;
		if (self->quality < 10)
			base->output_buffer_size /= 10;
		else if (self->quality < 100)
			base->output_buffer_size /= (100 / self->quality);
		break;
	default:
		break;
	}

	switch (base->alg) {
	case GSTDSP_JPEGENC:
		du_port_alloc_buffers(base->ports[0], 1);
		du_port_alloc_buffers(base->ports[1], 2);
		break;
	default:
		du_port_alloc_buffers(base->ports[0], 2);
		du_port_alloc_buffers(base->ports[1], 4);
		break;
	}

	self->width = width;
	self->height = height;

	{
		const GValue *framerate = NULL;
		framerate = gst_structure_get_value(in_struc, "framerate");
		if (framerate) {
			gst_structure_set_value(out_struc, "framerate", framerate);
			/* calculate nearest integer */
			self->framerate = (gst_value_get_fraction_numerator(framerate) * 2 /
					   gst_value_get_fraction_denominator(framerate) + 1) / 2;
		}
	}

	/* see if downstream caps express something */
	allowed_caps = gst_pad_get_allowed_caps(base->srcpad);
	if (allowed_caps) {
		if (gst_caps_get_size(allowed_caps) > 0) {
			GstStructure *s;
			s = gst_caps_get_structure(allowed_caps, 0);
			gst_structure_get_int(s, "level", &tgt_level);
			if (base->alg == GSTDSP_H264ENC) {
				const char *stream_format;
				stream_format = gst_structure_get_string(s, "stream-format");
				if (stream_format && !strcmp(stream_format, "avc"))
					self->priv.h264.bytestream = false;
				else
					stream_format = "byte-stream";
				gst_structure_set(out_struc, "stream-format", G_TYPE_STRING, stream_format, NULL);
			}
		}
		gst_caps_unref(allowed_caps);
	}

	check_supported_levels(self, tgt_level);

	if (self->bitrate == 0)
		self->bitrate = self->max_bitrate;
	else if (self->bitrate > self->max_bitrate)
		self->bitrate = self->max_bitrate;

	gst_caps_append_structure(out_caps, out_struc);

#ifdef DEBUG
	{
		gchar *str = gst_caps_to_string(out_caps);
		pr_info(self, "src caps: %s", str);
		g_free(str);
	}
#endif

	if (!gst_pad_take_caps(base->srcpad, out_caps))
		return FALSE;

	base->node = create_node(self);
	if (!base->node) {
		pr_err(self, "dsp node creation failed");
		return FALSE;
	}

	if (codec->setup_params)
		codec->setup_params(base);

	if (!gstdsp_start(base)) {
		pr_err(self, "dsp start failed");
		return FALSE;
	}

	if (codec->send_params)
		codec->send_params(base, base->node);

	return TRUE;
}

static gboolean
sink_event(GstDspBase *base,
	   GstEvent *event)
{
	GstDspVEnc *self = GST_DSP_VENC(base);

	switch (GST_EVENT_TYPE(event)) {
	case GST_EVENT_CUSTOM_DOWNSTREAM:
		{
			const GstStructure *s;
			s = gst_event_get_structure(event);

			if (gst_structure_has_name(s, "GstForceKeyUnit")) {
				g_mutex_lock(self->keyframe_mutex);
				if (self->keyframe_event)
					gst_event_unref(self->keyframe_event);
				self->keyframe_event = event;
				g_mutex_unlock(self->keyframe_mutex);
				return TRUE;
			}
			break;
		}
	default:
		break;
	}

	if (parent_class->sink_event)
		return parent_class->sink_event(base, event);

	return gst_pad_push_event(base->srcpad, event);
}

static gboolean
src_event(GstDspBase *base,
	  GstEvent *event)
{
	GstDspVEnc *self = GST_DSP_VENC(base);

	switch (GST_EVENT_TYPE(event)) {
	case GST_EVENT_CUSTOM_UPSTREAM:
		{
			const GstStructure *s;
			s = gst_event_get_structure(event);

			if (gst_structure_has_name(s, "GstForceKeyUnit")) {
				g_mutex_lock(self->keyframe_mutex);
				/* make it downstream */
				GST_EVENT_TYPE(event) = GST_EVENT_CUSTOM_DOWNSTREAM;
				if (self->keyframe_event)
					gst_event_unref(self->keyframe_event);
				self->keyframe_event = event;
				g_mutex_unlock(self->keyframe_mutex);
				return TRUE;
			}
			break;
		}
	default:
		break;
	}

	if (parent_class->src_event)
		return parent_class->src_event(base, event);

	return gst_pad_push_event(base->sinkpad, event);
}

static void
set_property(GObject *obj,
	     guint prop_id,
	     const GValue *value,
	     GParamSpec *pspec)
{
	GstDspVEnc *self = GST_DSP_VENC(obj);

	switch (prop_id) {
	case ARG_BITRATE: {
		guint bitrate;
		bitrate = g_value_get_uint(value);
		if (self->max_bitrate && bitrate > (unsigned) self->max_bitrate)
			bitrate = self->max_bitrate;
		g_atomic_int_set(&self->bitrate, bitrate);
		break;
	}
	case ARG_MODE:
		self->mode = g_value_get_enum(value);
		/* guess intra-refresh, if not manually set */
		if (self->intra_refresh_set)
			break;
		self->intra_refresh = (self->mode == 1);
		break;
	case ARG_KEYFRAME_INTERVAL:
		g_atomic_int_set(&self->keyframe_interval, g_value_get_int(value));
		break;
	case ARG_MAX_BITRATE:
		self->user_max_bitrate = g_value_get_uint(value);
		break;
	case ARG_INTRA_REFRESH:
		self->intra_refresh = g_value_get_boolean(value);
		self->intra_refresh_set = true;
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}

static void
get_property(GObject *obj,
	     guint prop_id,
	     GValue *value,
	     GParamSpec *pspec)
{
	GstDspVEnc *self = GST_DSP_VENC(obj);

	switch (prop_id) {
	case ARG_BITRATE:
		g_value_set_uint(value, g_atomic_int_get(&self->bitrate));
		break;
	case ARG_MODE:
		g_value_set_enum(value, self->mode);
		break;
	case ARG_KEYFRAME_INTERVAL:
		g_value_set_int(value, g_atomic_int_get(&self->keyframe_interval));
		break;
	case ARG_MAX_BITRATE: {
		guint bitrate;

		bitrate = self->user_max_bitrate;
		if (!bitrate)
			bitrate = self->max_bitrate;
		g_value_set_uint(value, bitrate);
		break;
	}
	case ARG_INTRA_REFRESH:
		g_value_set_boolean(value, self->intra_refresh);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}

static void
reset(GstDspBase *base)
{
	GstDspVEnc *self = GST_DSP_VENC(base);

	pr_debug(self, "venc reset");

	/* some cleanup */
	if (base->alg == GSTDSP_H264ENC) {
		self->priv.h264.codec_data_done = FALSE;
		self->priv.h264.sps_received = FALSE;
		self->priv.h264.pps_received = FALSE;
		gst_buffer_replace(&self->priv.h264.sps, NULL);
		gst_buffer_replace(&self->priv.h264.pps, NULL);
		gst_buffer_replace(&self->priv.h264.codec_data, NULL);
	} else
		self->priv.mpeg4.codec_data_done = FALSE;
}

static void
instance_init(GTypeInstance *instance,
	      gpointer g_class)
{
	GstDspBase *base = GST_DSP_BASE(instance);
	GstDspVEnc *self = GST_DSP_VENC(instance);

	gst_pad_set_setcaps_function(base->sinkpad, sink_setcaps);
	base->reset = reset;

	self->bitrate = DEFAULT_BITRATE;
	self->mode = DEFAULT_MODE;
	self->keyframe_interval = DEFAULT_KEYFRAME_INTERVAL;
	self->intra_refresh = DEFAULT_INTRA_REFRESH;

	self->keyframe_mutex = g_mutex_new();
}

static void
finalize(GObject *obj)
{
	GstDspVEnc *self = GST_DSP_VENC(obj);
	g_mutex_free(self->keyframe_mutex);
	if (self->keyframe_event)
		gst_event_unref(self->keyframe_event);
	G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void
base_init(gpointer g_class)
{
	GstElementClass *element_class;
	GstPadTemplate *template;

	element_class = GST_ELEMENT_CLASS(g_class);

	template = gst_pad_template_new("sink", GST_PAD_SINK,
					GST_PAD_ALWAYS,
					generate_sink_template());

	gst_element_class_add_pad_template(element_class, template);
	gst_object_unref(template);
}

static void
class_init(gpointer g_class,
	   gpointer class_data)
{
	GObjectClass *gobject_class;
	GstDspBaseClass *base_class;

	parent_class = g_type_class_peek_parent(g_class);
	gobject_class = G_OBJECT_CLASS(g_class);
	base_class = GST_DSP_BASE_CLASS(g_class);

	gobject_class->set_property = set_property;
	gobject_class->get_property = get_property;

	g_object_class_install_property(gobject_class, ARG_BITRATE,
					g_param_spec_uint("bitrate", "Bit-rate",
							  "Encoding bit-rate (0 for auto)",
							  0, G_MAXUINT, DEFAULT_BITRATE,
							  G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, ARG_MODE,
					g_param_spec_enum("mode", "Encoding mode",
							  "Encoding mode",
							  GST_TYPE_DSPVENC_MODE,
							  DEFAULT_MODE,
							  G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, ARG_KEYFRAME_INTERVAL,
					g_param_spec_int("keyframe-interval", "Keyframe interval",
							 "Generate keyframes at every specified intervals (seconds)",
							 0, G_MAXINT, DEFAULT_KEYFRAME_INTERVAL, G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, ARG_MAX_BITRATE,
					g_param_spec_uint("max-bitrate", "Maximum Bit-rate",
							  "Maximum Encoding bit-rate (0 for auto)",
							  0, G_MAXUINT, DEFAULT_MAX_BITRATE,
							  G_PARAM_READWRITE));

	g_object_class_install_property(gobject_class, ARG_INTRA_REFRESH,
					g_param_spec_boolean("intra-refresh", "Intra-refresh",
							     "Whether or not to use periodic intra-refresh",
							     DEFAULT_INTRA_REFRESH, G_PARAM_READWRITE));

	gobject_class->finalize = finalize;

	base_class->src_event = src_event;
	base_class->sink_event = sink_event;
}

GType
gst_dsp_venc_get_type(void)
{
	static GType type;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo type_info = {
			.class_size = sizeof(GstDspVEncClass),
			.class_init = class_init,
			.base_init = base_init,
			.instance_size = sizeof(GstDspVEnc),
			.instance_init = instance_init,
		};

		type = g_type_register_static(GST_DSP_BASE_TYPE, "GstDspVEnc", &type_info, 0);
	}

	return type;
}
