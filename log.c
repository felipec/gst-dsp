/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#include "log.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#include <gst/gst.h>

#define SYSLOG

#ifdef SYSLOG
#include <syslog.h>
#endif

#ifndef GST_DISABLE_GST_DEBUG
#include "plugin.h"
#endif

#ifdef SYSLOG
static inline int
log_level_to_syslog(unsigned int level)
{
	switch (level) {
	case 0: return LOG_ERR;
	case 1: return LOG_WARNING;
	case 2:
	case 3: return LOG_INFO;
	default: return LOG_DEBUG;
	}
}
#endif

#ifndef GST_DISABLE_GST_DEBUG
static inline GstDebugLevel
log_level_to_gst(unsigned int level)
{
	switch (level) {
	case 0: return GST_LEVEL_ERROR;
	case 1: return GST_LEVEL_WARNING;
	case 2:
	case 3: return GST_LEVEL_INFO;
	default: return GST_LEVEL_DEBUG;
	}
}
#endif

void pr_helper(unsigned int level,
		void *object,
		const char *file,
		const char *function,
		unsigned int line,
		const char *fmt,
		...)
{
	char *tmp;
	va_list args;

	va_start(args, fmt);

	if (vasprintf(&tmp, fmt, args) < 0)
		goto leave;

	if (level <= 1) {
#ifdef SYSLOG
		if (object && GST_IS_OBJECT(object) && GST_OBJECT_NAME(object))
			syslog(log_level_to_syslog(level), "%s: %s", GST_OBJECT_NAME(object), tmp);
		else
			syslog(log_level_to_syslog(level), "%s", tmp);
#endif
		g_printerr("%s: %s\n", function, tmp);
	}
	else if (level == 2)
		g_print("%s:%s(%u): %s\n", file, function, line, tmp);
#if defined(DEVEL) || defined(DEBUG)
	else if (level == 3)
		g_print("%s: %s\n", function, tmp);
#endif
#ifdef DEBUG
	else if (level == 4)
		g_print("%s:%s(%u): %s\n", file, function, line, tmp);
#endif

#ifndef GST_DISABLE_GST_DEBUG
	gst_debug_log_valist(gstdsp_debug, log_level_to_gst(level), file, function, line, object, fmt, args);
#endif

	free(tmp);

leave:
	va_end(args);
}
