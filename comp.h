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

#ifndef COMP_H
#define COMP_H

#include <glib.h>

typedef struct GComp GComp;

struct GComp
{
	GCond *condition;
	GMutex *mutex;
	gboolean done;
};

static inline GComp *
g_comp_new(void)
{
	GComp *comp;

	comp = g_new(GComp, 1);
	comp->condition = g_cond_new();
	comp->mutex = g_mutex_new();

	return comp;
}

static inline void
g_comp_free(GComp *comp)
{
	g_cond_free(comp->condition);
	g_mutex_free(comp->mutex);
	g_free(comp);
}

static inline void
g_comp_init(GComp *comp)
{
	g_mutex_lock(comp->mutex);
	comp->done = FALSE;
	g_mutex_unlock(comp->mutex);
}

static inline void
g_comp_wait(GComp *comp)
{
	g_mutex_lock(comp->mutex);
	while (!comp->done)
		g_cond_wait(comp->condition, comp->mutex);
	g_mutex_unlock(comp->mutex);
}

static inline void
g_comp_done(GComp *comp)
{
	g_mutex_lock(comp->mutex);
	comp->done = TRUE;
	g_cond_signal(comp->condition);
	g_mutex_unlock(comp->mutex);
}

#endif /* SEM_H */
