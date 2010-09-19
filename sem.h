/*
 * Copyright (C) 2009-2010 Felipe Contreras
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

static inline void
g_sem_up(GSem *sem)
{
	g_mutex_lock(sem->mutex);
	sem->count++;
	g_cond_signal(sem->condition);
	g_mutex_unlock(sem->mutex);
}

#endif /* SEM_H */
