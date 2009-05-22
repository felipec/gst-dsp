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

#include "gstdspmp4vdec.h"
#include "plugin.h"

#include "dsp_bridge.h"

#include <string.h> /* for memcpy */

#include "log.h"

#define GST_CAT_DEFAULT gstdsp_debug

static inline bool
send_buffer(GstDspMp4vDec *self,
	    dmm_buffer_t *buffer,
	    unsigned int id,
	    size_t len);

static inline void
map_buffer(GstDspMp4vDec *self,
	   GstBuffer *g_buf,
	   dmm_buffer_t *d_buf);

static inline du_port_t *
du_port_new(guint buffer_count)
{
	du_port_t *p;
	p = calloc(1, sizeof(*p));

	p->buffer_count = buffer_count;
	p->sem = g_sem_new(0);

	return p;
}

static inline void
du_port_free(du_port_t *p)
{
	if (!p)
		return;

	g_sem_free(p->sem);

	free(p);
}

static inline void
g_sem_down_status(GSem *sem,
		  const GstFlowReturn *status)
{
	GstFlowReturn ret;
	g_mutex_lock(sem->mutex);

	while (sem->count == 0 &&
	       (ret = g_atomic_int_get(status)) == GST_FLOW_OK)
		g_cond_wait(sem->condition, sem->mutex);

	if (ret == GST_FLOW_OK)
		sem->count--;

	g_mutex_unlock(sem->mutex);
}

static inline void
g_sem_signal(GSem *sem)
{
	g_mutex_lock(sem->mutex);
	g_cond_signal(sem->condition);
	g_mutex_unlock(sem->mutex);
}

static inline void
g_sem_reset(GSem *sem,
	    guint count)
{
	g_mutex_lock(sem->mutex);
	sem->count = count;
	g_mutex_unlock(sem->mutex);
}

typedef struct
{
	uint32_t buffer_data;
	uint32_t buffer_size;
	uint32_t param_data;
	uint32_t param_size;
	uint32_t buffer_len;
	uint32_t silly_eos;
	uint32_t silly_buf_state;
	uint32_t silly_buf_active;
	uint32_t silly_buf_id;
	uint32_t reserved;
	uint32_t silly_arm_arg;
	uint32_t silly_arm_buffer_arg;
	uint32_t silly_arm_param_arg;
	uint32_t silly_out_buffer_index;
	uint32_t silly_in_buffer_index;
	uint32_t user_data;
	uint32_t stream_id;
} dsp_comm_t;

static GstElementClass *parent_class;

static inline GstCaps *
generate_sink_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();

	struc = gst_structure_new("video/mpeg",
				  "mpegversion", G_TYPE_INT, 4,
				  "systemstream", G_TYPE_BOOLEAN, FALSE,
				  NULL);

	gst_caps_append_structure(caps, struc);

	struc = gst_structure_new("video/x-divx",
				  "divxversion", GST_TYPE_INT_RANGE, 4, 5,
				  NULL);

	gst_caps_append_structure(caps, struc);

	struc = gst_structure_new("video/x-xvid",
				  NULL);

	gst_caps_append_structure(caps, struc);

	struc = gst_structure_new("video/x-3ivx",
				  NULL);

	gst_caps_append_structure(caps, struc);

	struc = gst_structure_new("video/x-h263",
				  "variant", G_TYPE_STRING, "itu",
				  NULL);

	gst_caps_append_structure(caps, struc);

	return caps;
}

static inline GstCaps *
generate_src_template(void)
{
	GstCaps *caps;
	GstStructure *struc;

	caps = gst_caps_new_empty();

	struc = gst_structure_new("video/x-raw-yuv",
				  "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC('U', 'Y', 'V', 'Y'),
				  NULL);

	gst_caps_append_structure(caps, struc);

	return caps;
}

struct mp4vdec_args
{
	uint16_t num_streams;

	uint16_t in_id;
	uint16_t in_type;
	uint16_t in_count;

	uint16_t out_id;
	uint16_t out_type;
	uint16_t out_count;

	uint16_t reserved;

	uint32_t max_width;
	uint32_t max_height;
	uint32_t color_format;
	uint32_t max_framerate;
	uint32_t max_bitrate;
	uint32_t endianness;
	uint32_t profile;
	int32_t max_level;
	uint32_t mode;
	int32_t preroll;
	uint32_t display_width;
};

struct foo_data {
	unsigned long size;
	unsigned short data[42];
};

static inline void
got_message(GstDspMp4vDec *self,
	    dsp_msg_t *msg)
{
	uint32_t id;
	uint32_t command_id;

	id = msg->cmd & 0x000000ff;
	command_id = msg->cmd & 0xffffff00;

	switch (command_id) {
		case 0x0500:
			pr_debug(self, "got flush");
			if (id == 0)
				g_comp_done(self->flush);
			break;
		case 0x0600:
			{
				dmm_buffer_t *b;
				dmm_buffer_t *cur = self->port[id]->buffer;
				dsp_comm_t *msg_data = cur->data;

				pr_debug(self, "got %s buffer", id == 0 ? "input" : "output");

				b = (void *) msg_data->user_data;

				if (id == 1) {
					self->out_buffer = b;
					if (g_atomic_int_get(&self->status) == GST_FLOW_OK)
						g_sem_up(self->port[1]->sem);
				}
				else {
					if (g_atomic_int_get(&self->status) == GST_FLOW_OK)
						g_sem_up(self->port[0]->sem);
					if (b->user_data)
						gst_buffer_unref(b->user_data);
					dmm_buffer_free(b);
				}
			}
			break;
		default:
			pr_warning(self, "unhandled command: %u", command_id);
	}
}

static void
output_loop(gpointer data)
{
	GstPad *pad;
	GstDspMp4vDec *self;
	GstFlowReturn ret = GST_FLOW_OK;
	GstBuffer *new_buf, *out_buf;
	dmm_buffer_t *b;

	pad = data;
	self = GST_DSP_MP4VDEC(gst_pad_get_parent(pad));

	pr_debug(self, "begin");
	g_sem_down_status(self->port[1]->sem, &self->status);

	b = self->out_buffer;

	if (!b) {
		pr_err(self, "out_buffer = NULL!");
		ret = GST_FLOW_ERROR;
		goto leave;
	}

	ret = gst_pad_alloc_buffer_and_set_caps(self->srcpad,
						GST_BUFFER_OFFSET_NONE,
						self->output_buffer_size,
						GST_PAD_CAPS(self->srcpad),
						&new_buf);

	if (G_UNLIKELY(ret != GST_FLOW_OK)) {
		pr_err(self, "couldn't allocate buffer: %s", gst_flow_get_name(ret));
		goto leave;
	}

	dmm_buffer_invalidate(b, b->size);

	if (b->user_data) {
		out_buf = b->user_data;
		map_buffer(self, new_buf, b);
		if (b->need_copy) {
			pr_warning(self, "buffer not aligned: %p, %lu",
				   GST_BUFFER_DATA(new_buf),
				   (unsigned long) GST_BUFFER_DATA(new_buf) % b->alignment);
		}
	}
	else {
		out_buf = new_buf;

		if (b->need_copy) {
			pr_info(self, "copy");
			memcpy(GST_BUFFER_DATA(out_buf), b->data, b->size);
		}
	}

	g_mutex_lock(self->ts_mutex);
	if (self->ts == GST_CLOCK_TIME_NONE)
		pr_err(self, "dang!");
	GST_BUFFER_TIMESTAMP(out_buf) = self->ts;
#ifdef TS_COUNT
	self->ts_count--;
	if (self->ts_count > 2 || self->ts_count < 1)
		pr_info(self, "tsc=%lu", self->ts_count);
#endif
	g_mutex_unlock(self->ts_mutex);

	send_buffer(self, b, 1, 0);

	ret = gst_pad_push(self->srcpad, out_buf);
	if (G_UNLIKELY(ret != GST_FLOW_OK)) {
		pr_info(self, "pad push failed: %s", gst_flow_get_name(ret));
		goto leave;
	}

leave:
	if (ret != GST_FLOW_OK) {
		g_atomic_int_set(&self->status, ret);
		gst_pad_pause_task(self->srcpad);
	}

	pr_debug(self, "end");
}

gpointer
dsp_thread(gpointer data)
{
	GstDspMp4vDec *self = data;

	pr_info(self, "begin");

	while (!self->done) {
		unsigned int index = 0;
		pr_debug(self, "waiting for events");
		if (!dsp_wait_for_events(self->dsp_handle, self->events, 1, &index, 10000)) {
			pr_warning(self, "failed waiting for events");
			continue;
		}

		if (index == 0) {
			dsp_msg_t msg;
			while (true) {
				if (!dsp_node_get_message(self->dsp_handle, self->node, &msg, 100))
					break;
				pr_debug(self, "got dsp message: 0x%0x 0x%0x 0x%0x",
					 msg.cmd, msg.arg_1, msg.arg_2);
				got_message(self, &msg);
			}
		}
		else {
			pr_err(self, "wrong event index");
			g_atomic_int_set(&self->status, GST_FLOW_ERROR);
			goto leave;
		}
	}

leave:
	pr_info(self, "end");

	return NULL;
}

static inline void *
create_node(GstDspMp4vDec *self,
	    int dsp_handle,
	    void *proc)
{
	void *node;
	const dsp_uuid_t mp4v_dec_uuid = { 0x7e4b8541, 0x47a1, 0x11d6, 0xb1, 0x56,
		{ 0x00, 0xb0, 0xd0, 0x17, 0x67, 0x4b } };

	const dsp_uuid_t usn_uuid = { 0x79A3C8B3, 0x95F2, 0x403F, 0x9A, 0x4B,
		{ 0xCF, 0x80, 0x57, 0x73, 0x05, 0x41 } };

	const dsp_uuid_t ringio_uuid = { 0x47698bfb, 0xa7ee, 0x417e, 0xa6, 0x7a,
		{ 0x41, 0xc0, 0x27, 0x9e, 0xb8, 0x05 } };

	if (!dsp_register(dsp_handle, &ringio_uuid, DSP_DCD_LIBRARYTYPE, "/lib/dsp/ringio.dll64P")) {
		pr_err(self, "failed to register ringio node library");
		return NULL;
	}

	if (!dsp_register(dsp_handle, &usn_uuid, DSP_DCD_LIBRARYTYPE, "/lib/dsp/usn.dll64P")) {
		pr_err(self, "failed to register usn node library");
		return NULL;
	}

	if (!dsp_register(dsp_handle, &mp4v_dec_uuid, DSP_DCD_LIBRARYTYPE, "/lib/dsp/mp4vdec_sn.dll64P")) {
		pr_err(self, "failed to register mp4vdec node library");
		return NULL;
	}

	if (!dsp_register(dsp_handle, &mp4v_dec_uuid, DSP_DCD_NODETYPE, "/lib/dsp/mp4vdec_sn.dll64P")) {
		pr_err(self, "failed to register mp4vdec node");
		return NULL;
	}

	{
		struct dsp_node_attr_in attrs = {
			.cb = sizeof(attrs),
			.priority = 5,
			.timeout = 1000,
			.profile_id = 4,
			.heap_size = 0,
			.gpp_va = 0,
		};

		struct mp4vdec_args args = {
			.num_streams = 2,
			.in_id = 0,
			.in_type = 0,
			.in_count = self->port[0]->buffer_count,
			.out_id = 1,
			.out_type = 0,
			.out_count = self->port[1]->buffer_count,
			.max_width = 848,
			.max_height = 480,
			.color_format = 4,
			.max_framerate = 0,
			.max_bitrate = -1,
			.endianness = 1,
			.profile = 0,
			.max_level = -1,
			.mode = 0,
			.preroll = 0,
			.display_width = 0,
		};

		struct foo_data cb_data = {
			.size = sizeof(args),
		};

		memcpy(&cb_data.data, &args, sizeof(args));

		if (!dsp_node_allocate(dsp_handle, proc, &mp4v_dec_uuid, &cb_data, &attrs, &node)) {
			pr_err(self, "dsp node allocate failed");
			return NULL;
		}
	}

	if (!dsp_node_create(dsp_handle, node)) {
		pr_err(self, "dsp node create failed");
		return NULL;
	}

	pr_info(self, "dsp node created");

	return node;
}

static inline bool
destroy_node(GstDspMp4vDec *self,
	     int dsp_handle,
	     void *node)
{
	if (node) {
		if (!dsp_node_free(dsp_handle, node)) {
			pr_err(self, "dsp node free failed");
			return false;
		}

		pr_info(self, "dsp node deleted");
	}

	return true;
}

static gboolean
dsp_init(GstDspMp4vDec *self)
{
	int dsp_handle;
	guint i;

	self->dsp_handle = dsp_handle = dsp_open();

	if (dsp_handle < 0) {
		pr_err(self, "dsp open failed");
		return FALSE;
	}

	if (!dsp_attach(dsp_handle, 0, NULL, &self->proc)) {
		pr_err(self, "dsp attach failed");
		goto fail;
	}

	for (i = 0; i < 2; i++) {
		du_port_t *p = self->port[i];
		p->buffer = dmm_buffer_new(self->dsp_handle, self->proc);
		dmm_buffer_allocate(p->buffer, sizeof(dsp_comm_t));
	}

	self->node = create_node(self, dsp_handle, self->proc);
	if (!self->node) {
		pr_err(self, "dsp node creation failed");
		goto fail;
	}

	return TRUE;

fail:
	if (self->proc) {
		if (!dsp_detach(dsp_handle, self->proc)) {
			pr_err(self, "dsp detach failed");
		}
		self->proc = NULL;
	}

	if (self->dsp_handle >= 0) {
		if (dsp_close(dsp_handle) < 0) {
			pr_err(self, "dsp close failed");
		}
		self->dsp_handle = -1;
	}

	return FALSE;
}

static gboolean
dsp_deinit(GstDspMp4vDec *self)
{
	gboolean ret = TRUE;
	guint i;

	if (self->node) {
		if (!destroy_node(self, self->dsp_handle, self->node)) {
			pr_err(self, "dsp node destroy failed");
			ret = FALSE;
		}

		self->node = NULL;
	}

	for (i = 0; i < 2; i++) {
		du_port_t *p = self->port[i];
		dmm_buffer_free(p->buffer);
	}

	if (self->proc) {
		if (!dsp_detach(self->dsp_handle, self->proc)) {
			pr_err(self, "dsp detach failed");
			ret = FALSE;
		}
		self->proc = NULL;
	}

	if (self->dsp_handle >= 0) {
		if (dsp_close(self->dsp_handle) < 0) {
			pr_err(self, "dsp close failed");
			ret = FALSE;
		}
		self->dsp_handle = -1;
	}

	return ret;
}

static gboolean
dsp_start(GstDspMp4vDec *self)
{
	if (!dsp_node_run(self->dsp_handle, self->node)) {
		pr_err(self, "dsp node run failed");
		return FALSE;
	}

	pr_info(self, "dsp node running");

	self->events[0] = calloc(1, sizeof(struct dsp_notification));
	if (!dsp_node_register_notify(self->dsp_handle, self->node,
				      DSP_NODEMESSAGEREADY, 1,
				      self->events[0]))
	{
		pr_err(self, "failed to register for notifications");
		return false;
	}

	pr_info(self, "creating dsp thread");
	self->dsp_thread = g_thread_create(dsp_thread, self, TRUE, NULL);
	gst_pad_start_task(self->srcpad, output_loop, self->srcpad);

	/* play */
	dsp_send_message(self->dsp_handle, self->node, 0x0100, 0, 0);

	return TRUE;
}

static gboolean
dsp_stop(GstDspMp4vDec *self)
{
	unsigned long exit_status;

	/* stop */
	dsp_send_message(self->dsp_handle, self->node, 0x0200, 0, 0);

	g_thread_join(self->dsp_thread);
	gst_pad_pause_task(self->srcpad);

	if (!dsp_node_terminate (self->dsp_handle, self->node, &exit_status)) {
		pr_err(self, "dsp node terminate failed: 0x%lx", exit_status);
		return FALSE;
	}

	pr_info(self, "dsp node terminated");

	return TRUE;
}

static inline void
map_buffer(GstDspMp4vDec *self,
	   GstBuffer *g_buf,
	   dmm_buffer_t *d_buf)
{
	if (d_buf->alignment == 0||
	    (unsigned long) GST_BUFFER_DATA(g_buf) % d_buf->alignment == 0)
	{
		if (d_buf->data != GST_BUFFER_DATA(g_buf)) {
			dmm_buffer_unmap(d_buf);
			dmm_buffer_use(d_buf, GST_BUFFER_DATA(g_buf), GST_BUFFER_SIZE(g_buf));
		}
		d_buf->user_data = g_buf;
		return;
	}

	/* reallocate? */
	if (!d_buf->allocated_data ||
	    d_buf->size > GST_BUFFER_SIZE(g_buf)) {
		dmm_buffer_unmap(d_buf);
		dmm_buffer_allocate(d_buf, GST_BUFFER_SIZE(g_buf));
	}
	d_buf->need_copy = true;
}

static inline bool
send_buffer(GstDspMp4vDec *self,
	    dmm_buffer_t *buffer,
	    unsigned int id,
	    size_t len)
{
	dsp_comm_t *msg_data;
	dmm_buffer_t *tmp;

	pr_debug(self, "sending %s buffer", id == 0 ? "input" : "output");

	tmp = self->port[id]->buffer;
	msg_data = tmp->data;

	memset(msg_data, 0, sizeof(*msg_data));

	msg_data->buffer_data = (uint32_t) buffer->map;
	msg_data->buffer_size = buffer->size;
	msg_data->stream_id = id;
	msg_data->buffer_len = len;

	msg_data->user_data = (uint32_t) buffer;

	dmm_buffer_flush(tmp, sizeof(*msg_data));

	dsp_send_message(self->dsp_handle, self->node,
			 0x0600 | id, (uint32_t) tmp->map, 0);

	return true;
}

static GstStateChangeReturn
change_state(GstElement *element,
	     GstStateChange transition)
{
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
	GstDspMp4vDec *self;

	self = GST_DSP_MP4VDEC(element);

	pr_info(self, "%s -> %s",
		gst_element_state_get_name(GST_STATE_TRANSITION_CURRENT(transition)),
		gst_element_state_get_name(GST_STATE_TRANSITION_NEXT(transition)));

	switch (transition) {
		case GST_STATE_CHANGE_NULL_TO_READY:
			if (!dsp_init(self)) {
				pr_err(self, "dsp init failed");
				return GST_STATE_CHANGE_FAILURE;
			}

			break;

		case GST_STATE_CHANGE_READY_TO_PAUSED:
			if (!dsp_start(self)) {
				pr_err(self, "dsp start failed");
				return GST_STATE_CHANGE_FAILURE;
			}
			break;

		case GST_STATE_CHANGE_PAUSED_TO_READY:
			self->done = TRUE;
			g_atomic_int_set(&self->status, GST_FLOW_WRONG_STATE);
			g_sem_signal(self->port[0]->sem);
			g_sem_signal(self->port[1]->sem);
			break;

		default:
			break;
	}

	ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition) {
		case GST_STATE_CHANGE_PAUSED_TO_READY:
			if (!dsp_stop(self)) {
				pr_err(self, "dsp stop failed");
				return GST_STATE_CHANGE_FAILURE;
			}

			break;

		case GST_STATE_CHANGE_READY_TO_NULL:
			if (!dsp_deinit(self)) {
				pr_err(self, "dsp deinit failed");
				return GST_STATE_CHANGE_FAILURE;
			}

			break;

		default:
			break;
	}

	return ret;
}

static GstFlowReturn
pad_chain(GstPad *pad,
	  GstBuffer *buf)
{
	GstDspMp4vDec *self;
	dmm_buffer_t *d_buffer;
	GstFlowReturn ret = GST_FLOW_OK;

	self = GST_DSP_MP4VDEC(GST_OBJECT_PARENT(pad));

	pr_debug(self, "begin");

	if ((ret = g_atomic_int_get(&self->status)) != GST_FLOW_OK)
		goto leave;

	d_buffer = dmm_buffer_new(self->dsp_handle, self->proc);
	d_buffer->alignment = 0;
	map_buffer(self, buf, d_buffer);

	if (d_buffer->need_copy) {
		pr_info(self, "copy");
		memcpy(d_buffer->data, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
	}

	dmm_buffer_flush(d_buffer, GST_BUFFER_SIZE(buf));

	g_mutex_lock(self->ts_mutex);
	self->ts = GST_BUFFER_TIMESTAMP(buf);
#ifdef TS_COUNT
	self->ts_count++;
#endif
	g_mutex_unlock(self->ts_mutex);

	send_buffer(self, d_buffer, 0, GST_BUFFER_SIZE(buf));

	g_sem_down_status(self->port[0]->sem, &self->status);

leave:

	pr_debug(self, "end");

	return ret;
}

static inline void
setup_output_buffers(GstDspMp4vDec *self)
{
	dmm_buffer_t *b;
	guint i;

	for (i = 0; i < self->port[1]->buffer_count; i++) {
		GstBuffer *buf = NULL;

		gst_pad_alloc_buffer_and_set_caps(self->srcpad,
						  GST_BUFFER_OFFSET_NONE,
						  self->output_buffer_size,
						  GST_PAD_CAPS(self->srcpad),
						  &buf);

		b = dmm_buffer_new(self->dsp_handle, self->proc);

		if (G_LIKELY(buf)) {
			map_buffer(self, buf, b);
			if (b->need_copy) {
				pr_warning(self, "buffer not aligned: %p, %lu",
					   GST_BUFFER_DATA(buf),
					   (unsigned long) GST_BUFFER_DATA(buf) % b->alignment);
			}
		}
		else {
			dmm_buffer_allocate(b, self->output_buffer_size);
			b->need_copy = true;
		}

		send_buffer(self, b, 1, 0);
	}
}

static gboolean
sink_setcaps(GstPad *pad,
	     GstCaps *caps)
{
	GstDspMp4vDec *self;
	GstStructure *in_struc;
	GstCaps *out_caps;
	GstStructure *out_struc;

	self = GST_DSP_MP4VDEC(GST_PAD_PARENT(pad));

	{
		gchar *str = gst_caps_to_string(caps);
		pr_info(self, "sink caps: %s", str);
		g_free(str);
	}

	in_struc = gst_caps_get_structure(caps, 0);

	out_caps = gst_caps_new_empty();

	out_struc = gst_structure_new("video/x-raw-yuv",
				      "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC('U', 'Y', 'V', 'Y'),
				      NULL);

	{
		gint width = 0, height = 0;
		if (gst_structure_get_int(in_struc, "width", &width))
			gst_structure_set(out_struc, "width", G_TYPE_INT, width, NULL);
		if (gst_structure_get_int(in_struc, "height", &height))
			gst_structure_set(out_struc, "height", G_TYPE_INT, height, NULL);

		self->output_buffer_size = width * height * 2;
	}

	{
		const GValue *framerate = NULL;
		framerate = gst_structure_get_value(in_struc, "framerate");
		if (framerate)
			gst_structure_set_value(out_struc, "framerate", framerate);
	}

	gst_caps_append_structure(out_caps, out_struc);

	{
		gchar *str = gst_caps_to_string(out_caps);
		pr_info(self, "src caps: %s", str);
		g_free(str);
	}

	gst_pad_set_caps(self->srcpad, out_caps);

	setup_output_buffers(self);

	return gst_pad_set_caps(pad, caps);
}

static gboolean
pad_event(GstPad *pad,
	  GstEvent *event)
{
	GstDspMp4vDec *self;
	gboolean ret = TRUE;

	self = GST_DSP_MP4VDEC(GST_OBJECT_PARENT(pad));

	pr_info(self, "event: %s", GST_EVENT_TYPE_NAME(event));

	switch (GST_EVENT_TYPE(event)) {
		case GST_EVENT_FLUSH_START:
			ret = gst_pad_push_event(self->srcpad, event);
			g_atomic_int_set(&self->status, GST_FLOW_WRONG_STATE);
			self->out_buffer = NULL;

			g_mutex_lock(self->ts_mutex);
			self->ts = GST_CLOCK_TIME_NONE;
			g_mutex_unlock(self->ts_mutex);

			g_sem_signal(self->port[0]->sem);
			g_sem_signal(self->port[1]->sem);

			gst_pad_pause_task(self->srcpad);

			g_comp_init(self->flush);

			/* flush */
			dsp_send_message(self->dsp_handle, self->node, 0x0500 | 0, 5, 0);
			dsp_send_message(self->dsp_handle, self->node, 0x0500 | 1, 5, 0);

			break;

		case GST_EVENT_FLUSH_STOP:
			ret = gst_pad_push_event(self->srcpad, event);
			g_comp_wait(self->flush);
			g_atomic_int_set(&self->status, GST_FLOW_OK);

			g_sem_reset(self->port[1]->sem, 0);
			setup_output_buffers(self);

			gst_pad_start_task(self->srcpad, output_loop, self->srcpad);
			break;

		default:
			ret = gst_pad_push_event (self->srcpad, event);
			break;
	}

	return ret;
}

static void
instance_init(GTypeInstance *instance,
	      gpointer g_class)
{
	GstDspMp4vDec *self;
	GstElementClass *element_class;

	element_class = GST_ELEMENT_CLASS(g_class);
	self = GST_DSP_MP4VDEC(instance);

	self->sinkpad =
		gst_pad_new_from_template(gst_element_class_get_pad_template(element_class, "sink"), "sink");

	gst_pad_set_chain_function(self->sinkpad, pad_chain);
	gst_pad_set_event_function(self->sinkpad, pad_event);

	self->srcpad =
		gst_pad_new_from_template(gst_element_class_get_pad_template(element_class, "src"), "src");

	gst_pad_use_fixed_caps(self->srcpad);

	gst_element_add_pad(GST_ELEMENT(self), self->sinkpad);
	gst_element_add_pad(GST_ELEMENT(self), self->srcpad);

	self->port[0] = du_port_new(1);
	self->port[1] = du_port_new(1);

	self->ts_mutex = g_mutex_new();

	self->flush = g_comp_new();

	gst_pad_set_setcaps_function(self->sinkpad, sink_setcaps);
}

static void
finalize(GObject *obj)
{
	GstDspMp4vDec *self;

	self = GST_DSP_MP4VDEC(obj);

	g_comp_free(self->flush);

	g_mutex_free(self->ts_mutex);

	du_port_free(self->port[1]);
	du_port_free(self->port[0]);

	G_OBJECT_CLASS(parent_class)->finalize (obj);
}

static void
base_init(gpointer g_class)
{
	GstElementClass *element_class;
	GstPadTemplate *template;
	GstElementDetails details;

	element_class = GST_ELEMENT_CLASS(g_class);

	details.longname = "DSP MPEG-4 video decoder";
	details.klass = "Codec/Decoder/Video";
	details.description = "Decodes video in MPEG-4 format with TI's DSP algorithms";
	details.author = "Felipe Contreras";

	gst_element_class_set_details(element_class, &details);

	template = gst_pad_template_new("src", GST_PAD_SRC,
					GST_PAD_ALWAYS,
					generate_src_template());

	gst_element_class_add_pad_template(element_class, template);

	template = gst_pad_template_new("sink", GST_PAD_SINK,
					GST_PAD_ALWAYS,
					generate_sink_template());

	gst_element_class_add_pad_template(element_class, template);
}

static void
class_init(gpointer g_class,
	   gpointer class_data)
{
	GstElementClass *gstelement_class;
	GObjectClass *gobject_class;

	gstelement_class = GST_ELEMENT_CLASS (g_class);
	gobject_class = G_OBJECT_CLASS (g_class);

	parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

	gstelement_class->change_state = change_state;
	gobject_class->finalize = finalize;
}

GType
gst_dsp_mp4v_dec_get_type(void)
{
	static GType type = 0;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo *type_info;

		type_info = g_new0(GTypeInfo, 1);
		type_info->class_size = sizeof(GstDspMp4vDecClass);
		type_info->class_init = class_init;
		type_info->base_init = base_init;
		type_info->instance_size = sizeof(GstDspMp4vDec);
		type_info->instance_init = instance_init;

		type = g_type_register_static(GST_TYPE_ELEMENT, "GstDspMp4vDec", type_info, 0);
		g_free(type_info);
	}

	return type;
}
