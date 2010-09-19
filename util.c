/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "util.h"

#include <glib.h>

bool gstdsp_register(int dsp_handle,
		     const struct dsp_uuid *uuid,
		     int type,
		     const char *filename)
{
	gchar *path;
	path = g_build_filename(DSPDIR, filename, NULL);
	if (!dsp_register(dsp_handle, uuid, type, path)) {
		g_free(path);
		return false;
	}

	g_free(path);
	return true;
}
