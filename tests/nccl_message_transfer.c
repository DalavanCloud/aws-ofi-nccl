/*
 * Copyright (c) 2018 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

/*
 * This test validates functionality of NCCL's connection establishment and
 * data transfer APIs
 */

#include "test-common.h"

int main(int argc, char* argv[])
{
	int rank, proc_name;
	char name[MPI_MAX_PROCESSOR_NAME];

	/* Plugin defines */
	int ndev;
	sendComm_t *sComm = NULL;
	listenComm_t *lComm = NULL;
	recvComm_t *rComm = NULL;
	ncclNet_t *extNet = NULL;
	char src_handle[NCCL_NET_HANDLE_MAXSIZE] = {0};

	ncclDebugLogger_t ofi_log_function;
	ofi_log_function = logger;

	/* Initialisation for data transfer */
	nccl_ofi_req_t *req[NUM_REQUESTS] = {NULL};
	int req_completed[NUM_REQUESTS] = {0};
	int inflight_reqs = NUM_REQUESTS;
	int *send_buf = NULL;
	int *recv_buf = NULL;
	int done, received_size, idx;

	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	MPI_Get_processor_name(name, &proc_name);

	/* Get external Network from NCCL-OFI library */
	extNet = get_extNet();
	if (extNet == NULL)
		return -1;

	/* TODO: Check return codes for transport layer APIs */
	/* Init API */
	extNet->init(&logger);
	NCCL_OFI_INFO(NCCL_NET, "Process rank %d started. NCCLNet device used on %s is %s.",
		      rank, name, extNet->name);

	/* Devices API */
	extNet->devices(&ndev);
	NCCL_OFI_INFO(NCCL_NET, "Received %d network devices", ndev);

	/* Listen API */
	char handle[NCCL_NET_HANDLE_MAXSIZE];
	NCCL_OFI_INFO(NCCL_NET, "Server: Listening on dev 0");
	extNet->listen(0, (void *)&handle, (void **)&lComm);

	if (rank == 0) {

		/* MPI send */
		MPI_Send(&handle, NCCL_NET_HANDLE_MAXSIZE, MPI_CHAR, 1, 0, MPI_COMM_WORLD);

		/* MPI recv */
		MPI_Recv((void *)src_handle, NCCL_NET_HANDLE_MAXSIZE, MPI_CHAR, (rank + 1), 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

		/* Connect API */
		NCCL_OFI_INFO(NCCL_NET, "Send connection request to rank %d", rank + 1);
		extNet->connect(0, (void *)src_handle, (void **)&sComm);

		/* Accept API */
		NCCL_OFI_INFO(NCCL_NET, "Server: Start accepting requests");
		extNet->accept((void *)lComm, (void **)&rComm);
		NCCL_OFI_INFO(NCCL_NET, "Successfully accepted connection from rank %d",
			      rank + 1);

		/* Send NUM_REQUESTS to Rank 1 */
		NCCL_OFI_INFO(NCCL_NET, "Sent %d requests to rank %d", NUM_REQUESTS,
			      rank + 1);
		for (idx = 0; idx < NUM_REQUESTS; idx++) {
			send_buf = calloc(SEND_SIZE, sizeof(int));
			extNet->isend((void *)sComm, (void *)send_buf, SEND_SIZE,
				       0, (void **)&req[idx]);
		}
	}
	else if (rank == 1) {

		/* MPI recv */
		MPI_Recv((void *)src_handle, NCCL_NET_HANDLE_MAXSIZE, MPI_CHAR, (rank - 1), 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

		/* MPI send */
		MPI_Send((void *)handle, NCCL_NET_HANDLE_MAXSIZE, MPI_CHAR, (rank - 1), 0, MPI_COMM_WORLD);

		/* Connect API */
		NCCL_OFI_INFO(NCCL_NET, "Send connection request to rank %d", rank - 1);
		extNet->connect(0, (void *)src_handle, (void **)&sComm);

		/* Accept API */
		NCCL_OFI_INFO(NCCL_NET, "Server: Start accepting requests");
		extNet->accept((void *)lComm, (void **)&rComm);
		NCCL_OFI_INFO(NCCL_NET, "Successfully accepted connection from rank %d",
			      rank - 1);

		/* Receive NUM_REQUESTS from Rank 0 */
		NCCL_OFI_INFO(NCCL_NET, "Rank %d posting %d receive buffers", rank,
			      NUM_REQUESTS);
		for (idx = 0; idx < NUM_REQUESTS; idx++) {
			recv_buf = calloc(RECV_SIZE, sizeof(int));
			extNet->irecv((void *)rComm, (void *)recv_buf,
					RECV_SIZE, 0, (void **)&req[idx]);
		}
	}

	/* Test for completions */
	while (true) {
		for (idx = 0; idx < NUM_REQUESTS; idx++) {
			if (req_completed[idx])
				continue;

			extNet->test((void *)req[idx], &done, &received_size);
			if (done) {
				inflight_reqs--;
				req_completed[idx] = 1;
			}
		}
		if (inflight_reqs == 0)
			break;
	}
	NCCL_OFI_INFO(NCCL_NET, "Got completions for %d requests for rank %d",
		      NUM_REQUESTS, rank);

	extNet->closeListen((void *)lComm);
	extNet->closeSend((void *)sComm);
	extNet->closeRecv((void *)rComm);

	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();

	return 0;
}
