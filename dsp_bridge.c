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

#include "dsp_bridge.h"

/* for open */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <unistd.h> /* for close */
#include <sys/ioctl.h> /* for ioctl */
#include <stdlib.h> /* for free */

#include <malloc.h> /* for memalign */

int dsp_open(void)
{
	return open("/dev/DspBridge", O_RDWR);
}

int dsp_close(int handle)
{
	return close(handle);
}

struct proc_attach {
	unsigned int num;
	const void *info; /* not used */
	void **ret_handle;
};

bool dsp_attach(int handle,
		unsigned int num,
		const void *info,
		void **ret_handle)
{
	struct proc_attach arg = {
		.num = num,
		.info = info,
		.ret_handle = ret_handle,
	};

	return DSP_SUCCEEDED(ioctl(handle, 7, &arg));
}

struct proc_detach {
	void *proc_handle;
};

bool dsp_detach(int handle,
		void *proc_handle)
{
	struct proc_detach arg = {
		.proc_handle = proc_handle,
	};

	return DSP_SUCCEEDED(ioctl(handle, 9, &arg));
}

struct register_notify {
	void *proc_handle;
	unsigned int event_mask;
	unsigned int notify_type;
	struct dsp_notification *info;
};

bool dsp_register_notify(int handle,
			 void *proc_handle,
			 unsigned int event_mask,
			 unsigned int notify_type,
			 struct dsp_notification *info)
{
	struct register_notify arg = {
		.proc_handle = proc_handle,
		.event_mask = event_mask,
		.notify_type = notify_type,
		.info = info,
	};

	return DSP_SUCCEEDED(ioctl(handle, 15, &arg));
}

struct node_register_notify {
	void *node_handle;
	unsigned int event_mask;
	unsigned int notify_type;
	struct dsp_notification *info;
};

bool dsp_node_register_notify(int handle,
			      void *node_handle,
			      unsigned int event_mask,
			      unsigned int notify_type,
			      struct dsp_notification *info)
{
	struct node_register_notify arg = {
		.node_handle = node_handle,
		.event_mask = event_mask,
		.notify_type = notify_type,
		.info = info,
	};

	return DSP_SUCCEEDED(ioctl(handle, 35, &arg));
}

struct wait_for_events {
	struct dsp_notification **notifications;
	unsigned int count;
	unsigned int *ret_index;
	unsigned int timeout;
};

bool dsp_wait_for_events(int handle,
			 struct dsp_notification **notifications,
			 unsigned int count,
			 unsigned int *ret_index,
			 unsigned int timeout)
{
	struct wait_for_events arg = {
		.notifications = notifications,
		.count = count,
		.ret_index = ret_index,
		.timeout = timeout,
	};

	return DSP_SUCCEEDED(ioctl(handle, 5, &arg));
}

struct enum_node {
	unsigned int num;
	struct dsp_ndb_props *info;
	unsigned int info_size;
	unsigned int *ret_num;
};

bool dsp_enum(int handle,
	      unsigned int num,
	      struct dsp_ndb_props *info,
	      size_t info_size,
	      unsigned int *ret_num)
{
	struct enum_node arg = {
		.num = num,
		.info = info,
		.info_size = info_size,
		.ret_num = ret_num,
	};

	return DSP_SUCCEEDED(ioctl(handle, 1, &arg));
}

struct register_object {
	const dsp_uuid_t *uuid;
	enum dsp_dcd_object_type type;
	const char *path;
};

bool dsp_register(int handle,
		  const dsp_uuid_t *uuid,
		  enum dsp_dcd_object_type type,
		  const char *path)
{
	struct register_object arg = {
		.uuid = uuid,
		.type = type,
		.path = path,
	};

	return DSP_SUCCEEDED(ioctl(handle, 3, &arg));
}

struct unregister_object {
	dsp_uuid_t *uuid;
	enum dsp_dcd_object_type type;
};

bool dsp_unregister(int handle,
		    dsp_uuid_t *uuid,
		    enum dsp_dcd_object_type type)
{
	struct unregister_object arg = {
		.uuid = uuid,
		.type = type,
	};

	return DSP_SUCCEEDED(ioctl(handle, 4, &arg));
}

struct node_create {
	void *node_handle;
};

bool dsp_node_create(int handle,
		     void *node_handle)
{
	struct node_create arg = {
		.node_handle = node_handle,
	};

	return DSP_SUCCEEDED(ioctl(handle, 28, &arg));
}

struct node_run {
	void *node_handle;
};

bool dsp_node_run(int handle,
		  void *node_handle)
{
	struct node_run arg = {
		.node_handle = node_handle,
	};

	return DSP_SUCCEEDED(ioctl(handle, 36, &arg));
}

struct node_terminate {
	void *node_handle;
	unsigned long *status;
};

bool dsp_node_terminate(int handle,
			void *node_handle,
			unsigned long *status)
{
	struct node_terminate arg = {
		.node_handle = node_handle,
		.status = status,
	};

	return DSP_SUCCEEDED(ioctl(handle, 37, &arg));
}

struct node_put_message {
	void *node_handle;
	const dsp_msg_t *message;
	unsigned int timeout;
};

bool dsp_node_put_message(int handle,
			  void *node_handle,
			  const dsp_msg_t *message,
			  unsigned int timeout)
{
	struct node_put_message arg = {
		.node_handle = node_handle,
		.message = message,
		.timeout = timeout,
	};

	return DSP_SUCCEEDED(ioctl(handle, 34, &arg));
}

struct node_get_message {
	void *node_handle;
	dsp_msg_t *message;
	unsigned int timeout;
};

bool dsp_node_get_message(int handle,
			  void *node_handle,
			  dsp_msg_t *message,
			  unsigned int timeout)
{
	struct node_get_message arg = {
		.node_handle = node_handle,
		.message = message,
		.timeout = timeout,
	};

	return DSP_SUCCEEDED(ioctl(handle, 32, &arg));
}

struct node_delete {
	void *node_handle;
};

static inline bool dsp_node_delete(int handle,
				   void *node_handle)
{
	struct node_delete arg = {
		.node_handle = node_handle,
	};

	return DSP_SUCCEEDED(ioctl(handle, 29, &arg));
}

struct node_allocate {
	void *proc_handle;
	const dsp_uuid_t *node_id;
	const void *cb_data;
	struct dsp_node_attr_in *attrs;
	void **ret_node;
};

bool dsp_node_allocate(int handle,
		       void *proc_handle,
		       const dsp_uuid_t *node_uuid,
		       const void *cb_data,
		       struct dsp_node_attr_in *attrs,
		       void **ret_node)
{
	void *node_handle = NULL;
	struct node_allocate arg = {
		.proc_handle = proc_handle,
		.node_id = node_uuid,
		.cb_data = cb_data,
		.attrs = attrs,
		.ret_node = &node_handle,
	};

	if (!DSP_SUCCEEDED(ioctl(handle, 24, &arg))) {
		free(attrs->gpp_va);
		attrs->gpp_va = NULL;
		return false;
	}

	*ret_node = node_handle;

	return true;
}

bool dsp_node_free(int handle,
		   void *node_handle)
{
	dsp_node_delete(handle, node_handle);

	return true;
}

struct reserve_mem {
	void *proc_handle;
	unsigned long size;
	void **addr;
};

bool dsp_reserve(int handle,
		 void *proc_handle,
		 unsigned long size,
		 void **addr)
{
	struct reserve_mem arg = {
		.proc_handle = proc_handle,
		.size = size,
		.addr = addr,
	};

	return DSP_SUCCEEDED(ioctl(handle, 17, &arg));
}

struct unreserve_mem {
	void *proc_handle;
	unsigned long size;
	void *addr;
};

bool dsp_unreserve(int handle,
		   void *proc_handle,
		   void *addr)
{
	struct unreserve_mem arg = {
		.proc_handle = proc_handle,
		.addr = addr,
	};

	return DSP_SUCCEEDED(ioctl(handle, 18, &arg));
}

struct map_mem {
	void *proc_handle;
	void *mpu_addr;
	unsigned long size;
	void *req_addr;
	void **ret_map_addr;
	unsigned long attr;
};

bool dsp_map(int handle,
	     void *proc_handle,
	     void *mpu_addr,
	     unsigned long size,
	     void *req_addr,
	     void *ret_map_addr,
	     unsigned long attr)
{
	struct map_mem arg = {
		.proc_handle = proc_handle,
		.mpu_addr = mpu_addr,
		.size = size,
		.req_addr = req_addr,
		.ret_map_addr = ret_map_addr,
		.attr = attr,
	};

	return DSP_SUCCEEDED(ioctl(handle, 19, &arg));
}

struct unmap_mem {
	void *proc_handle;
	unsigned long size;
	void *map_addr;
};

bool dsp_unmap(int handle,
	       void *proc_handle,
	       void *map_addr)
{
	struct unmap_mem arg = {
		.proc_handle = proc_handle,
		.map_addr = map_addr,
	};

	return DSP_SUCCEEDED(ioctl(handle, 20, &arg));
}

struct flush_mem {
	void *proc_handle;
	void *mpu_addr;
	unsigned long size;
	unsigned long flags;
};

bool dsp_flush(int handle,
	       void *proc_handle,
	       void *mpu_addr,
	       unsigned long size,
	       unsigned long flags)
{
	struct flush_mem arg = {
		.proc_handle = proc_handle,
		.mpu_addr = mpu_addr,
		.size = size,
		.flags = flags,
	};

	return DSP_SUCCEEDED(ioctl(handle, 21, &arg));
}

struct invalidate_mem {
	void *proc_handle;
	void *mpu_addr;
	unsigned long size;
};

bool dsp_invalidate(int handle,
		    void *proc_handle,
		    void *mpu_addr,
		    unsigned long size)
{
	struct invalidate_mem arg = {
		.proc_handle = proc_handle,
		.mpu_addr = mpu_addr,
		.size = size,
	};

	return DSP_SUCCEEDED(ioctl(handle, 23, &arg));
}
