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
#define MAX_WIDTH 4096
#define MAX_HEIGHT 3072
#define MAX_TOTAL_PIXEL (4000 * 3008)

#if SN_API == 0
#define INTERNAL_FORMAT IPP_YUV_422P
#else
#define INTERNAL_FORMAT IPP_YUV_420P
#endif

#define OVERWRITE_INPUT_BUFFER

static bool send_stop_message(GstDspBase *base);
static gboolean sink_event(GstDspBase *base, GstEvent *event);
static void send_processing_info_gstmessage(GstDspIpp *self, const gchar* info);

enum {
	PROP_0,
	PROP_NOISE_FILTER_STRENGTH,
};

enum {
	NOISE_FILTER_CUSTOM,
	NOISE_FILTER_NORMAL,
	NOISE_FILTER_AGGRESSIVE,
};

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
	DFGM_ERROR_NONE = 0,
	DFGM_ERROR_CREATE_XBF = 0x0100,
	DFGM_ERROR_DESTROY_XBF = 0x0200,
	DFGM_ERROR_SET_ALGS = 0x0400,
	DFGM_ERROR_DESTROY_ALGS = 0x0800,
	DFGM_ERROR_CREATE_XBF_PIPE = 0x2000,
	DFGM_ERROR_DESTROY_XBF_PIPE = 0x4000,
	DFGM_ERROR_CONTROL_PIPE = 0x8000,
	DFGM_ERROR_QUEUE_BUFF = 0x10000,
	DFGM_ERROR_INVALID_STATE = 0x80000
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

#define GST_TYPE_IPP_NOISE_FILTER_STRENGTH (gst_dsp_ipp_get_noise_filter_type())
static GType
gst_dsp_ipp_get_noise_filter_type(void)
{
	static GType gst_dspipp_strength_type;

	static GEnumValue strength[] = {
		{NOISE_FILTER_CUSTOM, "Custom", "custom"},
		{NOISE_FILTER_NORMAL, "Normal", "normal"},
		{NOISE_FILTER_AGGRESSIVE, "Aggressive", "aggressive"},
		{0, NULL, NULL},
	};

	if (G_UNLIKELY(!gst_dspipp_strength_type)) {
		gst_dspipp_strength_type =
				g_enum_register_static("GstDspIppNoiseFilterStrength", strength);
	}
	return gst_dspipp_strength_type;
}

#define DEFAULT_NOISE_FILTER_STRENGTH NOISE_FILTER_CUSTOM

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

struct ipp_star_algo_in_args {
	uint32_t size;
};

struct ipp_star_algo_out_args {
	uint32_t size;
	int32_t error;
};

static struct ipp_algo *
get_star_params(GstDspIpp *self)
{
	struct ipp_algo *algo;
	dmm_buffer_t *tmp;
	struct ipp_star_algo_create_params *params;
	struct ipp_star_algo_in_args *in_args;
	struct ipp_star_algo_out_args *out_args;

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

	tmp = ipp_calloc(self, sizeof(*in_args), DMA_TO_DEVICE);
	in_args = tmp->data;
	in_args->size = sizeof(*in_args);
	dmm_buffer_map(tmp);

	algo->in = tmp;

	tmp = ipp_calloc(self, sizeof(*out_args), DMA_TO_DEVICE);
	out_args = tmp->data;
	out_args->size = sizeof(*out_args);
	dmm_buffer_map(tmp);

	algo->out = tmp;

	return algo;
}

/* yuv conversion */

struct ipp_crcbs_yuv_algo_create_params {
	uint32_t size;
	int32_t max_width;
	int32_t max_height;
	int32_t error_code;
};

struct ipp_yuvc_algo_in_args {
	uint32_t size;
	int32_t input_height;
	int32_t input_width;
	int32_t output_chroma_format;
	int32_t input_chroma_format;
};

struct ipp_yuvc_algo_out_args {
	uint32_t size;
	int32_t extended_error;
	int32_t output_chroma_format;
	int32_t out_buf_size;
	int32_t out_width;
	int32_t out_height;
};

static struct ipp_algo *
get_yuvc_params(GstDspIpp *self, int in_fmt, int out_fmt)
{
	struct ipp_algo *algo;
	dmm_buffer_t *tmp;
	struct ipp_crcbs_yuv_algo_create_params *params;
	struct ipp_yuvc_algo_in_args *in_args;
	struct ipp_yuvc_algo_out_args *out_args;

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

	tmp = ipp_calloc(self, sizeof(*in_args), DMA_TO_DEVICE);
	in_args = tmp->data;
	in_args->size = sizeof(*in_args);
	in_args->input_width = self->width;
	in_args->input_height = self->height;

	in_args->input_chroma_format = in_fmt;
	in_args->output_chroma_format = out_fmt;

	dmm_buffer_map(tmp);

	algo->in = tmp;

	tmp = ipp_calloc(self, sizeof(*out_args), DMA_TO_DEVICE);
	out_args = tmp->data;
	out_args->size = sizeof(*out_args);
	dmm_buffer_map(tmp);

	algo->out = tmp;

	return algo;
}

/* chroma suppression  */

struct ipp_crcbs_algo_in_args {
	uint32_t size;
	int32_t input_height;
	int32_t input_width;
	int32_t input_chroma_format;
};

struct ipp_crcbs_algo_out_args {
	uint32_t size;
	int32_t extended_error;
	int32_t out_buf_size;
	int32_t out_width;
	int32_t out_height;
};

static struct ipp_algo *
get_crcbs_params(GstDspIpp *self)
{
	struct ipp_algo *algo;
	dmm_buffer_t *tmp;
	struct ipp_crcbs_yuv_algo_create_params *params;
	struct ipp_crcbs_algo_in_args *in_args;
	struct ipp_crcbs_algo_out_args *out_args;

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
	algo->fxn = "CRCBS_ICRCBS";
	algo->dma_fxn = "CRCBS_TI_IDMA3";

	tmp = ipp_calloc(self, sizeof(*in_args), DMA_TO_DEVICE);
	in_args = tmp->data;
	in_args->size = sizeof(*in_args);
	in_args->input_width = self->width;
	in_args->input_height = self->height;
	in_args->input_chroma_format = INTERNAL_FORMAT;
	dmm_buffer_map(tmp);

	algo->in = tmp;

	tmp = ipp_calloc(self, sizeof(*out_args), DMA_TO_DEVICE);
	out_args = tmp->data;
	out_args->size = sizeof(*out_args);
	dmm_buffer_map(tmp);

	algo->out = tmp;

	return algo;
}

/* EENF: Edge Enhancement and Noise Filter */

struct ipp_eenf_algo_create_params {
	uint32_t size;
	int32_t input_buffer_size_for_in_place;
	int16_t in_place;
	int16_t error_code;
	int16_t max_image_size_v;
	int16_t max_image_size_h;
};

struct ipp_eenf_algo_in_args {
	uint32_t size;
	int32_t input_chroma_format;
	int16_t in_full_width;
	int16_t in_full_height;
	int16_t in_offset_v;
	int16_t in_offset_h;
	int16_t input_width;
	int16_t input_height;
	int16_t in_place;
	int16_t nf_processing;
};

struct ipp_eenf_algo_out_args {
	uint32_t size;
	int32_t extended_error;
	int16_t out_width;
	int16_t out_height;
};

static struct ipp_algo *
get_eenf_params(GstDspIpp *self)
{
	struct ipp_algo *algo;
	dmm_buffer_t *tmp;
	struct ipp_eenf_algo_create_params *params;
	struct ipp_eenf_algo_in_args *in_args;
	struct ipp_eenf_algo_out_args *out_args;

	algo = calloc(1, sizeof(*algo));
	if (!algo)
		return NULL;

	tmp = ipp_calloc(self, sizeof(*params), DMA_TO_DEVICE);
	params = tmp->data;
	params->size = sizeof(*params);
	params->max_image_size_v = self->height;
	params->max_image_size_h = self->width;
	dmm_buffer_map(tmp);

	algo->create_params = tmp;
	algo->fxn = "EENF_IEENF";
	algo->dma_fxn = "EENF_TI_IDMA3";

	tmp = ipp_calloc(self, sizeof(*in_args), DMA_TO_DEVICE);
	in_args = tmp->data;
	in_args->size = sizeof(*in_args);
	in_args->input_chroma_format = INTERNAL_FORMAT;
	in_args->in_full_width = self->width;
	in_args->in_full_height = self->height;
	in_args->input_width = self->width;
	in_args->input_height = self->height;
	dmm_buffer_map(tmp);

	algo->in = tmp;

	tmp = ipp_calloc(self, sizeof(*out_args), DMA_TO_DEVICE);
	out_args = tmp->data;
	out_args->size = sizeof(*out_args);
	dmm_buffer_map(tmp);

	algo->out = tmp;

	return algo;
}

static bool setup_ipp_params(GstDspIpp *self)
{
	int i = 0;
	self->algos[i++] = get_star_params(self);

	if (self->in_pix_fmt != INTERNAL_FORMAT)
		self->algos[i++] = get_yuvc_params(self, IPP_YUV_422ILE, INTERNAL_FORMAT);

	self->algos[i++] = get_crcbs_params(self);
	self->algos[i++] = get_eenf_params(self);
	self->algos[i++] = get_yuvc_params(self, INTERNAL_FORMAT, IPP_YUV_422ILE);
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

static void ipp_buffer_begin(GstDspIpp *self)
{
	unsigned i;
	dmm_buffer_t **c = self->msg_ptr;

	for (i = 0; i < ARRAY_SIZE(self->msg_ptr); i++, c++) {
		if (!*c)
			continue;
		dmm_buffer_begin(*c, (*c)->size);
	}
}

static void ipp_buffer_end(GstDspIpp *self)
{
	unsigned i;
	dmm_buffer_t **c = self->msg_ptr;

	for (i = 0; i < ARRAY_SIZE(self->msg_ptr); i++, c++) {
		if (!*c)
			continue;
		dmm_buffer_end(*c, (*c)->size);
	}
}

struct xbf_msg_elem_2 {
	uint32_t size;
	uint32_t error_code;
};

static void got_message(GstDspBase *base, struct dsp_msg *msg)
{
	GstDspIpp *self = GST_DSP_IPP(base);
	int command_id = msg->cmd;
	int error_code = 0;
	dmm_buffer_t **msg_ptr = self->msg_ptr;

	ipp_buffer_end(self);

	if (msg_ptr[1]) {
		struct xbf_msg_elem_2 *msg_2;
		msg_2 = msg_ptr[1]->data;
		error_code = msg_2->error_code;
	}

	if (command_id == DFGM_FREE_BUFF) {
		send_processing_info_gstmessage(self, "ipp-stop-processing");
		du_port_t *p = base->ports[1];
		struct td_buffer *tb;

#ifdef OVERWRITE_INPUT_BUFFER
		if (!(self->nr_algos & 0x01)) {
			tb = self->out_buf_ptr;
		} else {
			tb = self->in_buf_ptr;
			tb->port = p;
		}
#else
		tb = self->out_buf_ptr;
#endif
		tb->data->len = base->output_buffer_size;
		async_queue_push(p->queue, tb);
	}

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
	case DFGM_CONTROL_PIPE_ACK:
		free_message_args(self);
		break;
	case DFGM_EVENT_ERROR:
		free_message_args(self);
		gstdsp_got_error(base, -1, "DFGM Event Error");
		base->done = TRUE;
		break;
	default:
		pr_warning(self, "unhandled command 0x%x", command_id);
		break;
	}

	switch (error_code) {
	case DFGM_ERROR_NONE:
		break;
	case DFGM_ERROR_CONTROL_PIPE:
	case DFGM_ERROR_QUEUE_BUFF:
	case DFGM_ERROR_CREATE_XBF_PIPE:
	case DFGM_ERROR_SET_ALGS:
	case DFGM_ERROR_CREATE_XBF:
		gstdsp_got_error(base, 0, "DFGM algo error");
		base->done = TRUE;
		break;
	default:
		gstdsp_got_error(base, 0, "DFGM unhandled error");
		base->done = TRUE;
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

	ipp_buffer_begin(self);

	if (id == DFGM_QUEUE_BUFF)
		send_processing_info_gstmessage(self, "ipp-start-processing");

	return dsp_send_message(base->dsp_handle, base->node, id,
				arg1 ? (uint32_t)arg1->map : 0,
				arg2 ? (uint32_t)arg2->map : 0);
}

static dmm_buffer_t *get_msg_2(GstDspIpp *self)
{
	struct xbf_msg_elem_2 *msg_2;
	dmm_buffer_t *tmp;

	tmp = ipp_calloc(self, sizeof(*msg_2), DMA_BIDIRECTIONAL);
	msg_2 = tmp->data;
	msg_2->size = sizeof(*msg_2);
	dmm_buffer_map(tmp);

	return tmp;
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

	return send_msg(self, DFGM_CREATE_XBF, b_arg_1, get_msg_2(self), b_plat_fxn_string);
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

#ifdef OVERWRITE_INPUT_BUFFER
	/*
	 * Star ports:
	 * 0: input
	 * 1: output
	 *
	 * Input buffer is also used for processing.Use above ports alternatively,
	 * and the first one should always be 1.
	 */
	for (i = 0; i < nr_algos - 1; i++) {
		*flt_graph->output_buf_distribution[i + 1].n = port;
		port = !port;
	}
#else
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
#endif

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

#ifdef OVERWRITE_INPUT_BUFFER
	arg_1->num_in_port = 2;
#else
	arg_1->num_in_port = (nr_algos == 2) ? 2 : 3;
#endif

	dmm_buffer_map(b_arg_1);

	return send_msg(self, DFGM_CREATE_XBF_PIPE, b_arg_1, get_msg_2(self), b_create_params);
}

static bool start_processing(GstDspIpp *self)
{
	return send_msg(self, DFGM_START_PROCESSING, NULL, get_msg_2(self), NULL);
}

struct control_pipe_msg_elem_1 {
	uint32_t size;
	struct {
		uint32_t alg_inst;
		uint32_t control_cmd;
		uint32_t dyn_params_ptr;
		uint32_t status_ptr;
	} control_tables[MAX_ALGS];
};

struct control_pipe_msg_elem_2 {
	uint32_t size;
	uint32_t error_code;
	struct {
		uint32_t command_error_code;
	} error_tables[MAX_ALGS];
};

static bool control_pipe(GstDspIpp *self)
{
	struct control_pipe_msg_elem_1 *msg_1;
	struct control_pipe_msg_elem_2 *msg_2;
	dmm_buffer_t *b_msg_1;
	dmm_buffer_t *b_msg_2;
	size_t tbl_size;
	int i;
	int eenf_idx;
	int nr_algos = self->nr_algos;

	/*
	 * If the input format is YUV420p, YUV422i to YUV420p conversion
	 * algorithm will not present in the ipp pipeline. In that case
	 * position of eenf in the pipeline is 2. Otherwise eenf positiom is 3.
	 */
	if (self->in_pix_fmt == INTERNAL_FORMAT)
		eenf_idx = 2;
	else
		eenf_idx = 3;

	b_msg_1 = ipp_calloc(self, sizeof(*msg_1), DMA_TO_DEVICE);
	msg_1 = b_msg_1->data;
	tbl_size = (sizeof(msg_1->control_tables) / MAX_ALGS) * nr_algos;
	msg_1->size = sizeof(uint32_t) + tbl_size;

	for (i = 0; i < nr_algos; i++) {
		msg_1->control_tables[i].alg_inst = i;
		msg_1->control_tables[i].control_cmd = -1;

		if (i == eenf_idx) {
			msg_1->control_tables[i].control_cmd = 1;
			msg_1->control_tables[i].dyn_params_ptr = (uint32_t)self->dyn_params->map;
			msg_1->control_tables[i].status_ptr = (uint32_t)self->status_params->map;
		}
	}

	dmm_buffer_map(b_msg_1);

	b_msg_2 = ipp_calloc(self, sizeof(*msg_2), DMA_TO_DEVICE);
	msg_2 = b_msg_2->data;
	tbl_size = (sizeof(msg_2->error_tables) / MAX_ALGS) * nr_algos;
	msg_2->size = 2 * sizeof(uint32_t) + tbl_size;
	dmm_buffer_map(b_msg_2);

	return send_msg(self, DFGM_CONTROL_PIPE, b_msg_1, b_msg_2, NULL);
}

struct queue_buff_msg_elem_1 {
	uint32_t size;
	uint32_t content_type;
	uint32_t port_num;
	uint32_t algo_index;
	uint32_t content_ptr;
	uint32_t reuse_allowed_flag;
	uint32_t content_size_used;
	uint32_t content_size;
	uint32_t process_status;
	uint32_t next_content_ptr;
};

static bool queue_buffer(GstDspIpp *self, struct td_buffer *tb)
{
	GstDspBase *base = GST_DSP_BASE(self);
	struct queue_buff_msg_elem_1 *queue_msg1;
	struct queue_buff_msg_elem_1 *msg_elem_list;
	dmm_buffer_t *msg_elem_array;
	int32_t cur_idx = 0;
	int i = 0;
	du_port_t *port;
	int nr_algos = self->nr_algos;
	int nr_buffers;
	int nr_msgs;

#ifdef OVERWRITE_INPUT_BUFFER
	nr_buffers = 2;
#else
	nr_buffers = (nr_algos == 2) ? 2 : 3;
#endif

	nr_msgs = nr_algos * 2 + nr_buffers;
	msg_elem_array = ipp_calloc(self, nr_msgs * sizeof(*msg_elem_list), DMA_BIDIRECTIONAL);
	msg_elem_list = msg_elem_array->data;
	dmm_buffer_map(msg_elem_array);

	queue_msg1 = &msg_elem_list[cur_idx];

	for (i = 0; i < nr_buffers; i++) {
		queue_msg1->size = sizeof(*queue_msg1);
		queue_msg1->content_type = CONTENT_TYPE_BUFFER;
		queue_msg1->port_num = i;
		queue_msg1->reuse_allowed_flag = 0;
		if (i == 0) {
			self->in_buf_ptr = tb;
			queue_msg1->content_size_used = base->input_buffer_size;
			queue_msg1->content_size = base->input_buffer_size;
			queue_msg1->content_ptr = (uint32_t)tb->data->map;
		} else if (i == 1) {
			port = base->ports[1];
			dmm_buffer_map(port->buffers[0].data);
			self->out_buf_ptr = &port->buffers[0];
			queue_msg1->content_size_used = base->output_buffer_size;
			queue_msg1->content_size = base->output_buffer_size;
			queue_msg1->content_ptr = (uint32_t)self->out_buf_ptr->data->map;
		} else {
			dmm_buffer_t *b = ipp_calloc(self, base->input_buffer_size, DMA_TO_DEVICE);
			dmm_buffer_map(b);
			self->intermediate_buf = b;
			queue_msg1->content_size_used = base->input_buffer_size;
			queue_msg1->content_size = base->input_buffer_size;
			queue_msg1->content_ptr = (uint32_t)b->map;
		}
		cur_idx++;
		queue_msg1->next_content_ptr = (uint32_t)((char *)msg_elem_array->map) +
			(cur_idx)*sizeof(*msg_elem_list);
		queue_msg1 = &msg_elem_list[cur_idx];
	}

	for (i = 0; i < nr_algos; i++) {
		queue_msg1->size = sizeof(*queue_msg1);
		queue_msg1->content_type = CONTENT_TYPE_IN_ARGS;
		queue_msg1->algo_index = i;
		queue_msg1->content_size_used = self->algos[i]->in->size;
		queue_msg1->content_size = queue_msg1->content_size_used;
		queue_msg1->content_ptr = (uint32_t)self->algos[i]->in->map;
		cur_idx++;
		queue_msg1->next_content_ptr = (uint32_t)((char *)msg_elem_array->map) +
			(cur_idx)*sizeof(*msg_elem_list);
		queue_msg1 = &msg_elem_list[cur_idx];
	}

	for (i = 0; i < nr_algos; i++) {
		queue_msg1->size = sizeof(*queue_msg1);
		queue_msg1->content_type = CONTENT_TYPE_OUT_ARGS;
		queue_msg1->algo_index = i;
		queue_msg1->content_size_used = self->algos[i]->out->size;
		queue_msg1->content_size = queue_msg1->content_size_used;
		queue_msg1->content_ptr = (uint32_t)self->algos[i]->out->map;
		cur_idx++;

		if (i == nr_algos - 1) {
			queue_msg1->next_content_ptr = 0;
		} else {
			queue_msg1->next_content_ptr = (uint32_t)((char *)msg_elem_array->map) +
				(cur_idx)*sizeof(*msg_elem_list);
			queue_msg1 = &msg_elem_list[cur_idx];
		}
	}

	return send_msg(self, DFGM_QUEUE_BUFF, msg_elem_array, NULL, NULL);
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

	return send_msg(self, DFGM_STOP_PROCESSING, b_arg_1, get_msg_2(self), NULL);
}

static bool destroy_pipe(GstDspIpp *self)
{
	return send_msg(self, DFGM_DESTROY_XBF_PIPE, NULL, get_msg_2(self), NULL);
}

static bool clear_algorithm(GstDspIpp *self)
{
	return send_msg(self, DFGM_CLEAR_XBF_ALGS, NULL, get_msg_2(self), NULL);
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

	return send_msg(self, DFGM_DESTROY_XBF, b_arg_1, get_msg_2(self), NULL);
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

/* Dynamic parameters for eenf */

struct algo_buf_info {
	uint32_t min_num_in_bufs;
	uint32_t min_num_out_bufs;
	uint32_t min_in_buf_size[MAX_ALGS];
	uint32_t min_out_buf_size[MAX_ALGS];
};

struct algo_status {
	uint32_t status;
	uint32_t extended_error;
	struct algo_buf_info bufInfo;
};

static struct ipp_eenf_params eenf_normal = {
	.edge_enhancement_strength = 110,
	.weak_edge_threshold = 30,
	.strong_edge_threshold = 90,
	.low_freq_luma_noise_filter_strength = 7,
	.mid_freq_luma_noise_filter_strength = 14,
	.high_freq_luma_noise_filter_strength = 28,
	.low_freq_cb_noise_filter_strength = 8,
	.mid_freq_cb_noise_filter_strength = 16,
	.high_freq_cb_noise_filter_strength = 32,
	.low_freq_cr_noise_filter_strength = 8,
	.mid_freq_cr_noise_filter_strength = 16,
	.high_freq_cr_noise_filter_strength = 32,
	.shading_vert_param_1 = 10,
	.shading_vert_param_2 = 400,
	.shading_horz_param_1 = 10,
	.shading_horz_param_2 = 400,
	.shading_gain_scale = 128,
	.shading_gain_offset = 2048,
	.shading_gain_max_value = 16384,
	.ratio_downsample_cb_cr = 1,
};

static struct ipp_eenf_params eenf_aggressive = {
	.edge_enhancement_strength = 170,
	.weak_edge_threshold = 50,
	.strong_edge_threshold = 300,
	.low_freq_luma_noise_filter_strength = 30,
	.mid_freq_luma_noise_filter_strength = 80,
	.high_freq_luma_noise_filter_strength = 20,
	.low_freq_cb_noise_filter_strength = 60,
	.mid_freq_cb_noise_filter_strength = 40,
	.high_freq_cb_noise_filter_strength = 30,
	.low_freq_cr_noise_filter_strength = 50,
	.mid_freq_cr_noise_filter_strength = 30,
	.high_freq_cr_noise_filter_strength = 20,
	.shading_vert_param_1 = 1,
	.shading_vert_param_2 = 800,
	.shading_horz_param_1 = 1,
	.shading_horz_param_2 = 800,
	.shading_gain_scale = 128,
	.shading_gain_offset = 4096,
	.shading_gain_max_value = 32767,
	.ratio_downsample_cb_cr = 4,
};

static void
get_eenf_dyn_params(GstDspIpp *self)
{
	dmm_buffer_t *tmp;
	size_t size;
	struct ipp_eenf_params *params;

	switch (self->eenf_strength) {
	case NOISE_FILTER_CUSTOM:
		pr_debug(self, "custom noise filter parameters");
		params = &self->eenf_params;
		break;
	case NOISE_FILTER_NORMAL:
		pr_debug(self, "normal noise filter parameters");
		params = &eenf_normal;
		break;
	case NOISE_FILTER_AGGRESSIVE:
		pr_debug(self, "aggerssive noise filter parameters");
		params = &eenf_aggressive;
		break;
	default:
		return;
	}

	params->size = sizeof(*params);
	params->in_place = 0;

	tmp = ipp_calloc(self, sizeof(*params), DMA_TO_DEVICE);
	memcpy(tmp->data, params, sizeof(*params));
	dmm_buffer_map(tmp);
	self->dyn_params = tmp;

	size = sizeof(struct algo_status);
	self->status_params = ipp_calloc(self, size, DMA_BIDIRECTIONAL);
	dmm_buffer_map(self->status_params);
}

static bool send_buffer(GstDspBase *base, struct td_buffer *tb)
{
	GstDspIpp *self = GST_DSP_IPP(base);
	bool ok;

	/* no need to send output buffer to dsp */
	if (tb->port->id == 1)
		return true;

	if (base->dsp_error)
		return false;

	send_processing_info_gstmessage(self, "ipp-start-init");

	get_eenf_dyn_params(self);
	ok = control_pipe(self);
	if (!ok)
		return ok;

	dmm_buffer_map(tb->data);

	return queue_buffer(GST_DSP_IPP(base), tb);
}

static bool send_play_message(GstDspBase *base)
{
	return true;
};

static void reset(GstDspBase *base)
{
	GstDspIpp *self = GST_DSP_IPP(base);

	self->msg_sem->count = 1;

	for (unsigned i = 0; i < self->nr_algos; i++) {
		struct ipp_algo *algo = self->algos[i];
		if (!algo)
			continue;
		dmm_buffer_free(algo->create_params);
		dmm_buffer_free(algo->b_algo_fxn);
		dmm_buffer_free(algo->b_dma_fxn);
		dmm_buffer_free(algo->in);
		dmm_buffer_free(algo->out);
		free(algo);
		self->algos[i] = NULL;
	}

	dmm_buffer_free(self->flt_graph);
	self->flt_graph = NULL;
	dmm_buffer_free(self->intermediate_buf);
	self->intermediate_buf = NULL;
	dmm_buffer_free(self->dyn_params);
	self->dyn_params = NULL;
	dmm_buffer_free(self->status_params);
	self->status_params = NULL;
}

static bool send_stop_message(GstDspBase *base)
{
	GstDspIpp *self = GST_DSP_IPP(base);
	bool ok = true;

	if (base->dsp_error)
		goto leave;

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

leave:
	return ok;
}

static inline GstCaps *generate_sink_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();
	struc = gst_structure_new("video/x-raw-yuv", "format",
				  GST_TYPE_FOURCC, GST_MAKE_FOURCC('U', 'Y', 'V', 'Y'), NULL);
	gst_caps_append_structure(caps, struc);

#if SN_API > 0
	struc = gst_structure_new("video/x-raw-yuv", "format",
				  GST_TYPE_FOURCC, GST_MAKE_FOURCC('I', '4', '2', '0'), NULL);
	gst_caps_append_structure(caps, struc);
#endif

	return caps;
}

static inline GstCaps *generate_src_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();
	struc = gst_structure_new("video/x-raw-yuv", "format",
				  GST_TYPE_FOURCC, GST_MAKE_FOURCC('U', 'Y', 'V', 'Y'), NULL);
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
	unsigned int format;
	const GValue *framerate;

	self = GST_DSP_IPP(GST_PAD_PARENT(pad));
	base = GST_DSP_BASE(self);

	in_struc = gst_caps_get_structure(caps, 0);
	out_caps = gst_caps_new_empty();
	out_struc = gst_structure_new("video/x-raw-yuv", NULL);

	if (gst_structure_get_int(in_struc, "width", &width))
		gst_structure_set(out_struc, "width", G_TYPE_INT, width, NULL);

	if (gst_structure_get_int(in_struc, "height", &height))
		gst_structure_set(out_struc, "height", G_TYPE_INT, height, NULL);

	if (width > MAX_WIDTH || width & 0x0F) {
		gstdsp_got_error(base, 0, "invalid width value");
		return FALSE;
	} else if (height > MAX_HEIGHT || height & 0x07) {
		gstdsp_got_error(base, 0, "invalid height value");
		return FALSE;
	} else if (width * height > MAX_TOTAL_PIXEL) {
		gstdsp_got_error(base, 0, "Total number of pixels exceeding the limit");
		return FALSE;
	}

	gst_structure_get_fourcc(in_struc, "format", &format);

	/* ipp output colour format is always UYVY */
	gst_structure_set(out_struc, "format", GST_TYPE_FOURCC,
			GST_MAKE_FOURCC('U', 'Y', 'V', 'Y'), NULL);

	framerate = gst_structure_get_value(in_struc, "framerate");
	if (framerate)
		gst_structure_set_value(out_struc, "framerate", framerate);

	switch (format) {
	case GST_MAKE_FOURCC('U', 'Y', 'V', 'Y'):
		self->in_pix_fmt = IPP_YUV_422ILE;
		base->input_buffer_size = width * height * 2;
		break;
	case GST_MAKE_FOURCC('I', '4', '2', '0'):
		self->in_pix_fmt = IPP_YUV_420P;
		base->input_buffer_size = (width * height * 3) / 2;
		break;
	default:
		pr_err(self, "unsupported colour format");
		return FALSE;
	}

	base->output_buffer_size = width * height * 2;
	self->width = width;
	self->height = height;

	gst_caps_append_structure(out_caps, out_struc);

	if (!gst_pad_take_caps(base->srcpad, out_caps))
		return FALSE;

	du_port_alloc_buffers(base->ports[0], 1);
	du_port_alloc_buffers(base->ports[1], 1);

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

static gboolean sink_event(GstDspBase *base, GstEvent *event)
{
	GstDspIpp *self = GST_DSP_IPP(base);
	const GstStructure *structure;

	structure = gst_event_get_structure(event);
	if (!structure)
		goto leave;

	switch (GST_EVENT_TYPE(event)) {
	case GST_EVENT_CUSTOM_DOWNSTREAM: {
		unsigned tmp;
		struct ipp_eenf_params *param = &self->eenf_params;

		if (!gst_structure_has_name(structure, "application/x-gst-ipp"))
			break;

		pr_info(self, "custom eenf params received");

		if (gst_structure_get_uint(structure, "edge-enhancement-strength", &tmp))
			param->edge_enhancement_strength = tmp;

		if (gst_structure_get_uint(structure, "weak-edge-threshold", &tmp))
			param->weak_edge_threshold = tmp;

		if (gst_structure_get_uint(structure, "strong-edge-threshold", &tmp))
			param->strong_edge_threshold = tmp;

		if (gst_structure_get_uint(structure, "luma-noise-filter-strength-low", &tmp))
			param->low_freq_luma_noise_filter_strength = tmp;

		if (gst_structure_get_uint(structure, "luma-noise-filter-strength-mid", &tmp))
			param->mid_freq_luma_noise_filter_strength = tmp;

		if (gst_structure_get_uint(structure, "luma-noise-filter-strength-high", &tmp))
			param->high_freq_luma_noise_filter_strength = tmp;

		if (gst_structure_get_uint(structure, "cb-noise-filter-strength-low", &tmp))
			param->low_freq_cb_noise_filter_strength = tmp;

		if (gst_structure_get_uint(structure, "cb-noise-filter-strength-mid", &tmp))
			param->mid_freq_cb_noise_filter_strength = tmp;

		if (gst_structure_get_uint(structure, "cb-noise-filter-strength-high", &tmp))
			param->high_freq_cb_noise_filter_strength = tmp;

		if (gst_structure_get_uint(structure, "cr-noise-filter-strength-low", &tmp))
			param->low_freq_cr_noise_filter_strength = tmp;

		if (gst_structure_get_uint(structure, "cr-noise-filter-strength-mid", &tmp))
			param->mid_freq_cr_noise_filter_strength = tmp;

		if (gst_structure_get_uint(structure, "cr-noise-filter-strength-high", &tmp))
			param->high_freq_cr_noise_filter_strength = tmp;

		if (gst_structure_get_uint(structure, "shading-vert-param1", &tmp))
			param->shading_vert_param_1 = tmp;

		if (gst_structure_get_uint(structure, "shading-vert-param2", &tmp))
			param->shading_vert_param_2 = tmp;

		if (gst_structure_get_uint(structure, "shading-horz-param1", &tmp))
			param->shading_horz_param_1 = tmp;

		if (gst_structure_get_uint(structure, "shading-horz-param2", &tmp))
			param->shading_horz_param_2 = tmp;

		if (gst_structure_get_uint(structure, "shading-gain-scale", &tmp))
			param->shading_gain_scale = tmp;

		if (gst_structure_get_uint(structure, "shading-gain-offset", &tmp))
			param->shading_gain_offset = tmp;

		if (gst_structure_get_uint(structure, "shading-gain-maxvalue", &tmp))
			param->shading_gain_max_value = tmp;

		if (gst_structure_get_uint(structure, "ratio-downsample-cb-cr", &tmp))
			param->ratio_downsample_cb_cr = tmp;

		gst_event_unref(event);

		return true;
	}
	default:
		break;
	}

leave:
	return parent_class->sink_event(base, event);
}

static void send_processing_info_gstmessage(GstDspIpp *self, const gchar* info)
{
	GstStructure *s;
	GstMessage *msg;

	s = gst_structure_new(info, NULL);
	msg = gst_message_new_element(GST_OBJECT(self), s);
	pr_debug(self, "Sending message : %s", info);

	if (gst_element_post_message(GST_ELEMENT(self), msg) == FALSE)
		pr_warning(self, "Element has no bus, no message sent");
}

static void
set_property(GObject *obj,
	     guint prop_id,
	     const GValue *value,
	     GParamSpec *pspec)
{
	GstDspIpp *self = GST_DSP_IPP(obj);

	switch (prop_id) {
	case PROP_NOISE_FILTER_STRENGTH:
		self->eenf_strength = g_value_get_enum(value);
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
	GstDspIpp *self = GST_DSP_IPP(obj);

	switch (prop_id) {
	case PROP_NOISE_FILTER_STRENGTH:
		g_value_set_enum(value, self->eenf_strength);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
		break;
	}
}

static void instance_init(GTypeInstance *instance, gpointer g_class)
{
	GstDspBase *base = GST_DSP_BASE(instance);
	GstDspIpp *self = GST_DSP_IPP(instance);

	base->got_message = got_message;
	base->send_buffer = send_buffer;
	base->send_play_message = send_play_message;
	base->send_stop_message = send_stop_message;
	base->reset = reset;
	self->msg_sem = g_sem_new(1);

	/* initialize params to normal strength */
	memcpy(&self->eenf_params, &eenf_normal, sizeof(eenf_normal));

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
	GstDspBaseClass *gstdspbase_class;

	gobject_class = G_OBJECT_CLASS(g_class);
	gstdspbase_class = GST_DSP_BASE_CLASS(g_class);

	gobject_class->set_property = set_property;
	gobject_class->get_property = get_property;

	g_object_class_install_property(gobject_class, PROP_NOISE_FILTER_STRENGTH,
			g_param_spec_enum("noise-filter-strength", "Noise filter strength",
				"Specifies the strength of the noise filter",
				GST_TYPE_IPP_NOISE_FILTER_STRENGTH,
				DEFAULT_NOISE_FILTER_STRENGTH,
				G_PARAM_READWRITE));

	parent_class = g_type_class_peek_parent(g_class);
	gobject_class->finalize = finalize;
	gstdspbase_class->sink_event = sink_event;
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
