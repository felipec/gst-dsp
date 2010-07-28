/*
 * Copyright (C) 2010 Nokia Corporation
 *
 * Authors:
 * Elamparithi Shanmugam <parithi@ti.com>
 * Felipe Contreras <felipe.contreras@nokia.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "gstdspipp.h"
#include "util.h"

static GstDspBaseClass *parent_class;

#define MAX_ALGS 16
#define IPP_TIMEOUT (2000 * 1000)

enum {
	IPP_YUV_420P = 1,
	IPP_YUV_422P,
	IPP_YUV_422IBE,
	IPP_YUV_422ILE,
	IPP_YUV_444P,
	IPP_YUV_411P,
	IPP_GRAY,
	IPP_RGB,
};

enum {
	DFGM_CREATE_XBF,
	DFGM_DESTROY_XBF,
	DFGM_SET_XBF_ALGS,
	DFGM_CLEAR_XBF_ALGS,
	DFGM_GET_MEM_REQ,
	DFGM_CREATE_XBF_PIPE,
	DFGM_DESTROY_XBF_PIPE,
	DFGM_START_PROCESSING,
	DFGM_STOP_PROCESSING,
	DFGM_QUEUE_BUFF,
	DFGM_CONTROL_PIPE,
	DFGM_FLUSH_PIPE,
	DFGM_EXIT,
};

enum {
	DFGM_CREATE_XBF_ACK = 0x1000,
	DFGM_DESTROY_XBF_ACK,
	DFGM_SET_XBF_ALGS_ACK,
	DFGM_CLEAR_XBF_ALGS_ACK,
	DFGM_GET_MEM_REQ_ACK,
	DFGM_CREATE_XBF_PIPE_ACK,
	DFGM_DESTROY_XBF_PIPE_ACK,
	DFGM_START_PROCESSING_ACK,
	DFGM_STOP_PROCESSING_ACK,
	DFGM_CONTROL_PIPE_ACK,
	DFGM_FLUSH_PIPE_ACK,
	DFGM_FREE_BUFF,
	DFGM_EVENT_ERROR,
	DFGM_EXIT_ACK,
};

enum {
	CONTENT_TYPE_BUFFER,
	CONTENT_TYPE_IN_ARGS,
	CONTENT_TYPE_OUT_ARGS,
};

struct ipp_name_string {
	int8_t str[25];
	uint32_t size;
};

static inline dmm_buffer_t *ipp_calloc(GstDspIpp *self, size_t size, int dir)
{
	GstDspBase *base = GST_DSP_BASE(self);
	return dmm_buffer_calloc(base->dsp_handle, base->proc, size, dir);
}

/* star algo */

struct ipp_star_algo_create_params {
	uint32_t size;
	int32_t num_in_bufs;
	int32_t num_out_bufs;
};

static struct ipp_algo *
get_star_params(GstDspIpp *self)
{
	struct ipp_algo *algo;
	dmm_buffer_t *tmp;
	struct ipp_star_algo_create_params *params;

	algo = calloc(1, sizeof(*algo));
	if (!algo)
		return NULL;

	tmp = ipp_calloc(self, sizeof(*params), DMA_TO_DEVICE);
	params = tmp->data;
	params->size = sizeof(*params);
	params->num_in_bufs = 3;
	dmm_buffer_map(tmp);

	algo->create_params = tmp;
	algo->fxn = "STAR_ALG";

	return algo;
}

/* yuv conversion */

struct ipp_crcbs_yuv_algo_create_params {
	uint32_t size;
	int32_t max_width;
	int32_t max_height;
	int32_t error_code;
};

static struct ipp_algo *
get_yuvc_params(GstDspIpp *self)
{
	struct ipp_algo *algo;
	dmm_buffer_t *tmp;
	struct ipp_crcbs_yuv_algo_create_params *params;

	algo = calloc(1, sizeof(*algo));
	if (!algo)
		return NULL;

	tmp = ipp_calloc(self, sizeof(*params), DMA_TO_DEVICE);
	params = tmp->data;
	params->size = sizeof(*params);
	params->max_width = self->width;
	params->max_height = self->height;
	dmm_buffer_map(tmp);

	algo->create_params = tmp;
	algo->fxn = "YUVCONVERT_IYUVCONVERT";
	algo->dma_fxn = "YUVCONVERT_TI_IDMA3";

	return algo;
}

static bool setup_ipp_params(GstDspIpp *self)
{
	int i = 0;
	self->algos[i++] = get_star_params(self);
	self->algos[i++] = get_yuvc_params(self);
	self->nr_algos = i;

	return true;
}

static void free_message_args(GstDspIpp *self)
{
	unsigned i;
	dmm_buffer_t **c;
	c = self->msg_ptr;
	for (i = 0; i < ARRAY_SIZE(self->msg_ptr); i++, c++) {
		dmm_buffer_free(*c);
		*c = NULL;
	}
}

static void got_message(GstDspBase *base, struct dsp_msg *msg)
{
	GstDspIpp *self = GST_DSP_IPP(base);
	int command_id = msg->cmd;

	switch (command_id) {
	case DFGM_CREATE_XBF_ACK:
	case DFGM_CREATE_XBF_PIPE_ACK:
	case DFGM_DESTROY_XBF_ACK:
	case DFGM_SET_XBF_ALGS_ACK:
	case DFGM_STOP_PROCESSING_ACK:
	case DFGM_CLEAR_XBF_ALGS_ACK:
	case DFGM_DESTROY_XBF_PIPE_ACK:
	case DFGM_START_PROCESSING_ACK:
	case DFGM_FREE_BUFF:
		free_message_args(self);
		break;
	default:
		pr_warning(self, "unhandled command 0x%x", command_id);
		break;
	}

	g_sem_up(self->msg_sem);
}

static bool send_msg(GstDspIpp *self, int id,
		     dmm_buffer_t *arg1,
		     dmm_buffer_t *arg2,
		     dmm_buffer_t *arg3)
{
	GstDspBase *base = GST_DSP_BASE(self);

	g_sem_down(self->msg_sem);

	self->msg_ptr[0] = arg1;
	self->msg_ptr[1] = arg2;
	self->msg_ptr[2] = arg3;

	return dsp_send_message(base->dsp_handle, base->node, id,
				arg1 ? (uint32_t)arg1->map : 0,
				arg2 ? (uint32_t)arg2->map : 0);
}

struct create_xbf_msg_elem_1 {
	uint32_t size;
	uint32_t plat_fxns_string;
	uint32_t string_size;
	uint32_t plat_prms_ptr;
};

static bool create_xbf(GstDspIpp *self)
{
	struct create_xbf_msg_elem_1 *create_xbf_msg1;
	dmm_buffer_t *b_arg_1;
	dmm_buffer_t *b_plat_fxn_string;
	const struct ipp_name_string plat_fxns_name = {
		.str = "IPPPLATFORMFXNS",
		.size = sizeof("IPPPLATFORMFXNS"),
	};

	b_plat_fxn_string = ipp_calloc(self, plat_fxns_name.size, DMA_TO_DEVICE);
	memcpy(b_plat_fxn_string->data, plat_fxns_name.str, plat_fxns_name.size);
	dmm_buffer_map(b_plat_fxn_string);

	b_arg_1 = ipp_calloc(self, sizeof(*create_xbf_msg1), DMA_BIDIRECTIONAL);
	create_xbf_msg1 = b_arg_1->data;
	create_xbf_msg1->size = sizeof(*create_xbf_msg1);
	create_xbf_msg1->plat_fxns_string = (uint32_t)b_plat_fxn_string->map;
	create_xbf_msg1->string_size = plat_fxns_name.size;
	dmm_buffer_map(b_arg_1);

	return send_msg(self, DFGM_CREATE_XBF, b_arg_1, NULL, b_plat_fxn_string);
}

struct set_xbf_algs_msg_elem_1 {
	uint32_t size;
	uint32_t num_algs;
	struct set_xbf_algs_msg_elem_1_elem {
		uint32_t fxn_name_ptr;
		uint32_t fxn_name_size;
		uint32_t dma_name_ptr;
		uint32_t dma_name_size;
	} alg_tables[MAX_ALGS];
};

struct set_xbf_algs_msg_elem_2 {
	uint32_t size;
	uint32_t error_code;
	uint32_t mem_int_array_ptr;
};

static dmm_buffer_t *str_to_ipp_str(GstDspIpp *self, const char *s)
{
	struct ipp_name_string *ipp_s;
	dmm_buffer_t *tmp;

	tmp = ipp_calloc(self, sizeof(*ipp_s), DMA_TO_DEVICE);
	ipp_s = tmp->data;
	ipp_s->size = strlen(s);
	memcpy(ipp_s->str, s, ipp_s->size);
	dmm_buffer_map(tmp);

	tmp->len = ipp_s->size;

	return tmp;
}

static bool set_algorithm(GstDspIpp *self)
{
	int nr_algos = self->nr_algos;
	struct set_xbf_algs_msg_elem_1 *msg_elem_1;
	struct set_xbf_algs_msg_elem_2 *msg_elem_2;
	dmm_buffer_t *b_arg_1;
	dmm_buffer_t *b_arg_2;
	int i;

	b_arg_1 = ipp_calloc(self, sizeof(*msg_elem_1), DMA_BIDIRECTIONAL);
	msg_elem_1 = b_arg_1->data;
	msg_elem_1->size = sizeof(*msg_elem_1);
	msg_elem_1->num_algs = nr_algos;

	b_arg_2 = ipp_calloc(self, sizeof(*msg_elem_2), DMA_BIDIRECTIONAL);
	msg_elem_2 = b_arg_2->data;
	msg_elem_2->size = sizeof(*msg_elem_2);
	dmm_buffer_map(b_arg_2);

	for (i = 0; i < nr_algos; i++) {
		struct set_xbf_algs_msg_elem_1_elem *e;
		struct ipp_algo *algo = self->algos[i];
		dmm_buffer_t *str;

		e = &msg_elem_1->alg_tables[i];

		if (algo->fxn) {
			str = str_to_ipp_str(self, algo->fxn);
			algo->b_algo_fxn = str;
			e->fxn_name_ptr = (uint32_t)str->map;
			e->fxn_name_size = str->len;
		}

		if (algo->dma_fxn) {
			str = str_to_ipp_str(self, algo->dma_fxn);
			algo->b_dma_fxn = str;
			e->dma_name_ptr = (uint32_t)str->map;
			e->dma_name_size = str->len;
		}
	}

	dmm_buffer_map(b_arg_1);
	return send_msg(self, DFGM_SET_XBF_ALGS, b_arg_1, b_arg_2, NULL);
}

struct filter_graph_edge {
	int32_t n[MAX_ALGS];
};

struct filter_graph {
	struct filter_graph_edge graph_connection[MAX_ALGS][MAX_ALGS];
	struct filter_graph_edge output_buf_distribution[MAX_ALGS];
};

static void prepare_filter_graph(GstDspIpp *self)
{
	int32_t length;
	int32_t *ptr;
	int nr_algos = self->nr_algos;
	struct filter_graph *flt_graph;
	int i;
	int port = 1;

	self->flt_graph = ipp_calloc(self, sizeof(*flt_graph), DMA_TO_DEVICE);
	flt_graph = self->flt_graph->data;

	ptr = (int32_t *) flt_graph;
	length = sizeof(*flt_graph) / sizeof(int32_t);

	/* init graph to -1 */
	for (i = 0; i < length; i++)
		ptr[i] = -1;

	for (i = 0; i < nr_algos - 1; i++)
		*flt_graph->graph_connection[i][i + 1].n = 0;

	/*
	 * Star ports:
	 * 1: output
	 * 2: intermediate
	 *
	 * Use these alternatively, and the last one in the pipeline should
	 * always be 1.
	 */
	for (i = nr_algos - 1; i >= 1; i--) {
		*flt_graph->output_buf_distribution[i].n = port;
		port = !(port - 1) + 1;
	}

	dmm_buffer_map(self->flt_graph);
}

struct create_xbf_pipe_msg_elem_1 {
	uint32_t size;
	uint32_t filter_graph_ptr;
	uint32_t intrmd_bufs_array_ptr;
	uint32_t platform_params_ptr;
	uint32_t num_platform_params;
	uint32_t create_params_array_ptr;
	uint32_t num_create_params;
	uint32_t num_in_port;
	uint32_t num_out_port;
};

static bool create_pipe(GstDspIpp *self)
{
	struct create_xbf_pipe_msg_elem_1 *arg_1;
	dmm_buffer_t *b_arg_1;
	dmm_buffer_t *b_create_params;
	int32_t *create_params_ptr_array;
	int i;
	int nr_algos = self->nr_algos;

	b_create_params = ipp_calloc(self, sizeof(int32_t) * nr_algos, DMA_TO_DEVICE);
	create_params_ptr_array = b_create_params->data;

	for (i = 0; i < nr_algos; i++)
		create_params_ptr_array[i] = (int32_t)self->algos[i]->create_params->map;
	dmm_buffer_map(b_create_params);

	b_arg_1 = ipp_calloc(self, sizeof(*arg_1), DMA_BIDIRECTIONAL);
	arg_1 = b_arg_1->data;
	arg_1->size = sizeof(*arg_1);
	arg_1->filter_graph_ptr = (uint32_t)self->flt_graph->map;
	arg_1->create_params_array_ptr = (uint32_t)b_create_params->map;
	arg_1->num_create_params = nr_algos;
	arg_1->num_in_port = 2;
	dmm_buffer_map(b_arg_1);

	return send_msg(self, DFGM_CREATE_XBF_PIPE, b_arg_1, NULL, b_create_params);
}

static bool start_processing(GstDspIpp *self)
{
	return send_msg(self, DFGM_START_PROCESSING, NULL, NULL, NULL);
}

struct stop_processing_msg_elem_1 {
	uint32_t size;
	uint32_t reset_state;
};

static bool stop_processing(GstDspIpp *self)
{
	struct stop_processing_msg_elem_1 *arg_1;
	dmm_buffer_t *b_arg_1;

	b_arg_1 = ipp_calloc(self, sizeof(*arg_1), DMA_BIDIRECTIONAL);
	arg_1 = b_arg_1->data;
	arg_1->size = sizeof(*arg_1);
	arg_1->reset_state = 1;
	dmm_buffer_map(b_arg_1);

	return send_msg(self, DFGM_STOP_PROCESSING, b_arg_1, NULL, NULL);
}

static bool destroy_pipe(GstDspIpp *self)
{
	return send_msg(self, DFGM_DESTROY_XBF_PIPE, NULL, NULL, NULL);
}

static bool clear_algorithm(GstDspIpp *self)
{
	return send_msg(self, DFGM_CLEAR_XBF_ALGS, NULL, NULL, NULL);
}

struct destroy_xbf_msg_elem_1 {
	uint32_t size;
	uint32_t plat_prms_ptr;
};

static bool destroy_xbf(GstDspIpp *self)
{
	struct destroy_xbf_msg_elem_1 *arg_1;
	dmm_buffer_t *b_arg_1;

	b_arg_1 = ipp_calloc(self, sizeof(*arg_1), DMA_BIDIRECTIONAL);
	arg_1 = b_arg_1->data;
	arg_1->size = sizeof(*arg_1);
	dmm_buffer_map(b_arg_1);

	return send_msg(self, DFGM_DESTROY_XBF, b_arg_1, NULL, NULL);
}

static bool init_pipe(GstDspBase *base)
{
	GstDspIpp *self = GST_DSP_IPP(base);
	bool ok;

	ok = create_xbf(self);
	if (!ok)
		goto leave;
	ok = set_algorithm(self);
	if (!ok)
		goto leave;
	prepare_filter_graph(self);

	ok = create_pipe(self);
	if (!ok)
		goto leave;
	ok = start_processing(self);
	if (!ok)
		goto leave;

leave:
	return ok;
}

static bool send_buffer(GstDspBase *base, dmm_buffer_t *b, guint id)
{
	return true;
}

static bool send_play_message(GstDspBase *base)
{
	return true;
};

static bool send_stop_message(GstDspBase *base)
{
	GstDspIpp *self = GST_DSP_IPP(base);
	bool ok;
	unsigned i;

	ok = stop_processing(self);
	if (!ok)
		goto leave;

	ok = destroy_pipe(self);
	if (!ok)
		goto leave;

	ok = clear_algorithm(self);
	if (!ok)
		goto leave;

	ok = destroy_xbf(self);
	if (!ok)
		goto leave;

	/* let's wait for the previous msg to complete */
	g_sem_down(self->msg_sem);

	for (i = 0; i < self->nr_algos; i++) {
		struct ipp_algo *algo = self->algos[i];
		if (!algo)
			break;
		dmm_buffer_free(algo->create_params);
		dmm_buffer_free(algo->b_algo_fxn);
		dmm_buffer_free(algo->b_dma_fxn);
		free(algo);
		self->algos[i] = NULL;
	}

	if (self->flt_graph) {
		dmm_buffer_free(self->flt_graph);
		self->flt_graph = NULL;
	}

	g_sem_up(self->msg_sem);

leave:
	return ok;
}

static inline GstCaps *generate_sink_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();
	struc = gst_structure_new("video/x-raw-yuv", "format",
				  GST_TYPE_FOURCC, GST_MAKE_FOURCC('U','Y','V','Y'), NULL);
	gst_caps_append_structure(caps, struc);
	return caps;
}

static inline GstCaps *generate_src_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();
	struc = gst_structure_new("video/x-raw-yuv", "format",
				  GST_TYPE_FOURCC, GST_MAKE_FOURCC('U','Y','V','Y'), NULL);
	gst_caps_append_structure(caps, struc);
	return caps;
}

static void *create_node(GstDspIpp *self)
{
	GstDspBase *base;
	int dsp_handle;
	struct dsp_node *node = NULL;

	const struct dsp_uuid dfgm_uuid = { 0xe57d1a99, 0xbc8d, 0x463c, 0xac, 0x93,
		{ 0x49, 0xeA, 0x1A, 0xC0, 0x19, 0x53 } };

	const struct dsp_uuid ipp_uuid = { 0x8ea1b508, 0x49be, 0x4cd0, 0xbb, 0x12,
		{ 0xea, 0x95, 0x00, 0x58, 0xb3, 0x6b } };

	struct dsp_node_attr_in attrs = {
		.cb = sizeof(attrs),
		.priority = 5,
		.timeout = 1000,
	};

	base = GST_DSP_BASE(self);
	dsp_handle = base->dsp_handle;

	if (!gstdsp_register(dsp_handle, &dfgm_uuid, DSP_DCD_LIBRARYTYPE, "dfgm.dll64P")) {
		pr_err(self, "failed to register usn node library");
		return NULL;
	}

	if (!gstdsp_register(dsp_handle, &ipp_uuid, DSP_DCD_LIBRARYTYPE, "ipp_sn.dll64P")) {
		pr_err(self, "failed to register algo node library");
		return NULL;
	}

	if (!gstdsp_register(dsp_handle, &ipp_uuid, DSP_DCD_NODETYPE, "ipp_sn.dll64P")) {
		pr_err(self, "failed to register algo node");
		return NULL;
	}

	if (!dsp_node_allocate(dsp_handle, base->proc, &ipp_uuid, NULL, &attrs, &node)) {
		pr_err(self, "dsp node allocate failed");
		dsp_node_free(dsp_handle, node);
		return NULL;
	}

	if (!dsp_node_create(dsp_handle, node)) {
		pr_err(self, "dsp node create failed");
		dsp_node_free(dsp_handle, node);
		return NULL;
	}

	return node;
}

static gboolean sink_setcaps(GstPad *pad, GstCaps *caps)
{
	GstDspIpp *self;
	GstDspBase *base;
	GstStructure *in_struc;
	GstCaps *out_caps;
	GstStructure *out_struc;
	int width = 0;
	int height = 0;

	self = GST_DSP_IPP(GST_PAD_PARENT(pad));
	base = GST_DSP_BASE(self);

	in_struc = gst_caps_get_structure(caps, 0);
	out_caps = gst_caps_new_empty();
	out_struc = gst_structure_new("video/x-raw-yuv", NULL);

	if (gst_structure_get_int(in_struc, "width", &width))
		gst_structure_set(out_struc, "width", G_TYPE_INT, width, NULL);

	if (gst_structure_get_int(in_struc, "height", &height))
		gst_structure_set(out_struc, "height", G_TYPE_INT, height, NULL);

	self->width = width;
	self->height = height;

	gst_caps_append_structure(out_caps, out_struc);

	if (!gst_pad_take_caps(base->srcpad, out_caps))
		return FALSE;

	base->node = create_node(self);

	if (!base->node) {
		pr_err(self, "dsp node creation failed");
		return FALSE;
	}

	if (!setup_ipp_params(self))
		return FALSE;

	if (!gstdsp_start(base)) {
		pr_err(self, "dsp start failed");
		return FALSE;
	}

	if (!init_pipe(base))
		return FALSE;

	return true;
}

static void instance_init(GTypeInstance *instance, gpointer g_class)
{
	GstDspBase *base = GST_DSP_BASE(instance);
	GstDspIpp *self = GST_DSP_IPP(instance);

	base->got_message = got_message;
	base->send_buffer = send_buffer;
	base->send_play_message = send_play_message;
	base->send_stop_message = send_stop_message;
	du_port_alloc_buffers(base->ports[0], 1);
	du_port_alloc_buffers(base->ports[1], 1);
	self->msg_sem = g_sem_new(1);

	gst_pad_set_setcaps_function(base->sinkpad, sink_setcaps);
}

static void finalize(GObject *obj)
{
	GstDspIpp *self = GST_DSP_IPP(obj);

	g_sem_free(self->msg_sem);
	G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void class_init(gpointer g_class, gpointer class_data)
{
	GObjectClass *gobject_class;

	gobject_class = G_OBJECT_CLASS(g_class);

	parent_class = g_type_class_peek_parent(g_class);
	gobject_class->finalize = finalize;
}

static void base_init(gpointer g_class)
{
	GstElementClass *element_class;
	GstPadTemplate *template;

	element_class = GST_ELEMENT_CLASS(g_class);

	gst_element_class_set_details_simple(element_class,
					     "DSP IPP",
					     "Codec/Encoder/Image",
					     "Image processing algorithms",
					     "Texas Instruments");

	template = gst_pad_template_new("src", GST_PAD_SRC,
					GST_PAD_ALWAYS,
					generate_src_template());

	gst_element_class_add_pad_template(element_class, template);
	gst_object_unref(template);

	template = gst_pad_template_new("sink", GST_PAD_SINK,
					GST_PAD_ALWAYS,
					generate_sink_template());

	gst_element_class_add_pad_template(element_class, template);
	gst_object_unref(template);
}

GType gst_dsp_ipp_get_type(void)
{
	static GType type;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo type_info = {
			.class_size = sizeof(GstDspIppClass),
			.base_init = base_init,
			.class_init = class_init,
			.instance_size = sizeof(GstDspIpp),
			.instance_init = instance_init,
		};

		type = g_type_register_static(GST_DSP_BASE_TYPE, "GstDspIpp", &type_info, 0);
	}
	return type;
}
