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

static bool send_stop_message(GstDspBase *base);

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

enum {
	YUV_I_TO_P,
	YUV_P_TO_I,
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
get_yuvc_params(GstDspIpp *self, int alg_id)
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

	if (alg_id == YUV_I_TO_P) {
		in_args->input_chroma_format = IPP_YUV_422ILE;
		in_args->output_chroma_format = IPP_YUV_420P;
	}
	else {
		in_args->input_chroma_format = IPP_YUV_420P;
		in_args->output_chroma_format = IPP_YUV_422ILE;
	}

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
	in_args->input_chroma_format = IPP_YUV_420P;
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
	in_args->input_chroma_format = IPP_YUV_420P;
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
	self->algos[i++] = get_yuvc_params(self, YUV_I_TO_P);
	self->algos[i++] = get_crcbs_params(self);
	self->algos[i++] = get_eenf_params(self);
	self->algos[i++] = get_yuvc_params(self, YUV_P_TO_I);
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
	struct xbf_msg_elem_2 *msg_2;
	int error_code = DFGM_ERROR_NONE;
	dmm_buffer_t **msg_ptr = self->msg_ptr;

	ipp_buffer_end(self);

	if (msg_ptr[1]) {
		msg_2 = msg_ptr[1]->data;
		error_code = msg_2->error_code;
		base->dsp_error = error_code;
	}

	if (command_id == DFGM_FREE_BUFF) {
		du_port_t *p = base->ports[1];

		/* push the output buffer in to the queue. */
		self->out_buf_ptr->len = base->output_buffer_size;
		async_queue_push(p->queue, self->out_buf_ptr);
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
		free_message_args(self);
		break;
	case DFGM_EVENT_ERROR:
		free_message_args(self);
		send_stop_message(base);
		gstdsp_got_error(base, base->dsp_error, "DFGM Event Error");
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
		send_stop_message(base);
		gstdsp_got_error(base, base->dsp_error, "DFGM algo error");
		base->done = TRUE;
		break;
	default:
		send_stop_message(base);
		gstdsp_got_error(base, base->dsp_error, "DFGM unhandled error");
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
	arg_1->num_in_port = (nr_algos == 2) ? 2 : 3;
	dmm_buffer_map(b_arg_1);

	return send_msg(self, DFGM_CREATE_XBF_PIPE, b_arg_1, get_msg_2(self), b_create_params);
}

static bool start_processing(GstDspIpp *self)
{
	return send_msg(self, DFGM_START_PROCESSING, NULL, get_msg_2(self), NULL);
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

static bool queue_buffer(GstDspIpp *self, dmm_buffer_t *in_buffer, int id)
{
	GstDspBase *base = GST_DSP_BASE(self);
	struct queue_buff_msg_elem_1 *queue_msg1;
	struct queue_buff_msg_elem_1 *msg_elem_list;
	dmm_buffer_t *msg_elem_array;
	int32_t cur_idx = 0;
	int i = 0 ;
	du_port_t *port;
	int nr_algos = self->nr_algos;
	int nr_buffers;
	int nr_msgs;

	nr_buffers = (nr_algos == 2) ? 2 : 3;
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
			queue_msg1->content_size_used = base->input_buffer_size;
			queue_msg1->content_size = base->input_buffer_size;
			queue_msg1->content_ptr = (uint32_t)in_buffer->map;
		} else if (i == 1) {
			port = base->ports[1];
			dmm_buffer_map(port->buffers[0]);
			self->out_buf_ptr = port->buffers[0];
			queue_msg1->content_size_used = base->output_buffer_size;
			queue_msg1->content_size = base->output_buffer_size;
			queue_msg1->content_ptr = (uint32_t)self->out_buf_ptr->map;
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
			queue_msg1->next_content_ptr = 0 ;
		}
		else {
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

static bool send_buffer(GstDspBase *base, dmm_buffer_t *b, guint id)
{
	/* no need to send output buffer to dsp */
	if (id == 1)
		return true;

	dmm_buffer_map(b);

	return queue_buffer(GST_DSP_IPP(base), b, id);
}

static bool send_play_message(GstDspBase *base)
{
	return true;
};

static bool send_stop_message(GstDspBase *base)
{
	GstDspIpp *self = GST_DSP_IPP(base);
	bool ok = true;
	unsigned i;

	if (base->dsp_error)
		goto cleanup;

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

cleanup:
	for (i = 0; i < self->nr_algos; i++) {
		struct ipp_algo *algo = self->algos[i];
		if (!algo)
			break;
		dmm_buffer_free(algo->create_params);
		dmm_buffer_free(algo->b_algo_fxn);
		dmm_buffer_free(algo->b_dma_fxn);
		dmm_buffer_free(algo->in);
		dmm_buffer_free(algo->out);
		free(algo);
		self->algos[i] = NULL;
	}

	if (self->flt_graph) {
		dmm_buffer_free(self->flt_graph);
		self->flt_graph = NULL;
	}

	if (self->intermediate_buf) {
		dmm_buffer_free(self->intermediate_buf);
		self->intermediate_buf = NULL;
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

	base->input_buffer_size = width * height * 2;
	base->output_buffer_size = width * height * 2;
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
