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

#ifndef DSP_BRIDGE_H
#define DSP_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define ALLOCATE_HEAP

#define DSP_SUCCEEDED(x) ((int)(x) >= 0)
#define DSP_FAILED(x) ((int)(x) < 0)

#define DSP_MMUFAULT 0x00000010
#define DSP_SYSERROR 0x00000020
#define DSP_NODEMESSAGEREADY 0x00000200

typedef struct {
	uint32_t field_1;
	uint16_t field_2;
	uint16_t field_3;
	uint8_t field_4;
	uint8_t field_5;
	uint8_t field_6[6];
} dsp_uuid_t;

typedef struct {
	void *handle;
	void *heap;
	void *msgbuf_addr;
	size_t msgbuf_size;
} dsp_node_t;

/* note: cmd = 0x20000000 has special handling */
typedef struct {
	uint32_t cmd;
	uint32_t arg_1;
	uint32_t arg_2;
} dsp_msg_t;

struct dsp_notification {
	char *name;
	void *handle;
};

struct dsp_node_attr_in {
	unsigned long cb;
	int priority;
	unsigned int timeout;
	unsigned int profile_id;
	unsigned int heap_size;
	void *gpp_va;
};

enum dsp_dcd_object_type {
	DSP_DCD_NODETYPE,
	DSP_DCD_PROCESSORTYPE,
	DSP_DCD_LIBRARYTYPE,
	DSP_DCD_CREATELIBTYPE,
	DSP_DCD_EXECUTELIBTYPE,
	DSP_DCD_DELETELIBTYPE,
};

enum dsp_node_type {
	DSP_NODE_DEVICE,
	DSP_NODE_TASK,
	DSP_NODE_DAISSOCKET,
	DSP_NODE_MESSAGE,
};

#ifdef ALLOCATE_HEAP
struct DSP_RESOURCEREQMTS {
	unsigned long cbStruct;
	unsigned int uStaticDataSize;
	unsigned int uGlobalDataSize;
	unsigned int uProgramMemSize;
	unsigned int uWCExecutionTime;
	unsigned int uWCPeriod;
	unsigned int uWCDeadline;
	unsigned int uAvgExectionTime;
	unsigned int uMinimumPeriod;
};

struct DSP_NODEPROFS {
	unsigned int ulHeapSize;
};

struct dsp_ndb_props {
	unsigned long cbStruct;
	dsp_uuid_t uiNodeID;
	char acName[32];
	enum dsp_node_type uNodeType;
	unsigned int bCacheOnGPP;
	struct DSP_RESOURCEREQMTS dspResourceReqmts;
	int iPriority;
	unsigned int uStackSize;
	unsigned int uSysStackSize;
	unsigned int uStackSeg;
	unsigned int uMessageDepth;
	unsigned int uNumInputStreams;
	unsigned int uNumOutputStreams;
	unsigned int uTimeout;
	unsigned int uCountProfiles; /* Number of supported profiles */
	struct DSP_NODEPROFS aProfiles[16];	/* Array of profiles */
	unsigned int uStackSegName; /* Stack Segment Name */
};
#endif

enum dsp_resource {
	DSP_RESOURCE_DYNDARAM = 0,
	DSP_RESOURCE_DYNSARAM,
	DSP_RESOURCE_DYNEXTERNAL,
	DSP_RESOURCE_DYNSRAM,
	DSP_RESOURCE_PROCLOAD,
};

struct dsp_info {
	unsigned long cb;
	enum dsp_resource type;
	union {
		unsigned long resource;
		struct {
			unsigned long size;
			unsigned long total_free_size;
			unsigned long len_max_free_block;
			unsigned long free_blocks;
			unsigned long alloc_blocks;
		} mem;
		struct {
			unsigned long load;
			unsigned long pred_load;
			unsigned long freq;
			unsigned long pred_freq;
		} proc;
	} result;
};

enum dsp_connect_type {
	CONNECTTYPE_NODEOUTPUT,
	CONNECTTYPE_GPPOUTPUT,
	CONNECTTYPE_NODEINPUT,
	CONNECTTYPE_GPPINPUT
};

struct dsp_stream_connect {
	unsigned long cb;
	enum dsp_connect_type type;
	unsigned int index;
	void *node_handle;
	dsp_uuid_t node_id;
	unsigned int stream_index;
};

enum dsp_node_state {
	NODE_ALLOCATED,
	NODE_CREATED,
	NODE_RUNNING,
	NODE_PAUSED,
	NODE_DONE
};

struct dsp_node_info {
	unsigned long cb;
	struct dsp_ndb_props props;
	unsigned int priority;
	enum dsp_node_state state;
	void *owner;
	unsigned int num_streams;
	struct dsp_stream_connect streams[16];
	unsigned int node_env;
};

struct dsp_node_attr {
	unsigned long cb;
	struct dsp_node_attr_in attr_in;
	unsigned long inputs;
	unsigned long outputs;
	struct dsp_node_info info;
};

int dsp_open(void);

int dsp_close(int handle);

bool dsp_attach(int handle,
		unsigned int num,
		const void *info,
		void **ret_handle);

bool dsp_detach(int handle,
		void *proc_handle);

bool dsp_node_allocate(int handle,
		       void *proc_handle,
		       const dsp_uuid_t *node_uuid,
		       const void *cb_data,
		       struct dsp_node_attr_in *attrs,
		       dsp_node_t **ret_node);

bool dsp_node_free(int handle,
		   dsp_node_t *node);

bool dsp_node_create(int handle,
		     dsp_node_t *node);

bool dsp_node_run(int handle,
		  dsp_node_t *node);

bool dsp_node_terminate(int handle,
			dsp_node_t *node,
			unsigned long *status);

bool dsp_node_put_message(int handle,
			  dsp_node_t *node,
			  const dsp_msg_t *message,
			  unsigned int timeout);

bool dsp_node_get_message(int handle,
			  dsp_node_t *node,
			  dsp_msg_t *message,
			  unsigned int timeout);

bool dsp_reserve(int handle,
		 void *proc_handle,
		 unsigned long size,
		 void **addr);

bool dsp_unreserve(int handle,
		   void *proc_handle,
		   void *addr);

bool dsp_map(int handle,
	     void *proc_handle,
	     void *mpu_addr,
	     unsigned long size,
	     void *req_addr,
	     void *ret_map_addr,
	     unsigned long attr);

bool dsp_unmap(int handle,
	       void *proc_handle,
	       void *map_addr);

bool dsp_flush(int handle,
	       void *proc_handle,
	       void *mpu_addr,
	       unsigned long size,
	       unsigned long flags);

bool dsp_invalidate(int handle,
		    void *proc_handle,
		    void *mpu_addr,
		    unsigned long size);

bool dsp_register_notify(int handle,
			 void *proc_handle,
			 unsigned int event_mask,
			 unsigned int notify_type,
			 struct dsp_notification *info);

bool dsp_node_register_notify(int handle,
			      dsp_node_t *node,
			      unsigned int event_mask,
			      unsigned int notify_type,
			      struct dsp_notification *info);

bool dsp_wait_for_events(int handle,
			 struct dsp_notification **notifications,
			 unsigned int count,
			 unsigned int *ret_index,
			 unsigned int timeout);

bool dsp_enum(int handle,
	      unsigned int num,
	      struct dsp_ndb_props *info,
	      unsigned int info_size,
	      unsigned int *ret_num);

bool dsp_register(int handle,
		  const dsp_uuid_t *uuid,
		  enum dsp_dcd_object_type type,
		  const char *path);

bool dsp_unregister(int handle,
		    dsp_uuid_t *uuid,
		    enum dsp_dcd_object_type type);

bool dsp_proc_get_info(int handle,
		       void *proc_handle,
		       enum dsp_resource type,
		       struct dsp_info *info,
		       unsigned size);

static inline bool
dsp_send_message(int handle,
		 dsp_node_t *node,
		 uint32_t cmd,
		 uint32_t arg_1,
		 uint32_t arg_2)
{
	dsp_msg_t msg;

	msg.cmd = cmd;
	msg.arg_1 = arg_1;
	msg.arg_2 = arg_2;

	return dsp_node_put_message(handle, node, &msg, -1);
}

bool dsp_node_get_attr(int handle,
		       dsp_node_t *node,
		       struct dsp_node_attr *attr,
		       size_t attr_size);

bool dsp_enum_nodes(int handle,
		    void *proc_handle,
		    void **node_table,
		    unsigned node_table_size,
		    unsigned *num_nodes,
		    unsigned *allocated);

#endif /* DSP_BRIDGE_H */
