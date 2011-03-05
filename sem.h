/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef SEM_H
#define SEM_H

#include <glib.h>

typedef struct GSem GSem;

struct GSem {
	GCond *condition;
	GMutex *mutex;
	guint count;
};

static inline GSem *
g_sem_new(guint count)
{
	GSem *sem;

	sem = g_new(GSem, 1);
	sem->condition = g_cond_new();
	sem->mutex = g_mutex_new();
	sem->count = count;

	return sem;
}

static inline void
g_sem_free(GSem *sem)
{
	g_cond_free(sem->condition);
	g_mutex_free(sem->mutex);
	g_free(sem);
}

static inline void
g_sem_down(GSem *sem)
{
	g_mutex_lock(sem->mutex);
	while (sem->count == 0)
		g_cond_wait(sem->condition, sem->mutex);
	sem->count--;
	g_mutex_unlock(sem->mutex);
}

static inline bool
g_sem_down_timed(GSem *sem, int seconds)
{
	GTimeVal tv;

	g_mutex_lock(sem->mutex);
	while (sem->count == 0) {
		g_get_current_time(&tv);
		tv.tv_sec += seconds;
		if (!g_cond_timed_wait(sem->condition, sem->mutex, &tv)) {
			g_mutex_unlock(sem->mutex);
			return false;
		}
	}
	sem->count--;
	g_mutex_unlock(sem->mutex);

	return true;
}

static inline void
g_sem_up(GSem *sem)
{
	g_mutex_lock(sem->mutex);
	sem->count++;
	g_cond_signal(sem->condition);
	g_mutex_unlock(sem->mutex);
}

#endif /* SEM_H */
