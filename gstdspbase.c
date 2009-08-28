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

#include "gstdspbase.h"
#include "plugin.h"

#include "dsp_bridge.h"

#include <string.h> /* for memcpy */

#include "log.h"

#define GST_CAT_DEFAULT gstdsp_debug
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

static inline bool
send_buffer(GstDspBase *self,
	    dmm_buffer_t *buffer,
	    unsigned int id,
	    size_t len);

static inline void
map_buffer(GstDspBase *self,
	   GstBuffer *g_buf,
	   dmm_buffer_t *d_buf);

du_port_t *
du_port_new(guint index,
	    guint num_buffers)
{
	du_port_t *p;
	p = calloc(1, sizeof(*p));

	p->index = index;
	p->queue = async_queue_new();
	p->num_buffers = num_buffers;
	p->comm = calloc(num_buffers, sizeof(**p->comm));
	p->buffers = calloc(num_buffers, sizeof(**p->comm));

	return p;
}

void
du_port_free(du_port_t *p)
{
	if (!p)
		return;

	free(p->buffers);
	free(p->comm);
	async_queue_free(p->queue);

	free(p);
}

static inline void
du_port_flush(du_port_t *p)
{
	guint i;
	for (i = 0; i < p->num_buffers; i++)
		p->comm[i]->used = FALSE;
	for (i = 0; i < p->num_buffers; i++) {
		dmm_buffer_t *b = p->buffers[i];
		if (!b)
			continue;
		if (b->user_data)
			gst_buffer_unref(b->user_data);
		dmm_buffer_free(b);
		p->buffers[i] = NULL;
	}
	async_queue_flush(p->queue);
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
	uint32_t msg_virt;
	uint32_t buffer_virt;
	uint32_t param_virt;
	uint32_t silly_out_buffer_index;
	uint32_t silly_in_buffer_index;
	uint32_t user_data;
	uint32_t stream_id;
} dsp_comm_t;

static GstElementClass *parent_class;

static inline void
got_message(GstDspBase *self,
	    dsp_msg_t *msg)
{
	uint32_t id;
	uint32_t command_id;

	id = msg->cmd & 0x000000ff;
	command_id = msg->cmd & 0xffffff00;

	switch (command_id) {
		case 0x0600:
			{
				dmm_buffer_t *b;
				du_port_t *p = self->ports[id];
				dmm_buffer_t *cur = NULL;
				dsp_comm_t *msg_data;
				guint i;

				pr_debug(self, "got %s buffer", id == 0 ? "input" : "output");

				for (i = 0; i < p->num_buffers; i++) {
					if (msg->arg_1 == (uint32_t) p->comm[i]->map) {
						cur = p->comm[i];
						break;
					}
				}
				msg_data = cur->data;
				b = (void *) msg_data->user_data;
				b->len = msg_data->buffer_len;

				if (p->recv_cb)
					p->recv_cb(self, p, (void *) msg_data->param_virt, b);

				if (id == 0) {
					if (b->user_data) {
						gst_buffer_unref(b->user_data);
						b->user_data = NULL;
					}
				}

				cur->used = FALSE;
				async_queue_push(p->queue, b);
			}
			break;
		case 0x0500:
			pr_debug(self, "got flush");
			g_sem_up(self->flush);
			break;
		case 0x0200:
			pr_debug(self, "got stop");
			break;
		case 0x0400:
			pr_debug(self, "got alg ctrl");
			dmm_buffer_free(self->alg_ctrl);
			self->alg_ctrl = NULL;
			break;
		case 0x0e00:
			if (msg->arg_1 == 1 && msg->arg_2 == 0x0500) {
				pr_debug(self, "playback completed");
				break;
			}
			pr_err(self, "error: cmd=%u, arg1=%u, arg2=%u",
			       msg->cmd, msg->arg_1, msg->arg_2);
			break;
		default:
			pr_warning(self, "unhandled command: %u", command_id);
	}
}

static inline dmm_buffer_t *
get_slot(GstDspBase *self,
	 GstBuffer *new_buf)
{
	guint i;
	dmm_buffer_t *b = NULL;

	for (i = 0; i < ARRAY_SIZE(self->cache); i++) {
		dmm_buffer_t *cur = self->cache[i];
		if (cur && !cur->used) {
			if (cur->data == GST_BUFFER_DATA(new_buf)) {
				b = cur;
				return b;
			}
		}
	}

	pr_debug(self, "couldn't reuse mapping");

	for (i = 0; i < ARRAY_SIZE(self->cache); i++) {
		dmm_buffer_t *cur = self->cache[i];
		if (!cur) {
			b = dmm_buffer_new(self->dsp_handle, self->proc);
			self->cache[i] = b;
			goto found;
		}
	}

	for (i = 0; i < ARRAY_SIZE(self->cache); i++) {
		dmm_buffer_t *cur = self->cache[i];
		if (cur && !cur->used) {
			b = cur;
			goto found;
		}
	}

	pr_err(self, "couldn't find slot");
	return NULL;

found:
	map_buffer(self, new_buf, b);
	return b;
}

static inline void
setup_buffers(GstDspBase *self)
{
	GstBuffer *buf = NULL;
	dmm_buffer_t *b;
	du_port_t *p;
	guint i;

	p = self->ports[0];
	for (i = 0; i < p->num_buffers; i++) {
		p->buffers[i] = b = dmm_buffer_new(self->dsp_handle, self->proc);
		b->alignment = 0;
		async_queue_push(p->queue, b);
	}

	p = self->ports[1];
	for (i = 0; i < p->num_buffers; i++) {
		b = dmm_buffer_new(self->dsp_handle, self->proc);
		p->buffers[i] = b;
		b->used = TRUE;
		if (self->use_map_cache)
			self->cache[i] = b;

		if (self->use_pad_alloc) {
			GstFlowReturn ret;
			ret = gst_pad_alloc_buffer_and_set_caps(self->srcpad,
								GST_BUFFER_OFFSET_NONE,
								self->output_buffer_size,
								GST_PAD_CAPS(self->srcpad),
								&buf);
			/* might fail if not (yet) linked */
			if (G_UNLIKELY(ret != GST_FLOW_OK)) {
				pr_err(self, "couldn't allocate buffer: %s", gst_flow_get_name(ret));
				dmm_buffer_allocate(b, self->output_buffer_size);
			} else {
				map_buffer(self, buf, b);
				gst_buffer_unref(buf);
			}
		}
		else
			dmm_buffer_allocate(b, self->output_buffer_size);

		send_buffer(self, b, 1, 0);
	}
}

static void
output_loop(gpointer data)
{
	GstPad *pad;
	GstDspBase *self;
	GstFlowReturn ret = GST_FLOW_OK;
	GstBuffer *out_buf;
	dmm_buffer_t *b;
	gboolean got_eos = FALSE;

	pad = data;
	self = GST_DSP_BASE(GST_OBJECT_PARENT(pad));

	pr_debug(self, "begin");
	b = async_queue_pop(self->ports[1]->queue);

	if ((ret = g_atomic_int_get(&self->status)) != GST_FLOW_OK) {
		pr_info(self, "status: %s", gst_flow_get_name(self->status));
		goto leave;
	}

	b->used = FALSE;

	if (!b->len) {
		/* no need to process this buffer */
		pr_warning(self, "empty buffer");
		send_buffer(self, b, 1, 0);
		goto leave;
	}

	dmm_buffer_invalidate(b, b->len);

	if (self->use_pad_alloc) {
		GstBuffer *new_buf;

		ret = gst_pad_alloc_buffer_and_set_caps(self->srcpad,
							GST_BUFFER_OFFSET_NONE,
							self->output_buffer_size,
							GST_PAD_CAPS(self->srcpad),
							&new_buf);

		if (G_UNLIKELY(ret != GST_FLOW_OK)) {
			pr_info(self, "couldn't allocate buffer: %s", gst_flow_get_name(ret));
			goto leave;
		}

		if (b->user_data) {
			dmm_buffer_t *tmp;
			out_buf = b->user_data;
			b->user_data = NULL;
			if (self->use_map_cache) {
				tmp = get_slot(self, new_buf);
				if (tmp) {
					b = tmp;
					b->user_data = new_buf;
				}
				else
					map_buffer(self, new_buf, b);
			}
			else
				map_buffer(self, new_buf, b);
			gst_buffer_unref(new_buf);
			b->used = TRUE;
		}
		else {
			out_buf = new_buf;

			/* we have to copy, or downstream sees no data */
			pr_info(self, "copy");
			memcpy(GST_BUFFER_DATA(out_buf), b->data, b->len);
		}
	}
	else {
		out_buf = gst_buffer_new();
		GST_BUFFER_DATA(out_buf) = b->data;
		GST_BUFFER_MALLOCDATA(out_buf) = b->allocated_data;
		GST_BUFFER_SIZE(out_buf) = b->len;
		gst_buffer_set_caps(out_buf, GST_PAD_CAPS(self->srcpad));

		b->allocated_data = NULL;
		dmm_buffer_allocate(b, self->output_buffer_size);
	}

	send_buffer(self, b, 1, 0);

	if (G_LIKELY(self->skip_hack > 0)) {
		self->skip_hack--;
		goto leave;
	}

	if (!g_atomic_int_get(&self->keyframe))
		GST_BUFFER_FLAGS(out_buf) |= GST_BUFFER_FLAG_DELTA_UNIT;

	g_mutex_lock(self->ts_mutex);
	GST_BUFFER_TIMESTAMP(out_buf) = self->ts_array[self->ts_out_pos];
	self->ts_out_pos = (self->ts_out_pos + 1) % ARRAY_SIZE(self->ts_array);
	if (self->use_eos_align) {
		if (G_UNLIKELY(self->eos) && self->ts_in_pos == self->ts_out_pos)
			got_eos = TRUE;
	}
#ifdef TS_COUNT
	self->ts_count--;
	if (self->ts_count > 2 || self->ts_count < 1)
		pr_info(self, "tsc=%lu", self->ts_count);
#endif
	g_mutex_unlock(self->ts_mutex);

	ret = gst_pad_push(self->srcpad, out_buf);
	if (G_UNLIKELY(ret != GST_FLOW_OK)) {
		pr_info(self, "pad push failed: %s", gst_flow_get_name(ret));
		goto leave;
	}

leave:
	if (got_eos) {
		pr_info(self, "got eos");
		gst_pad_push_event(self->srcpad, gst_event_new_eos());
		ret = GST_FLOW_UNEXPECTED;
	}

	if (ret != GST_FLOW_OK) {
		g_atomic_int_set(&self->status, ret);
		gst_pad_pause_task(self->srcpad);
	}

	pr_debug(self, "end");
}

void
gstdsp_post_error(GstDspBase *self,
		  const char *message)
{
	GError *gerror;
	GstMessage *gst_msg;

	gerror = g_error_new_literal(GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED, message);
	gst_msg = gst_message_new_error(GST_OBJECT(self), gerror, NULL);
	gst_element_post_message(GST_ELEMENT(self), gst_msg);

	g_error_free(gerror);
}

static inline void
got_error(GstDspBase *self,
	  guint id,
	  const char *message)
{
	pr_err(self, message);
	gstdsp_post_error(self, message);

	g_atomic_int_set(&self->status, GST_FLOW_ERROR);
	self->dsp_error = id;
	async_queue_disable(self->ports[0]->queue);
	async_queue_disable(self->ports[1]->queue);
}

static gpointer
dsp_thread(gpointer data)
{
	GstDspBase *self = data;

	pr_info(self, "begin");

	while (!self->done) {
		unsigned int index = 0;
		pr_debug(self, "waiting for events");
		if (!dsp_wait_for_events(self->dsp_handle, self->events, 3, &index, 1000)) {
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
		else if (index == 1) {
			got_error(self, 1, "got DSP MMUFAULT");
			goto leave;
		}
		else if (index == 2) {
			got_error(self, 2, "got DSP SYSERROR");
			goto leave;
		}
		else {
			got_error(self, 3, "wrong event index");
			goto leave;
		}
	}

leave:
	pr_info(self, "end");

	return NULL;
}

static inline bool
destroy_node(GstDspBase *self,
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
dsp_init(GstDspBase *self)
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

	for (i = 0; i < ARRAY_SIZE(self->ports); i++) {
		du_port_t *p = self->ports[i];
		guint j;
		for (j = 0; j < p->num_buffers; j++) {
			p->comm[j] = dmm_buffer_new(self->dsp_handle, self->proc);
			dmm_buffer_allocate(p->comm[j], sizeof(dsp_comm_t));
		}
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
dsp_deinit(GstDspBase *self)
{
	gboolean ret = TRUE;
	guint i;

	if (self->dsp_error)
		goto leave;

	for (i = 0; i < ARRAY_SIZE(self->ports); i++) {
		du_port_t *p = self->ports[i];
		guint j;
		for (j = 0; j < p->num_buffers; j++) {
			dmm_buffer_free(p->comm[j]);
			p->comm[j] = NULL;
		}
	}

	if (self->proc) {
		if (!dsp_detach(self->dsp_handle, self->proc)) {
			pr_err(self, "dsp detach failed");
			ret = FALSE;
		}
		self->proc = NULL;
	}

leave:

	if (self->dsp_handle >= 0) {
		if (dsp_close(self->dsp_handle) < 0) {
			pr_err(self, "dsp close failed");
			ret = FALSE;
		}
		self->dsp_handle = -1;
	}

	return ret;
}

gboolean
gstdsp_start(GstDspBase *self)
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

	self->events[1] = calloc(1, sizeof(struct dsp_notification));
	if (!dsp_register_notify(self->dsp_handle, self->proc,
				 DSP_MMUFAULT, 1,
				 self->events[1]))
	{
		pr_err(self, "failed to register for DSP_MMUFAULT");
		return false;
	}

	self->events[2] = calloc(1, sizeof(struct dsp_notification));
	if (!dsp_register_notify(self->dsp_handle, self->proc,
				 DSP_SYSERROR, 1,
				 self->events[2]))
	{
		pr_err(self, "failed to register for DSP_SYSERROR");
		return false;
	}

	pr_info(self, "creating dsp thread");
	self->dsp_thread = g_thread_create(dsp_thread, self, TRUE, NULL);
	gst_pad_start_task(self->srcpad, output_loop, self->srcpad);

	/* play */
	dsp_send_message(self->dsp_handle, self->node, 0x0100, 0, 0);

	setup_buffers(self);

	return TRUE;
}

static gboolean
dsp_stop(GstDspBase *self)
{
	unsigned long exit_status;
	guint i;

	if (!self->node)
		return TRUE;

	if (!self->dsp_error) {
		/* stop */
		dsp_send_message(self->dsp_handle, self->node, 0x0200, 0, 0);
	}

	g_thread_join(self->dsp_thread);
	gst_pad_stop_task(self->srcpad);

	for (i = 0; i < ARRAY_SIZE(self->ports); i++)
		du_port_flush(self->ports[i]);

	for (i = 0; i < ARRAY_SIZE(self->ports); i++) {
		dmm_buffer_free(self->ports[i]->param);
		self->ports[i]->param = NULL;
	}

	for (i = 0; i < ARRAY_SIZE(self->cache); i++) {
		dmm_buffer_t *cur = self->cache[i];
		if (cur) {
			dmm_buffer_free(cur);
			self->cache[i] = NULL;
		}
	}

	for (i = 0; i < ARRAY_SIZE(self->events); i++) {
		free(self->events[i]);
		self->events[i] = NULL;
	}

	if (self->dsp_error)
		goto leave;

	if (!dsp_node_terminate(self->dsp_handle, self->node, &exit_status)) {
		pr_err(self, "dsp node terminate failed: 0x%lx", exit_status);
		self->node = NULL;
		return FALSE;
	}

	if (!destroy_node(self, self->dsp_handle, self->node)) {
		pr_err(self, "dsp node destroy failed");
		self->node = NULL;
		return FALSE;
	}

	self->node = NULL;

	pr_info(self, "dsp node terminated");
leave:

	return TRUE;
}

static inline bool
buffer_is_aligned(GstBuffer *buf, dmm_buffer_t *b)
{
	if ((unsigned long) GST_BUFFER_DATA(buf) % b->alignment != 0)
		return false;
	if (((unsigned long) GST_BUFFER_DATA(buf) + GST_BUFFER_SIZE(buf)) % b->alignment != 0)
		return false;
	return true;
}

static inline void
map_buffer(GstDspBase *self,
	   GstBuffer *g_buf,
	   dmm_buffer_t *d_buf)
{
	if (d_buf->alignment == 0 || buffer_is_aligned(g_buf, d_buf)) {
		dmm_buffer_use(d_buf, GST_BUFFER_DATA(g_buf), GST_BUFFER_SIZE(g_buf));
		gst_buffer_ref(g_buf);
		d_buf->user_data = g_buf;
		return;
	}

	if (d_buf->alignment != 0) {
		pr_warning(self, "buffer not aligned: %p-%p",
			   GST_BUFFER_DATA(g_buf),
			   GST_BUFFER_DATA(g_buf) + GST_BUFFER_SIZE(g_buf));
	}

	dmm_buffer_allocate(d_buf, GST_BUFFER_SIZE(g_buf));
	d_buf->need_copy = true;
}

static inline bool
send_buffer(GstDspBase *self,
	    dmm_buffer_t *buffer,
	    unsigned int id,
	    size_t len)
{
	dsp_comm_t *msg_data;
	dmm_buffer_t *tmp;
	du_port_t *port;
	guint i;

	pr_debug(self, "sending %s buffer", id == 0 ? "input" : "output");

	port = self->ports[id];

	for (i = 0; i < port->num_buffers; i++) {
		if (!port->comm[i]->used) {
			tmp = port->comm[i];
			tmp->used = TRUE;
			break;
		}
	}

	msg_data = tmp->data;

	memset(msg_data, 0, sizeof(*msg_data));

	msg_data->buffer_data = (uint32_t) buffer->map;
	msg_data->buffer_size = buffer->size;
	msg_data->stream_id = id;
	msg_data->buffer_len = len;

	msg_data->user_data = (uint32_t) buffer;

	if (port->param) {
		msg_data->param_data = (uint32_t) port->param->map;
		msg_data->param_size = port->param->size;
		msg_data->param_virt = (uint32_t) port->param;
	}

	if (port->send_cb)
		port->send_cb(self, port, port->param, buffer);

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
	GstDspBase *self;

	self = GST_DSP_BASE(element);

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
			self->status = GST_FLOW_OK;
			self->done = FALSE;
			async_queue_enable(self->ports[0]->queue);
			async_queue_enable(self->ports[1]->queue);
			self->eos = FALSE;
			break;

		case GST_STATE_CHANGE_PAUSED_TO_READY:
			self->done = TRUE;
			g_atomic_int_set(&self->status, GST_FLOW_WRONG_STATE);
			async_queue_disable(self->ports[0]->queue);
			async_queue_disable(self->ports[1]->queue);
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

static inline gboolean
init_node(GstDspBase *self,
	  GstBuffer *buf)
{
	if (self->parse_func && !self->parse_func(self, buf))
		pr_err(self, "error while parsing");

	{
		gchar *str = gst_caps_to_string(self->tmp_caps);
		pr_info(self, "src caps: %s", str);
		g_free(str);
	}

	if (!gst_pad_set_caps(self->srcpad, self->tmp_caps)) {
		pr_err(self, "couldn't setup output caps");
		return FALSE;
	}

	if (!self->output_buffer_size)
		return FALSE;

	self->node = self->create_node(self);
	if (!self->node) {
		pr_err(self, "dsp node creation failed");
		return FALSE;
	}

	if (!gstdsp_start(self)) {
		pr_err(self, "dsp start failed");
		return FALSE;
	}

	return TRUE;
}

gboolean
gstdsp_send_codec_data(GstDspBase *self,
		       GstBuffer *buf)
{
	dmm_buffer_t *b;

	if (G_UNLIKELY(!self->node)) {
		if (!init_node(self, buf)) {
			pr_err(self, "couldn't start node");
			return FALSE;
		}
	}

	b = async_queue_pop(self->ports[0]->queue);
	if (G_UNLIKELY(!b))
		return FALSE;

	dmm_buffer_allocate(b, GST_BUFFER_SIZE(buf));
	memcpy(b->data, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));

	dmm_buffer_clean(b, GST_BUFFER_SIZE(buf));

	send_buffer(self, b, 0, GST_BUFFER_SIZE(buf));

	return TRUE;
}

static GstFlowReturn
pad_chain(GstPad *pad,
	  GstBuffer *buf)
{
	GstDspBase *self;
	dmm_buffer_t *b;
	GstFlowReturn ret = GST_FLOW_OK;
	du_port_t *p;

	self = GST_DSP_BASE(GST_OBJECT_PARENT(pad));
	p = self->ports[0];

	pr_debug(self, "begin");

	if (G_UNLIKELY(!self->node)) {
		if (!init_node(self, buf)) {
			pr_err(self, "couldn't start node");
			ret = GST_FLOW_ERROR;
			goto leave;
		}
	}

	b = async_queue_pop(self->ports[0]->queue);

	if ((ret = g_atomic_int_get(&self->status)) != GST_FLOW_OK) {
		pr_info(self, "status: %s", gst_flow_get_name(self->status));
		goto leave;
	}

	if (self->input_buffer_size <= GST_BUFFER_SIZE(buf))
		map_buffer(self, buf, b);
	else {
		dmm_buffer_allocate(b, self->input_buffer_size);
		b->need_copy = true;
	}

	if (b->need_copy) {
		pr_info(self, "copy");
		memcpy(b->data, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
	}

	dmm_buffer_clean(b, GST_BUFFER_SIZE(buf));

	g_mutex_lock(self->ts_mutex);
	self->ts_array[self->ts_in_pos] = GST_BUFFER_TIMESTAMP(buf);
	self->ts_in_pos = (self->ts_in_pos + 1) % ARRAY_SIZE(self->ts_array);
#ifdef TS_COUNT
	self->ts_count++;
#endif
	g_mutex_unlock(self->ts_mutex);

	send_buffer(self, b, 0, GST_BUFFER_SIZE(buf));

	gst_buffer_unref(buf);

leave:

	pr_debug(self, "end");

	return ret;
}

static gboolean
pad_event(GstPad *pad,
	  GstEvent *event)
{
	GstDspBase *self;
	gboolean ret = TRUE;

	self = GST_DSP_BASE(GST_OBJECT_PARENT(pad));

	pr_info(self, "event: %s", GST_EVENT_TYPE_NAME(event));

	switch (GST_EVENT_TYPE(event)) {
		case GST_EVENT_EOS:
			if (g_atomic_int_get(&self->status) != GST_FLOW_OK) {
				ret = gst_pad_push_event(self->srcpad, event);
				break;
			}

			if (!self->use_eos_align) {
				ret = gst_pad_push_event(self->srcpad, event);
				break;
			}

			g_mutex_lock(self->ts_mutex);
			if (self->ts_in_pos == self->ts_out_pos) {
				ret = gst_pad_push_event(self->srcpad, event);
			}
			else {
				self->eos = TRUE;
				gst_event_unref(event);
			}
			g_mutex_unlock(self->ts_mutex);
			break;

		case GST_EVENT_FLUSH_START:
			ret = gst_pad_push_event(self->srcpad, event);
			g_atomic_int_set(&self->status, GST_FLOW_WRONG_STATE);

			async_queue_disable(self->ports[0]->queue);
			async_queue_disable(self->ports[1]->queue);

			gst_pad_pause_task(self->srcpad);

			/* flush */
			dsp_send_message(self->dsp_handle, self->node, 0x0500 | 0, 5, 0);
			dsp_send_message(self->dsp_handle, self->node, 0x0500 | 1, 5, 0);

			break;

		case GST_EVENT_FLUSH_STOP:
			ret = gst_pad_push_event(self->srcpad, event);
			g_sem_down(self->flush); /* input */
			g_sem_down(self->flush); /* output */

			g_mutex_lock(self->ts_mutex);
			self->ts_in_pos = self->ts_out_pos = 0;
			self->eos = FALSE;
			g_mutex_unlock(self->ts_mutex);

			du_port_flush(self->ports[0]);
			du_port_flush(self->ports[1]);

			g_atomic_int_set(&self->status, GST_FLOW_OK);
			async_queue_enable(self->ports[0]->queue);
			async_queue_enable(self->ports[1]->queue);

			setup_buffers(self);

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
	GstDspBase *self;
	GstElementClass *element_class;

	element_class = GST_ELEMENT_CLASS(g_class);
	self = GST_DSP_BASE(instance);

	self->sinkpad =
		gst_pad_new_from_template(gst_element_class_get_pad_template(element_class, "sink"), "sink");

	gst_pad_set_chain_function(self->sinkpad, pad_chain);
	gst_pad_set_event_function(self->sinkpad, pad_event);

	self->srcpad =
		gst_pad_new_from_template(gst_element_class_get_pad_template(element_class, "src"), "src");

	gst_pad_use_fixed_caps(self->srcpad);

	gst_element_add_pad(GST_ELEMENT(self), self->sinkpad);
	gst_element_add_pad(GST_ELEMENT(self), self->srcpad);

	self->ts_mutex = g_mutex_new();

	self->flush = g_sem_new(0);
}

static void
finalize(GObject *obj)
{
	GstDspBase *self;

	self = GST_DSP_BASE(obj);

	g_sem_free(self->flush);

	g_mutex_free(self->ts_mutex);

	du_port_free(self->ports[1]);
	du_port_free(self->ports[0]);

	G_OBJECT_CLASS(parent_class)->finalize (obj);
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
gst_dsp_base_get_type(void)
{
	static GType type = 0;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo *type_info;

		type_info = g_new0(GTypeInfo, 1);
		type_info->class_size = sizeof(GstDspBaseClass);
		type_info->class_init = class_init;
		type_info->instance_size = sizeof(GstDspBase);
		type_info->instance_init = instance_init;

		type = g_type_register_static(GST_TYPE_ELEMENT, "GstDspBase", type_info, 0);
		g_free(type_info);
	}

	return type;
}
