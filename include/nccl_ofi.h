/*
 * Copyright (c) 2018 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#ifndef NCCL_OFI_H_
#define NCCL_OFI_H_

#ifdef _cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <rdma/fabric.h>
#include <rdma/fi_errno.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_tagged.h>
#include <nccl.h>
#include <nccl_net.h>

#ifdef __GNUC__
#define OFI_LIKELY(x)	__builtin_expect((x), 1)
#define OFI_UNLIKELY(x)	__builtin_expect((x), 0)
#else
#define OFI_LIKELY(x)	(x)
#define OFI_UNLIKELY(x)	(x)
#endif

#define OFI_MAJOR_VERSION	(1)
#define OFI_MINOR_VERSION	(6)
#define ofi_version		FI_VERSION(OFI_MAJOR_VERSION, \
					   OFI_MINOR_VERSION)
#define MAX_PROV_INFO		(15)
#define MAX_BDF_LEN		(25)

/*
 * We have a limit of MAX_HANDLE_SIZE = 64 bytes. Therefore, we can only
 * support an endpoint name of maximum 56 bytes. We are using remaining
 * 8 bytes for tags.
 */
#define MAX_EP_ADDR		(56)

/*
 * For each tag, we use MSB as control bit and remaining
 * for identifying different rings. We look at mem_tag_format for
 * an endpoint to determine if provider is reserving any MSBs.
 */
#define OFI_HIGHEST_TAG_BIT		(0x1UL << 63)

/*
 * We are supporting minimum 2^32 rings per endpoint and reserving 1 bit
 * for marking control sends/recvs.
 */
#define MIN_TAG_BITS_FOR_RING_ID	(32 + 1)

/* This is twice the size of maximum inflight requests supported by NCCL */
#define NCCL_OFI_MAX_REQUESTS	256

/* NCCL OFI lock for concurrency */
pthread_mutex_t nccl_ofi_lock = PTHREAD_MUTEX_INITIALIZER;
/* Logger Function */
ncclDebugLogger_t ofi_log_function = NULL;

typedef enum nccl_ofi_req_state {
	NCCL_OFI_REQ_CREATED = 0,
	NCCL_OFI_REQ_PENDING,
	NCCL_OFI_REQ_COMPLETED,
	NCCL_OFI_REQ_ERROR,
} nccl_ofi_req_state_t;

typedef enum nccl_ofi_req_direction {
	NCCL_OFI_SEND = 1,
	NCCL_OFI_RECV,
} nccl_ofi_req_direction_t;

typedef struct stack {
	int *array;
	int top;
	int size;
} stack_t;

typedef struct free_list {
	/* Array of free buffers */
	void *buffers;

	/* Stack of free buffer indexes */
	stack_t *free_index;

	/* Size of buffers array */
	uint64_t size;
} free_list_t;

typedef struct listenComm {
	uint64_t tag;
	struct fid_ep *local_ep;
	int dev;
	bool accepted;
} listenComm_t;

typedef struct sendComm {
	int dev;
	uint64_t tag;
	uint64_t num_inflight_reqs;
	fi_addr_t remote_ep;
	struct fid_ep *local_ep;
	free_list_t *nccl_ofi_reqs_fl;
	free_list_t *pending_reqs_fl;
} sendComm_t;

typedef struct recvComm {
	int dev;
	uint64_t tag;
	uint64_t num_inflight_reqs;
	fi_addr_t remote_ep;
	struct fid_ep *local_ep;
	free_list_t *nccl_ofi_reqs_fl;
} recvComm_t;

typedef struct nccl_ofi_req {
	/* Associated Comm object */
	union {
		listenComm_t *lComm;
		sendComm_t *sComm;
		recvComm_t *rComm;
	};

	/* Buffer index */
	uint64_t buffer_index;

	/* Associated OFI Context */
	struct fi_context ctx;

	/* Associated Device ID */
	int dev;

	/* Size of completed request */
	size_t size;

	/* State of request */
	nccl_ofi_req_state_t state;

	/* Direction of request */
	nccl_ofi_req_direction_t direction;
} nccl_ofi_req_t;

typedef struct pending_req {
	/* Associated nccl_ofi_req */
	nccl_ofi_req_t *nccl_ofi_req;

	/* Send/Recv Metadata */
	void *data;
	size_t len;
	int type;
} pending_req_t;

typedef struct pending_reqs_q_elem {
	struct pending_reqs_q_elem *next;

	/* Buffer index */
	uint64_t buffer_index;

	/* Pending request to retry */
	pending_req_t pending_req;
} pending_reqs_q_elem_t;

typedef struct pending_reqs_q {
	pending_reqs_q_elem_t *head;
	pending_reqs_q_elem_t *tail;
} pending_reqs_q_t;

typedef struct nccl_ofi {
	/* Current available tag ID */
	uint64_t tag;

	/* Maximum supported tag ID */
	uint64_t max_tag;

	/* Count of CQEs to read from CQ */
	uint64_t num_cqes;

	/* Provider name */
	char *prov_name;

	/* Fabric handle */
	struct fid_fabric *fabric;

	/* Access Domain handle */
	struct fid_domain *domain;

	/* Endpoint handle to communicate to */
	struct fid_ep *ep;

	/* Address vector handle */
	struct fid_av *av;

	/* Completion Queue handle */
	struct fid_cq *cq;

	/* Pending requests queue */
	pending_reqs_q_t *pending_reqs_q;
} nccl_ofi_t;

#ifdef _cplusplus
} // End extern "C"
#endif

#endif // End NCCL_OFI_H_
