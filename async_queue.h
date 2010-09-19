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

typedef struct AsyncQueue AsyncQueue;

struct AsyncQueue {
	GMutex *mutex;
	GCond *condition;
	GList *head;
	GList *tail;
	guint length;
	gboolean enabled;
};

AsyncQueue *async_queue_new(void);
void async_queue_free(AsyncQueue *queue);
void async_queue_push(AsyncQueue *queue, gpointer data);
gpointer async_queue_pop(AsyncQueue *queue);
gpointer async_queue_pop_forced(AsyncQueue *queue);
void async_queue_disable(AsyncQueue *queue);
void async_queue_enable(AsyncQueue *queue);
void async_queue_flush(AsyncQueue *queue);

#endif /* ASYNC_QUEUE_H */
