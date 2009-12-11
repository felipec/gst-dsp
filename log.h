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

#ifndef LOG_H
#define LOG_H

/* #define DEBUG */

void pr_helper(unsigned int level,
	       void *object,
	       const char *file,
	       const char *function,
	       unsigned int line,
	       const char *fmt,
	       ...);

#define pr_base(level, object, ...) pr_helper(level, object, __FILE__, __func__, __LINE__, __VA_ARGS__)

#define pr_err(object, ...) pr_base(0, object, __VA_ARGS__)
#define pr_warning(object, ...) pr_base(1, object, __VA_ARGS__)
#define pr_test(object, ...) pr_base(2, object, __VA_ARGS__)

#if !defined(GST_DISABLE_GST_DEBUG) || defined(DEBUG)
#define pr_info(object, ...) pr_base(3, object, __VA_ARGS__)
#define pr_debug(object, ...) pr_base(4, object, __VA_ARGS__)
#else
#define pr_info(object, ...) {}
#define pr_debug(object, ...) {}
#endif

#endif /* LOG_H */
