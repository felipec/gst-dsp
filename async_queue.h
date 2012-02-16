/*
 * Copyright (C) 2008-2010 Nokia Corporation
 *
 * Author: Felipe Contreras <felipe.contreras@nokia.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef ASYNC_QUEUE_H
#define ASYNC_QUEUE_H

#include <glib.h>

struct async_queue {
	GMutex *mutex;
	GCond *condition;
	GList *head;
	GList *tail;
	guint length;
	gboolean enabled;
};

static inline struct async_queue *
async_queue_new(void)
{
	struct async_queue *queue;

	queue = g_slice_new0(struct async_queue);

	queue->condition = g_cond_new();
	queue->mutex = g_mutex_new();
	queue->enabled = TRUE;

	return queue;
}

static inline void
async_queue_free(struct async_queue *queue)
{
	g_cond_free(queue->condition);
	g_mutex_free(queue->mutex);

	g_list_free(queue->head);
	g_slice_free(struct async_queue, queue);
}

static inline void
async_queue_push(struct async_queue *queue,
		 gpointer data)
{
	g_mutex_lock(queue->mutex);

	queue->head = g_list_prepend(queue->head, data);
	if (!queue->tail)
		queue->tail = queue->head;
	queue->length++;

	g_cond_signal(queue->condition);

	g_mutex_unlock(queue->mutex);
}

static inline gpointer
async_queue_pop(struct async_queue *queue)
{
	gpointer data = NULL;

	g_mutex_lock(queue->mutex);

	if (!queue->enabled)
		goto leave;

	if (!queue->tail)
		g_cond_wait(queue->condition, queue->mutex);

	if (queue->tail) {
		GList *node = queue->tail;
		data = node->data;

		queue->tail = node->prev;
		if (queue->tail)
			queue->tail->next = NULL;
		else
			queue->head = NULL;
		queue->length--;
		g_list_free_1(node);
	}

leave:
	g_mutex_unlock(queue->mutex);

	return data;
}

static inline gpointer
async_queue_pop_forced(struct async_queue *queue)
{
	gpointer data = NULL;

	g_mutex_lock(queue->mutex);

	if (queue->tail) {
		GList *node = queue->tail;
		data = node->data;

		queue->tail = node->prev;
		if (queue->tail)
			queue->tail->next = NULL;
		else
			queue->head = NULL;
		queue->length--;
		g_list_free_1(node);
	}

	g_mutex_unlock(queue->mutex);

	return data;
}

static inline void
async_queue_disable(struct async_queue *queue)
{
	g_mutex_lock(queue->mutex);
	queue->enabled = FALSE;
	g_cond_broadcast(queue->condition);
	g_mutex_unlock(queue->mutex);
}

static inline void
async_queue_enable(struct async_queue *queue)
{
	g_mutex_lock(queue->mutex);
	queue->enabled = TRUE;
	g_mutex_unlock(queue->mutex);
}

static inline void
async_queue_flush(struct async_queue *queue)
{
	g_mutex_lock(queue->mutex);
	g_list_free(queue->head);
	queue->head = queue->tail = NULL;
	queue->length = 0;
	g_mutex_unlock(queue->mutex);
}

#endif /* ASYNC_QUEUE_H */
