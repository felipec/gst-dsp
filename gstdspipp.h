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

#ifndef GST_DSP_IPP_H
#define GST_DSP_IPP_H

#include "gstdspbase.h"
#include "sem.h"

G_BEGIN_DECLS

#define GST_DSP_IPP(obj) (GstDspIpp *)(obj)
#define GST_DSP_IPP_TYPE (gst_dsp_ipp_get_type())
#define GST_DSP_IPP_CLASS(obj) (GstDspIppClass *)(obj)

#define IPP_MAX_NUM_OF_ALGOS 5

typedef struct GstDspIpp GstDspIpp;
typedef struct GstDspIppClass GstDspIppClass;

struct ipp_algo {
	dmm_buffer_t *create_params;
	const char *fxn;
	const char *dma_fxn;
	dmm_buffer_t *in;
	dmm_buffer_t *out;

	/* TODO no need to keep these around */
	dmm_buffer_t *b_algo_fxn;
	dmm_buffer_t *b_dma_fxn;
};

struct ipp_eenf_params {
	uint32_t size;
	int16_t in_place;
	int16_t edge_enhancement_strength;
	int16_t weak_edge_threshold;
	int16_t strong_edge_threshold;
	int16_t low_freq_luma_noise_filter_strength;
	int16_t mid_freq_luma_noise_filter_strength;
	int16_t high_freq_luma_noise_filter_strength;
	int16_t low_freq_cb_noise_filter_strength;
	int16_t mid_freq_cb_noise_filter_strength;
	int16_t high_freq_cb_noise_filter_strength;
	int16_t low_freq_cr_noise_filter_strength;
	int16_t mid_freq_cr_noise_filter_strength;
	int16_t high_freq_cr_noise_filter_strength;
	int16_t shading_vert_param_1;
	int16_t shading_vert_param_2;
	int16_t shading_horz_param_1;
	int16_t shading_horz_param_2;
	int16_t shading_gain_scale;
	int16_t shading_gain_offset;
	int16_t shading_gain_max_value;
	int16_t ratio_downsample_cb_cr;
};

struct GstDspIpp {
	GstDspBase element;
	int width, height;
	int in_pix_fmt;
	struct ipp_algo *algos[IPP_MAX_NUM_OF_ALGOS];
	unsigned nr_algos;
	GSem *msg_sem;
	struct ipp_eenf_params eenf_params;
	int eenf_strength;

	dmm_buffer_t *msg_ptr[3];
	dmm_buffer_t *flt_graph;
	struct td_buffer *in_buf_ptr;
	struct td_buffer *out_buf_ptr;
	dmm_buffer_t *intermediate_buf;
	dmm_buffer_t *dyn_params;
	dmm_buffer_t *status_params;
};

struct GstDspIppClass {
	GstDspBaseClass parent_class;
};

GType gst_dsp_ipp_get_type(void);

G_END_DECLS

#endif /* GST_DSP_IPP_H */
