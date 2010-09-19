/*
 * Copyright (C) 2009-2010 Felipe Contreras
 *
 * Author: Felipe Contreras <felipe.contreras@gmail.com>
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

#ifndef LOG_H
#define LOG_H

/* #define DEBUG */

void pr_helper(unsigned int level,
		void *object,
		const char *file,
		const char *function,
		unsigned int line,
		const char *fmt,
		...) __attribute__((format(printf, 6, 7)));

#define pr_base(level, object, ...) pr_helper(level, object, __FILE__, __func__, __LINE__, __VA_ARGS__)

#define pr_err(object, ...) pr_base(0, object, __VA_ARGS__)
#define pr_warning(object, ...) pr_base(1, object, __VA_ARGS__)
#define pr_test(object, ...) pr_base(2, object, __VA_ARGS__)

#if !defined(GST_DISABLE_GST_DEBUG) || defined(DEBUG)
#define pr_info(object, ...) pr_base(3, object, __VA_ARGS__)
#define pr_debug(object, ...) pr_base(4, object, __VA_ARGS__)
#else
#define pr_info(object, ...) ({ if (0) pr_base(3, object, __VA_ARGS__); })
#define pr_debug(object, ...) ({ if (0) pr_base(4, object, __VA_ARGS__); })
#endif

#endif /* LOG_H */
