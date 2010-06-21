#include <gst/gst.h>

#include <string.h>
#include <stdbool.h>

#include "gstdspparse.h"
#include "gstdspvdec.h"

typedef bool (*parse_func)(GstDspBase *base, GstBuffer *buf);

static char *filename;
static char *result;
static char *result_crop;

static GMainLoop *loop;
static GstElement *pipeline;
static GstElement *src, *demux, *sink;

static GstDspBase *dec;

static parse_func gst_dsp_parse;

static unsigned buffer_count;

static gboolean bus_cb(GstBus *bus, GstMessage *msg, gpointer data)
{
	switch (GST_MESSAGE_TYPE(msg)) {
	case GST_MESSAGE_EOS:
		g_main_loop_quit(loop);
		break;
	case GST_MESSAGE_ERROR: {
		gchar *debug;
		GError *err;

		gst_message_parse_error(msg, &err, &debug);

		g_printerr("error: %s: %s\n", err->message, debug);
		g_error_free(err);
		g_free(debug);

		g_main_loop_quit(loop);
		break;
	}
	default:
		break;
	}

	return TRUE;
}

static void on_pad_added(GstElement *element, GstPad *pad, gpointer data)
{
	GstPad *sinkpad;
	GstStructure *struc;
	const gchar *name;

	sinkpad = gst_element_get_static_pad(sink, "sink");
	gst_pad_link(pad, sinkpad);
	gst_object_unref(sinkpad);

	struc = gst_caps_get_structure(GST_PAD_CAPS(pad), 0);

	name = gst_structure_get_name(struc);
	if (strncmp(name, "video/", 6) != 0)
		return;

	if (strcmp(name, "video/x-h263") == 0)
		gst_dsp_parse = gst_dsp_h263_parse;
	else if (strcmp(name, "video/x-h264") == 0)
		gst_dsp_parse = gst_dsp_h264_parse;
	else
		gst_dsp_parse = gst_dsp_mpeg4_parse;
}

static void handoff(GstElement *object,
	     GstBuffer *buffer,
	     GstPad *pad,
	     gpointer user_data)
{
	GstStructure *struc;
	const GValue *codec_data;
	bool r;

	buffer_count++;

	struc = gst_caps_get_structure(GST_BUFFER_CAPS(buffer), 0);
	codec_data = gst_structure_get_value(struc, "codec_data");
	if (codec_data) {
		GstBuffer *tmp;
		tmp = gst_value_get_buffer(codec_data);
		r = gst_dsp_parse(dec, tmp);
		if (r)
			goto ok;
		else
			g_printerr("bad codec data\n");
	}

	r = gst_dsp_parse(dec, buffer);
ok:
	if (r) {
		GstDspVDec *vdec = GST_DSP_VDEC(dec);
		result = g_strdup_printf("fs=%ix%i",
				vdec->width, vdec->height);
		result_crop = g_strdup_printf("cfs=%ix%i",
				vdec->crop_width, vdec->crop_height);
	} else {
		g_printerr("parse error\n");
	}
	g_main_loop_quit(loop);
}

static void init(void)
{
	pipeline = gst_pipeline_new("parse");

	src = gst_element_factory_make("filesrc", "src");
	demux = gst_element_factory_make("qtdemux", "demux");
	sink = gst_element_factory_make("fakesink", "sink");

	gst_bin_add_many(GST_BIN(pipeline), src, demux, sink, NULL);
	gst_element_link(src, demux);
	g_signal_connect(demux, "pad-added", G_CALLBACK(on_pad_added), NULL);

	GstBus *bus;
	bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline));
	gst_bus_add_watch(bus, bus_cb, NULL);
	gst_object_unref(bus);

	g_object_set(G_OBJECT(src), "location", filename, NULL);

	g_object_set(G_OBJECT(sink), "signal-handoffs", TRUE, "num-buffers", 1, NULL);
	g_signal_connect(sink, "handoff", G_CALLBACK(handoff), NULL);

	gst_element_set_state(pipeline, GST_STATE_PLAYING);
}

GstDebugCategory *gstdsp_debug;

int main(int argc, char *argv[])
{
	int ret;
	char *expected;
	char *expected_crop = NULL;

	gst_init(&argc, &argv);

	loop = g_main_loop_new(NULL, FALSE);

	if (argc < 3)
		return -1;

	filename = argv[1];
	expected = argv[2];

	if (argc >= 4)
		expected_crop = argv[3];

#ifndef GST_DISABLE_GST_DEBUG
	gstdsp_debug = _gst_debug_category_new("dsp", 0, "DSP stuff");
#endif

	dec = g_object_new(GST_DSP_VDEC_TYPE, NULL);

	init();

	g_main_loop_run(loop);

	if (result) {
		g_print("%s %s\n", result, result_crop);
	} else {
		if (buffer_count == 0)
			g_printerr("no data\n");
		else
			g_printerr("no result\n");
	}

	ret = !!g_strcmp0(expected, result);
	if (!ret && expected_crop)
		ret = !!g_strcmp0(expected_crop, result_crop);

	g_free(result);
	g_free(result_crop);

	if (pipeline) {
		gst_element_set_state(pipeline, GST_STATE_NULL);
		gst_object_unref(GST_OBJECT(pipeline));
	}

	g_object_unref(dec);

	g_main_loop_unref(loop);

	return -ret;
}
