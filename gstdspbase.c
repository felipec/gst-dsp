/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "gstdspbase.h"
#include "plugin.h"

#include "dsp_bridge.h"

#include <string.h> /* for memcpy */

#include "util.h"
#include "log.h"

#define GST_CAT_DEFAULT gstdsp_debug

static inline bool send_buffer(GstDspBase *self, struct td_buffer *tb);

static inline void
map_buffer(GstDspBase *self,
	   GstBuffer *g_buf,
	   struct td_buffer *tb);

du_port_t *
du_port_new(int id,
	    int dir)
{
	du_port_t *p;
	p = calloc(1, sizeof(*p));

	p->id = id;
	p->queue = async_queue_new();
	p->dir = dir;

	return p;
}

void
du_port_free(du_port_t *p)
{
	if (!p)
		return;

	free(p->buffers);
	async_queue_free(p->queue);

	free(p);
}

void
du_port_alloc_buffers(du_port_t *p, guint num_buffers)
{
	p->num_buffers = num_buffers;
	free(p->buffers);
	p->buffers = calloc(num_buffers, sizeof(*p->buffers));
	for (unsigned i = 0; i < p->num_buffers; i++)
		p->buffers[i].port = p;
}

static inline void
du_port_flush(du_port_t *p)
{
	guint i;
	struct td_buffer *tb = p->buffers;

	for (i = 0; i < p->num_buffers; i++, tb++) {
		dmm_buffer_t *b = tb->data;
		if (!b)
			continue;
		if (tb->user_data)
			gst_buffer_unref(tb->user_data);
		dmm_buffer_free(b);
		tb->data = NULL;
	}
	async_queue_flush(p->queue);
}

static inline void
g_sem_down_status(GSem *sem,
		  const GstFlowReturn *status)
{
	GstFlowReturn ret = GST_FLOW_OK;
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

typedef struct {
	uint32_t buffer_data;
	uint32_t buffer_size;
	uint32_t param_data;
	uint32_t param_size;
	uint32_t buffer_len;
	uint32_t silly_eos;
	uint32_t silly_buf_state;
	uint32_t silly_buf_active;
	uint32_t silly_buf_id;
#if SN_API >= 2
	uint32_t nb_available_buf;
	uint32_t donot_flush_buf;
	uint32_t donot_invalidate_buf;
#endif
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
	    struct dsp_msg *msg)
{
	int32_t id;
	uint32_t command_id;

	id = msg->cmd & 0x000000ff;
	command_id = msg->cmd & 0xffffff00;

	switch (command_id) {
	case 0x0600: {
		dmm_buffer_t *b;
		du_port_t *p;
		dsp_comm_t *msg_data;
		dmm_buffer_t *param;
		unsigned i;
		struct td_buffer *tb = NULL;

		for (i = 0; i < ARRAY_SIZE(self->ports); i++)
			if (self->ports[i]->id == id) {
				p = self->ports[i];
				break;
			}

		if (i >= ARRAY_SIZE(self->ports))
			g_error("bad port index: %i", id);

		pr_debug(self, "got %s buffer", id == 0 ? "input" : "output");

		for (i = 0; i < p->num_buffers; i++) {
			if (msg->arg_1 == (uint32_t) p->buffers[i].comm->map) {
				tb = &p->buffers[i];
				break;
			}
		}

		if (!tb)
			g_error("buffer mismatch");

		dmm_buffer_end(tb->comm, tb->comm->size);

		msg_data = tb->comm->data;
		b = (void *) msg_data->user_data;
		b->len = msg_data->buffer_len;

		if (G_UNLIKELY(b->len > b->size))
			g_error("wrong buffer size");

		dmm_buffer_unmap(b);

		param = (void *) msg_data->param_virt;
		if (param)
			dmm_buffer_end(param, param->size);

		if (p->recv_cb)
			p->recv_cb(self, tb);

		if (id == 0) {
			if (tb->user_data) {
				gst_buffer_unref(tb->user_data);
				tb->user_data = NULL;
			}
		}

		async_queue_push(p->queue, tb);
		break;
	}
	case 0x0500:
		pr_debug(self, "got flush");
		break;
	case 0x0200:
		pr_debug(self, "got stop");
		g_sem_up(self->flush);
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

		if (msg->arg_1 == 1 && (msg->arg_2 & 0x0600) == 0x0600) {
			struct td_codec *codec = self->codec;
			if (codec->update_params)
				codec->update_params(self, self->node, msg->arg_2);
			break;
		}

		pr_warning(self, "DSP event: cmd=0x%04X, arg1=%u, arg2=0x%04X",
			   msg->cmd, msg->arg_1, msg->arg_2);
		if ((msg->arg_2 & 0x0F00) == 0x0F00)
			gstdsp_got_error(self, 0, "algo error");
		break;
	default:
		pr_warning(self, "unhandled command: %u", command_id);
	}
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
		p->buffers[i].data = b = dmm_buffer_new(self->dsp_handle, self->proc, p->dir);
		b->alignment = 0;
		async_queue_push(p->queue, &p->buffers[i]);
	}

	p = self->ports[1];
	for (i = 0; i < p->num_buffers; i++) {
		p->buffers[i].data = b = dmm_buffer_new(self->dsp_handle, self->proc, p->dir);

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
				b->need_copy = true;
			} else {
				map_buffer(self, buf, &p->buffers[i]);
				gst_buffer_unref(buf);
			}
		}
		else
			dmm_buffer_allocate(b, self->output_buffer_size);

		self->send_buffer(self, &p->buffers[i]);
	}
}

static inline void
pause_task(GstDspBase *self, GstFlowReturn status)
{
	bool deferred_eos;

	/* synchronize to ensure we are not dropping the EOS event */
	g_mutex_lock(self->ts_mutex);
	g_atomic_int_compare_and_exchange(&self->status, GST_FLOW_OK, status);
	deferred_eos = g_atomic_int_compare_and_exchange(&self->deferred_eos, true, false);
	g_mutex_unlock(self->ts_mutex);

	pr_info(self, "pausing task; reason %s", gst_flow_get_name(status));
	gst_pad_pause_task(self->srcpad);

	/* avoid waiting for buffers that will never come */
	async_queue_disable(self->ports[0]->queue);
	async_queue_disable(self->ports[1]->queue);

	/* there's a pending deferred EOS, it's now or never */
	if (deferred_eos) {
		pr_info(self, "send elapsed eos");
		gst_pad_push_event(self->srcpad, gst_event_new_eos());
	}
}

static inline GstFlowReturn
check_status(GstDspBase *self)
{
	GstFlowReturn ret;
	ret = g_atomic_int_get(&self->status);
	if (G_UNLIKELY(ret != GST_FLOW_OK))
		pause_task(self, ret);
	return ret;
}

static void
output_loop(gpointer data)
{
	GstPad *pad;
	GstDspBase *self;
	GstFlowReturn ret = GST_FLOW_OK;
	GstBuffer *out_buf;
	dmm_buffer_t *b;
	gboolean flush_buffer;
	gboolean got_eos = FALSE;
	gboolean keyframe = FALSE;
	GstEvent *event;
	du_port_t *p;
	struct td_buffer *tb;

	pad = data;
	self = GST_DSP_BASE(GST_OBJECT_PARENT(pad));
	p = self->ports[1];

	pr_debug(self, "begin");
	tb = async_queue_pop(p->queue);

	/*
	 * queue might have been disabled above, so perhaps tb == NULL,
	 * but then right here in between self->status may have been set to
	 * OK by e.g. FLUSH_STOP
	 */
	if (G_UNLIKELY(!tb)) {
		pr_info(self, "no buffer");
		ret = check_status(self);
		goto nok;
	}

	b = tb->data;

	ret = check_status(self);
	if (G_UNLIKELY(ret != GST_FLOW_OK)) {
		async_queue_push(p->queue, tb);
		goto end;
	}

	if (G_UNLIKELY(self->skip_hack_2 > 0)) {
		self->skip_hack_2--;
		goto leave;
	}

	/* check for too many buffers returned */
	g_mutex_lock(self->ts_mutex);
	if (G_UNLIKELY(b->len && !self->ts_count)) {
		pr_warning(self, "no timestamp; unexpected buffer");
		g_mutex_unlock(self->ts_mutex);
		goto leave;
	}
	g_mutex_unlock(self->ts_mutex);

	/* first clear pending events */
	g_mutex_lock(self->ts_mutex);
	while ((event = self->ts_array[self->ts_out_pos].event)) {
		self->ts_array[self->ts_out_pos].event = NULL;
		flush_buffer = (self->ts_out_pos != self->ts_push_pos);
		self->ts_out_pos = (self->ts_out_pos + 1) % ARRAY_SIZE(self->ts_array);
		if (G_LIKELY(!flush_buffer)) {
			self->ts_push_pos = self->ts_out_pos;
			pr_debug(self, "pushing event: %s", GST_EVENT_TYPE_NAME(event));
			gst_pad_push_event(self->srcpad, event);
		} else {
			pr_debug(self, "ignored flushed event: %s", GST_EVENT_TYPE_NAME(event));
			gst_event_unref(event);
		}
	}
	g_mutex_unlock(self->ts_mutex);

	/* a pending reallocation from the previous run */
	if (G_UNLIKELY(!b->data)) {
		dmm_buffer_allocate(b, self->output_buffer_size);
		send_buffer(self, tb);
		goto end;
	}

	if (G_UNLIKELY(!b->len)) {
		/* no need to process this buffer */
		/* no real frame data, so no need to consume a real frame's ts */
		goto leave;
	}

	g_mutex_lock(self->ts_mutex);
	flush_buffer = (self->ts_out_pos != self->ts_push_pos);
	g_mutex_unlock(self->ts_mutex);

	if (G_UNLIKELY(flush_buffer)) {
		g_mutex_lock(self->ts_mutex);
		pr_debug(self, "ignored flushed output buffer for %" GST_TIME_FORMAT,
			 GST_TIME_ARGS((self->ts_array[self->ts_out_pos].time)));
		self->ts_count--;
		self->ts_out_pos = (self->ts_out_pos + 1) % ARRAY_SIZE(self->ts_array);
		g_mutex_unlock(self->ts_mutex);
		goto leave;
	}

	/* now go after the data, but let's first see if it is keyframe */
	keyframe = tb->keyframe;

	if (self->use_pad_alloc) {
		GstBuffer *new_buf;

		ret = gst_pad_alloc_buffer_and_set_caps(self->srcpad,
							GST_BUFFER_OFFSET_NONE,
							self->output_buffer_size,
							GST_PAD_CAPS(self->srcpad),
							&new_buf);

		if (G_UNLIKELY(ret != GST_FLOW_OK)) {
			pr_info(self, "couldn't allocate buffer: %s", gst_flow_get_name(ret));
			async_queue_push(p->queue, tb);
			goto nok;
		}

		if (tb->user_data) {
			out_buf = tb->user_data;
			tb->user_data = NULL;
			map_buffer(self, new_buf, tb);
			gst_buffer_unref(new_buf);
		}
		else
			out_buf = new_buf;

		if (b->need_copy) {
			pr_info(self, "copy");
			memcpy(GST_BUFFER_DATA(out_buf), b->data, b->len);
		}

		GST_BUFFER_SIZE(out_buf) = b->len;
	}
	else {
		out_buf = gst_buffer_new();
		GST_BUFFER_DATA(out_buf) = b->data;
		GST_BUFFER_MALLOCDATA(out_buf) = b->allocated_data;
		GST_BUFFER_SIZE(out_buf) = b->len;
		gst_buffer_set_caps(out_buf, GST_PAD_CAPS(self->srcpad));

		/* invalidate data to force reallocation */
		b->data = b->allocated_data = NULL;
	}

	if (G_UNLIKELY(self->skip_hack > 0)) {
		self->skip_hack--;
		gst_buffer_unref(out_buf);
		goto leave;
	}

	if (!keyframe)
		GST_BUFFER_FLAGS(out_buf) |= GST_BUFFER_FLAG_DELTA_UNIT;

	g_mutex_lock(self->ts_mutex);
	GST_BUFFER_TIMESTAMP(out_buf) = self->ts_array[self->ts_out_pos].time;
	GST_BUFFER_DURATION(out_buf) = self->ts_array[self->ts_out_pos].duration;
	self->ts_out_pos = (self->ts_out_pos + 1) % ARRAY_SIZE(self->ts_array);
	self->ts_push_pos = self->ts_out_pos;
	self->ts_count--;

	if (G_UNLIKELY(g_atomic_int_get(&self->deferred_eos)) && self->ts_count == 0)
		got_eos = TRUE;
#ifdef TS_COUNT
	if (self->ts_count > 2 || self->ts_count < 1)
		pr_info(self, "tsc=%lu", self->ts_count);
#endif
	g_mutex_unlock(self->ts_mutex);

	pr_debug(self, "pushing buffer %" GST_TIME_FORMAT,
		 GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(out_buf)));
	ret = gst_pad_push(self->srcpad, out_buf);
	if (G_UNLIKELY(ret != GST_FLOW_OK)) {
		pr_info(self, "pad push failed: %s", gst_flow_get_name(ret));
		goto leave;
	}

leave:
	if (G_UNLIKELY(got_eos)) {
		pr_info(self, "got eos");
		gst_pad_push_event(self->srcpad, gst_event_new_eos());
		g_atomic_int_set(&self->deferred_eos, false);
		ret = GST_FLOW_UNEXPECTED;
		/*
		 * We don't want to allocate data unnecessarily; postpone after
		 * EOS and flush.
		 */
		if (b->data)
			send_buffer(self, tb);
		else
			/* we'll need to allocate on the next run */
			async_queue_push(p->queue, tb);
	}
	else {
		if (!b->data)
			dmm_buffer_allocate(b, self->output_buffer_size);
		self->send_buffer(self, tb);
	}

nok:
	if (G_UNLIKELY(ret != GST_FLOW_OK))
		pause_task(self, ret);

end:
	pr_debug(self, "end");
}

void
gstdsp_base_flush_buffer(GstDspBase *self)
{
	struct td_buffer *tb;
	tb = async_queue_pop(self->ports[0]->queue);
	if (!tb)
		return;
	dmm_buffer_allocate(tb->data, 1);
	send_buffer(self, tb);
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

void
gstdsp_got_error(GstDspBase *self,
	  guint id,
	  const char *message)
{
	pr_err(self, "%s", message);
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
			if (errno == ETIME) {
				pr_info(self, "timed out waiting for events");
				continue;
			}
			pr_err(self, "failed waiting for events: %i", errno);
			gstdsp_got_error(self, -1, "unable to get event");
			break;
		}

		if (index == 0) {
			struct dsp_msg msg;
			while (true) {
				if (!dsp_node_get_message(self->dsp_handle, self->node, &msg, 100))
					break;
				pr_debug(self, "got dsp message: 0x%0x 0x%0x 0x%0x",
					 msg.cmd, msg.arg_1, msg.arg_2);
				self->got_message(self, &msg);
			}
		}
		else if (index == 1) {
			gstdsp_got_error(self, 1, "got DSP MMUFAULT");
			break;
		}
		else if (index == 2) {
			gstdsp_got_error(self, 2, "got DSP SYSERROR");
			break;
		}
		else {
			gstdsp_got_error(self, 3, "wrong event index");
			break;
		}
	}

	pr_info(self, "end");

	return NULL;
}

static inline bool
destroy_node(GstDspBase *self)
{
	if (self->node) {
		if (!dsp_node_free(self->dsp_handle, self->node)) {
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

	self->dsp_handle = dsp_handle = dsp_open();

	if (dsp_handle < 0) {
		pr_err(self, "dsp open failed");
		return FALSE;
	}

	if (!dsp_attach(dsp_handle, 0, NULL, &self->proc)) {
		pr_err(self, "dsp attach failed");
		goto fail;
	}

	return TRUE;

fail:
	if (self->proc) {
		if (!dsp_detach(dsp_handle, self->proc))
			pr_err(self, "dsp detach failed");
		self->proc = NULL;
	}

	if (self->dsp_handle >= 0) {
		if (dsp_close(dsp_handle) < 0)
			pr_err(self, "dsp close failed");
		self->dsp_handle = -1;
	}

	return FALSE;
}

static gboolean
dsp_deinit(GstDspBase *self)
{
	gboolean ret = TRUE;

	if (self->dsp_error)
		goto leave;

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

static bool
send_play_message(GstDspBase *self)
{
	return dsp_send_message(self->dsp_handle, self->node, 0x0100, 0, 0);
};

gboolean
gstdsp_start(GstDspBase *self)
{
	guint i;

	for (i = 0; i < ARRAY_SIZE(self->ports); i++) {
		du_port_t *p = self->ports[i];
		guint j;
		for (j = 0; j < p->num_buffers; j++) {
			struct td_buffer *tb = &p->buffers[j];
			tb->comm = dmm_buffer_new(self->dsp_handle, self->proc, DMA_BIDIRECTIONAL);
			dmm_buffer_allocate(tb->comm, sizeof(*tb->comm));
			dmm_buffer_map(tb->comm);
		}
	}

	if (!dsp_node_run(self->dsp_handle, self->node)) {
		pr_err(self, "dsp node run failed");
		return false;
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

	self->send_play_message(self);

	setup_buffers(self);

	return true;
}

static bool
send_stop_message(GstDspBase *self)
{
	if (dsp_send_message(self->dsp_handle, self->node, 0x0200, 0, 0))
		g_sem_down(self->flush);
	/** @todo find a way to stop wait_for_events */
	return true;
};

static gboolean
_dsp_stop(GstDspBase *self)
{
	unsigned long exit_status;
	unsigned i;

	if (!self->node)
		return TRUE;

	if (!self->dsp_error) {
		self->send_stop_message(self);
		self->done = TRUE;
	}

	g_thread_join(self->dsp_thread);
	gst_pad_stop_task(self->srcpad);

	for (i = 0; i < ARRAY_SIZE(self->ports); i++)
		du_port_flush(self->ports[i]);

	for (i = 0; i < ARRAY_SIZE(self->ports); i++) {
		guint j;
		du_port_t *port = self->ports[i];
		for (j = 0; j < port->num_buffers; j++) {
			dmm_buffer_free(port->buffers[j].params);
			port->buffers[j].params = NULL;
		}
	}

	for (i = 0; i < ARRAY_SIZE(self->ts_array); i++) {
		if (self->ts_array[i].event) {
			gst_event_unref(self->ts_array[i].event);
			self->ts_array[i].event = NULL;
		}
	}
	self->ts_in_pos = self->ts_out_pos = self->ts_push_pos = 0;
	self->ts_count = 0;
	self->skip_hack = 0;
	self->skip_hack_2 = 0;

	for (i = 0; i < ARRAY_SIZE(self->events); i++) {
		free(self->events[i]);
		self->events[i] = NULL;
	}

	if (self->alg_ctrl) {
		dmm_buffer_free(self->alg_ctrl);
		self->alg_ctrl = NULL;
	}

	if (self->dsp_error)
		goto leave;

	if (!dsp_node_terminate(self->dsp_handle, self->node, &exit_status))
		pr_err(self, "dsp node terminate failed: 0x%lx", exit_status);

leave:
	if (!destroy_node(self))
		pr_err(self, "dsp node destroy failed");

	self->node = NULL;

	for (i = 0; i < ARRAY_SIZE(self->ports); i++) {
		du_port_t *p = self->ports[i];
		guint j;
		for (j = 0; j < p->num_buffers; j++) {
			dmm_buffer_free(p->buffers[j].comm);
			p->buffers[j].comm = NULL;
		}
		du_port_alloc_buffers(p, 0);
	}

	pr_info(self, "dsp node terminated");

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
	   struct td_buffer *tb)
{
	dmm_buffer_t *d_buf = tb->data;

	if (d_buf->alignment == 0 || buffer_is_aligned(g_buf, d_buf)) {
		dmm_buffer_use(d_buf, GST_BUFFER_DATA(g_buf), GST_BUFFER_SIZE(g_buf));
		gst_buffer_ref(g_buf);
		tb->user_data = g_buf;
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

static inline bool send_buffer(GstDspBase *self, struct td_buffer *tb)
{
	dsp_comm_t *msg_data;
	du_port_t *port;
	int index = tb->port->id;
	dmm_buffer_t *buffer = tb->data;

	pr_debug(self, "sending %s buffer", index == 0 ? "input" : "output");

	buffer->len = buffer->size;

	port = self->ports[index];

	msg_data = tb->comm->data;

	if (port->send_cb)
		port->send_cb(self, tb);

	if (tb->params)
		dmm_buffer_begin(tb->params, tb->params->size);

	dmm_buffer_map(buffer);

	memset(msg_data, 0, sizeof(*msg_data));

	msg_data->buffer_data = (uint32_t) buffer->map;
	msg_data->buffer_size = buffer->size;
	msg_data->stream_id = port->id;
	msg_data->buffer_len = index == 0 ? buffer->len : 0;

	msg_data->user_data = (uint32_t) buffer;

	if (tb->params) {
		msg_data->param_data = (uint32_t) tb->params->map;
		msg_data->param_size = tb->params->size;
		msg_data->param_virt = (uint32_t) tb->params;
	}

	dmm_buffer_begin(tb->comm, sizeof(*msg_data));

	dsp_send_message(self->dsp_handle, self->node,
			 0x0600 | port->id, (uint32_t) tb->comm->map, 0);

	return true;
}

void
gstdsp_send_alg_ctrl(GstDspBase *self,
		     struct dsp_node *node,
		     dmm_buffer_t *b)
{
	self->alg_ctrl = b;
	dmm_buffer_map(b);
	dsp_send_message(self->dsp_handle, node,
			 0x0400, 3, (uint32_t) b->map);
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
			gstdsp_post_error(self, "dsp init failed");
			return GST_STATE_CHANGE_FAILURE;
		}
		break;

	case GST_STATE_CHANGE_READY_TO_PAUSED:
		self->status = GST_FLOW_OK;
		self->done = FALSE;
		async_queue_enable(self->ports[0]->queue);
		async_queue_enable(self->ports[1]->queue);
		self->deferred_eos = false;
		break;

	case GST_STATE_CHANGE_PAUSED_TO_READY:
		g_atomic_int_set(&self->status, GST_FLOW_WRONG_STATE);
		async_queue_disable(self->ports[0]->queue);
		async_queue_disable(self->ports[1]->queue);
		break;

	default:
		break;
	}

	ret = parent_class->change_state(element, transition);

	if (ret == GST_STATE_CHANGE_FAILURE)
		return ret;

	switch (transition) {
	case GST_STATE_CHANGE_PAUSED_TO_READY:
		if (!_dsp_stop(self)) {
			gstdsp_post_error(self, "dsp stop failed");
			return GST_STATE_CHANGE_FAILURE;
		}
		if (self->reset)
			self->reset(self);
		gst_caps_replace(&self->tmp_caps, NULL);
		break;

	case GST_STATE_CHANGE_READY_TO_NULL:
		if (!dsp_deinit(self)) {
			gstdsp_post_error(self, "dsp deinit failed");
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

#ifdef DEBUG
	{
		gchar *str = gst_caps_to_string(self->tmp_caps);
		pr_info(self, "src caps: %s", str);
		g_free(str);
	}
#endif

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
	struct td_buffer *tb;

	if (G_UNLIKELY(!self->node)) {
		if (!init_node(self, buf)) {
			pr_err(self, "couldn't start node");
			return FALSE;
		}
	}

	/*
	 * codec-data must make it through as part of setcaps setup,
	 * otherwise node will miss (likely vital) config data,
	 * Since the port's async_queue might be disabled/flushing,
	 * we forcibly pop a buffer here.
	 */
	tb = async_queue_pop_forced(self->ports[0]->queue);
	/* there should always be one available, as we are just starting */
	g_assert(tb);

	dmm_buffer_allocate(tb->data, GST_BUFFER_SIZE(buf));
	memcpy(tb->data->data, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));

	send_buffer(self, tb);

	return TRUE;
}

gboolean
gstdsp_set_codec_data_caps(GstDspBase *base,
		GstBuffer *buf)
{
	GstCaps *caps = NULL;
	GstStructure *structure;
	GValue value = { .g_type = 0 };

	caps = gst_pad_get_negotiated_caps(base->srcpad);
	caps = gst_caps_make_writable(caps);
	structure = gst_caps_get_structure(caps, 0);

	g_value_init(&value, GST_TYPE_BUFFER);

	gst_value_set_buffer(&value, buf);
	gst_structure_set_value(structure, "codec_data", &value);
	g_value_unset(&value);

	return gst_pad_take_caps(base->srcpad, caps);
}

static GstFlowReturn
pad_chain(GstPad *pad,
	  GstBuffer *buf)
{
	GstDspBase *self;
	dmm_buffer_t *b;
	GstFlowReturn ret = GST_FLOW_OK;
	du_port_t *p;
	struct td_buffer *tb;

	self = GST_DSP_BASE(GST_OBJECT_PARENT(pad));
	p = self->ports[0];

	pr_debug(self, "begin");

	if (G_UNLIKELY(!self->node)) {
		if (!init_node(self, buf)) {
			gstdsp_post_error(self, "couldn't start node");
			ret = GST_FLOW_ERROR;
			goto leave;
		}
	}

	tb = async_queue_pop(p->queue);

	ret = g_atomic_int_get(&self->status);
	if (ret != GST_FLOW_OK) {
		pr_info(self, "status: %s", gst_flow_get_name(self->status));
		if (tb)
			async_queue_push(p->queue, tb);
		goto leave;
	}

	b = tb->data;

	if (GST_BUFFER_SIZE(buf) >= self->input_buffer_size)
		map_buffer(self, buf, tb);
	else {
		dmm_buffer_allocate(b, self->input_buffer_size);
		b->need_copy = true;
	}

	if (b->need_copy) {
		pr_info(self, "copy");
		memcpy(b->data, GST_BUFFER_DATA(buf), GST_BUFFER_SIZE(buf));
	}

	g_mutex_lock(self->ts_mutex);
	self->ts_array[self->ts_in_pos].time = GST_BUFFER_TIMESTAMP(buf);
	self->ts_array[self->ts_in_pos].duration = GST_BUFFER_DURATION(buf);
	self->ts_in_pos = (self->ts_in_pos + 1) % ARRAY_SIZE(self->ts_array);
	self->ts_count++;
	g_mutex_unlock(self->ts_mutex);

	self->send_buffer(self, tb);

leave:

	gst_buffer_unref(buf);

	pr_debug(self, "end");

	return ret;
}

static gboolean
sink_event(GstDspBase *self,
	   GstEvent *event)
{
	gboolean ret = TRUE;

	pr_info(self, "event: %s", GST_EVENT_TYPE_NAME(event));

	switch (GST_EVENT_TYPE(event)) {
	case GST_EVENT_EOS: {
		bool defer_eos = false;

		g_mutex_lock(self->ts_mutex);
		if (self->ts_count != 0)
			defer_eos = true;
		if (self->status != GST_FLOW_OK)
			defer_eos = false;
		g_atomic_int_set(&self->deferred_eos, defer_eos);
		g_mutex_unlock(self->ts_mutex);

		if (defer_eos) {
			if (self->flush_buffer)
				self->flush_buffer(self);
			gst_event_unref(event);
		} else
			ret = gst_pad_push_event(self->srcpad, event);
		break;
	}
	case GST_EVENT_FLUSH_START:
		ret = gst_pad_push_event(self->srcpad, event);
		g_atomic_int_set(&self->status, GST_FLOW_WRONG_STATE);

		async_queue_disable(self->ports[0]->queue);
		async_queue_disable(self->ports[1]->queue);

		gst_pad_pause_task(self->srcpad);

		break;

	case GST_EVENT_FLUSH_STOP:
		ret = gst_pad_push_event(self->srcpad, event);

		g_mutex_lock(self->ts_mutex);
		self->ts_push_pos = self->ts_in_pos;
		pr_debug(self, "flushing next %u buffer(s)",
			 self->ts_push_pos - self->ts_out_pos);
		g_atomic_int_set(&self->deferred_eos, false);
		g_mutex_unlock(self->ts_mutex);

		g_atomic_int_set(&self->status, GST_FLOW_OK);
		async_queue_enable(self->ports[0]->queue);
		async_queue_enable(self->ports[1]->queue);

		gst_pad_start_task(self->srcpad, output_loop, self->srcpad);
		break;

	case GST_EVENT_NEWSEGMENT:
		g_mutex_lock(self->ts_mutex);
		pr_debug(self, "storing event");
		self->ts_array[self->ts_in_pos].event = event;
		self->ts_in_pos = (self->ts_in_pos + 1) % ARRAY_SIZE(self->ts_array);
		g_mutex_unlock(self->ts_mutex);
		break;

	/*
	 * FIXME maybe serialize some more events ??,
	 * but that may need more than a fixed size queue
	 */

	default:
		ret = gst_pad_push_event(self->srcpad, event);
		break;
	}

	return ret;
}

static gboolean
src_event(GstDspBase *self,
	  GstEvent *event)
{
	return gst_pad_push_event(self->sinkpad, event);
}

static gboolean
base_sink_event(GstPad *pad,
		GstEvent *event)
{
	GstDspBase *self;
	GstDspBaseClass *class;
	gboolean ret = TRUE;

	self = GST_DSP_BASE(gst_pad_get_parent(pad));
	class = GST_DSP_BASE_GET_CLASS(self);

	if (class->sink_event)
		ret = class->sink_event(self, event);

	gst_object_unref(self);

	return ret;
}

static gboolean
base_src_event(GstPad *pad,
	  GstEvent *event)
{
	GstDspBase *self;
	GstDspBaseClass *class;
	gboolean ret = TRUE;

	self = GST_DSP_BASE(gst_pad_get_parent(pad));
	class = GST_DSP_BASE_GET_CLASS(self);

	if (class->src_event)
		ret = class->src_event(self, event);

	gst_object_unref(self);

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

	self->ports[0] = du_port_new(0, DMA_TO_DEVICE);
	self->ports[1] = du_port_new(1, DMA_FROM_DEVICE);

	self->got_message = got_message;
	self->send_buffer = send_buffer;
	self->send_play_message = send_play_message;
	self->send_stop_message = send_stop_message;
	self->sinkpad =
		gst_pad_new_from_template(gst_element_class_get_pad_template(element_class, "sink"), "sink");

	gst_pad_set_chain_function(self->sinkpad, pad_chain);
	gst_pad_set_event_function(self->sinkpad, base_sink_event);

	self->srcpad =
		gst_pad_new_from_template(gst_element_class_get_pad_template(element_class, "src"), "src");

	gst_pad_use_fixed_caps(self->srcpad);

	gst_pad_set_event_function(self->srcpad, base_src_event);

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

	G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void
class_init(gpointer g_class,
	   gpointer class_data)
{
	GstElementClass *gstelement_class;
	GObjectClass *gobject_class;
	GstDspBaseClass *class;

	parent_class = g_type_class_peek_parent(g_class);
	gstelement_class = GST_ELEMENT_CLASS(g_class);
	gobject_class = G_OBJECT_CLASS(g_class);
	class = GST_DSP_BASE_CLASS(g_class);

	gstelement_class->change_state = change_state;
	gobject_class->finalize = finalize;

	class->sink_event = sink_event;
	class->src_event = src_event;
}

GType
gst_dsp_base_get_type(void)
{
	static GType type;

	if (G_UNLIKELY(type == 0)) {
		GTypeInfo type_info = {
			.class_size = sizeof(GstDspBaseClass),
			.class_init = class_init,
			.instance_size = sizeof(GstDspBase),
			.instance_init = instance_init,
		};

		type = g_type_register_static(GST_TYPE_ELEMENT, "GstDspBase", &type_info, 0);
	}

	return type;
}
