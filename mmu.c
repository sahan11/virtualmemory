#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <string.h>
#include <limits.h>

// Define constants for message types and special page references
#define PSEND_TYPE 10
#define MMUTOPRO 20
#define INVALID_PAGE_REF -2
#define PAGEFAULT -1
#define PROCESS_OVER -9
#define PAGEFAULT_HANDLED 5
#define TERMINATED 10

// Page Table Entry (PTE)
typedef struct
{
	int fn;	   // Frame number
	int valid; // Valid bit
	int count; // Reference count
} ptb;

// Process Control Block (PCB) structure
typedef struct
{
	pid_t pid;	// Process ID
	int m;		// Memory size requirement
	int f_cnt;	// Count of frames allocated
	int f_allo; // Allocated frame
} pcb;

// Free Frame List structure
typedef struct
{
	int curr;	 // Current index
	int flist[]; // Array of free frames
} fl;

// structure for message buffer
struct msgbuf
{
	long mtype; // Message type
	int id;		// Process ID
	int pageno; // Page number
};

// structure for sending frame number in reply
struct pbuf_send
{
	long mtype; // Message type
	int fn;		// Frame number
};

// structure for sending notifications to scheduler
struct sch_send
{
	long mtype;	  // Message type
	char mbuf[1]; // Message buffer
};

int m, k; // Memory size and number of processes

int SM1_id, SM2_id; // Shared memory segment IDs
int MQ2_id, MQ3_id; // Message queue IDs
int pcbid;			// PCB shared memory segment ID

pcb *pcbptr;			// Pointer to PCB shared memory
ptb *ptb_ptr;			// Pointer to page table shared memory
fl *freelist_ptr; // Pointer to free frame list shared memory
int flag = 1;			// Flag for termination condition
int ctr = 0;			// Global timestamp counter
int *pffreq;			// Page fault frequency array
FILE *file;			// File pointer for result file

int readRequest(int *id) // Read request from process
{
	struct msgbuf mbuf;
	int len = sizeof(struct msgbuf) - sizeof(long);
	memset(&mbuf, 0, sizeof(mbuf));

	int rst = msgrcv(MQ3_id, &mbuf, len, PSEND_TYPE, 0);
	if (rst == -1)
	{
		if (errno == EINTR)
			return -1;
		perror("msgrcv");
		exit(EXIT_FAILURE);
	}
	*id = mbuf.id;
	return mbuf.pageno;
}

void sendReply(int id, int fn) // Send reply to process
{
	struct pbuf_send mbuf;
	mbuf.mtype = id + MMUTOPRO;
	mbuf.fn = fn;
	int len = sizeof(struct msgbuf) - sizeof(long);
	int rst = msgsnd(MQ3_id, &mbuf, len, 0);
	if (rst == -1)
	{
		perror("msgsnd");
		exit(EXIT_FAILURE);
	}
}

void notifySched(int type) // Notify scheduler
{
	struct sch_send mbuf;
	mbuf.mtype = type;
	int len = sizeof(struct msgbuf) - sizeof(long);
	int rst = msgsnd(MQ2_id, &mbuf, len, 0);
	if (rst == -1)
	{
		perror("msgsnd");
		exit(EXIT_FAILURE);
	}
}

int handlePF(int id, int pageno) // Handle page fault
{
	if (freelist_ptr->curr == -1 || pcbptr[id].f_cnt <= pcbptr[id].f_allo)
	{
		int min = INT_MAX, idx = -1;
		int val = 0;
		for (int i = 0; i < pcbptr[id].m; i++)
		{
			int j = id * m + i;
			if (ptb_ptr[j].valid == 1 && ptb_ptr[j].count < min)
			{
				min = ptb_ptr[j].count;
				val = ptb_ptr[j].fn;
				idx = i;
			}
		}
		ptb_ptr[id * m + idx].valid = 0;
		return val;
	}
	else
	{
		int fn = freelist_ptr->flist[freelist_ptr->curr];
		freelist_ptr->curr -= 1;
		return fn;
	}
}

void clearPages(int id) // Release all frames allocated to process
{
	for (int k = 0; k < pcbptr[id].m; k++)
	{
		if (ptb_ptr[id * m + k].valid == 1)
		{
			freelist_ptr->flist[freelist_ptr->curr + 1] = ptb_ptr[id * m + k].fn;
			freelist_ptr->curr += 1;
		}
	}
}

int MemReq() //	Handle memory request from process
{
	pcbptr = (pcb *)(shmat(pcbid, NULL, 0));
	ptb_ptr = (ptb *)(shmat(SM1_id, NULL, 0));
	freelist_ptr = (fl *)(shmat(SM2_id, NULL, 0));

	int id = -1;
	int pageno = readRequest(&id);

	if (pageno == -1 && id == -1)
		return 0;

	int i = id;
	if (pageno == PROCESS_OVER)
	{
		clearPages(id);
		notifySched(TERMINATED);
		return 0;
	}
	ctr++;
	printf("Page reference : (%d,%d,%d)\n", ctr, id, pageno);
	fprintf(file, "Page reference : (%d,%d,%d)\n", ctr, id, pageno);

	if (pcbptr[id].m < pageno || pageno < 0)
	{
		printf("Invalid Page Reference : (%d %d)\n", id, pageno);
		fprintf(file, "Invalid Page Reference : (%d %d)\n", id, pageno);

		sendReply(id, INVALID_PAGE_REF);
		printf("Process %d: TRYING TO ACCESS INVALID PAGE REFERENCE %d\n", id, pageno);
		clearPages(id);
		notifySched(TERMINATED);
		// Invalid reference
	}
	else
	{
		if (ptb_ptr[i * m + pageno].valid == 0)
		{
			// PAGE FAULT
			printf("Page Fault : (%d, %d)\n", id, pageno);
			fprintf(file, "Page Fault : (%d, %d)\n", id, pageno);

			pffreq[id] += 1;
			sendReply(id, -1);
			int fno = handlePF(id, pageno);
			ptb_ptr[i * m + pageno].valid = 1;
			ptb_ptr[i * m + pageno].count = ctr;
			ptb_ptr[i * m + pageno].fn = fno;

			notifySched(PAGEFAULT_HANDLED);
		}
		else
		{
			sendReply(id, ptb_ptr[i * m + pageno].fn);
			ptb_ptr[i * m + pageno].count = ctr;
			// FRAME FOUND
		}
	}
	shmdt(ptb_ptr);
	shmdt(pcbptr);
	shmdt(freelist_ptr);
	return 1;
}

void signal_handler(int sig)
{
	flag = 0; // Termination flag
}

int main(int argc, char const *argv[])
{
	MQ2_id = atoi(argv[1]);
	MQ3_id = atoi(argv[2]);
	SM1_id = atoi(argv[3]);
	SM2_id = atoi(argv[4]);
	pcbid = atoi(argv[5]);
	m = atoi(argv[6]); // Memory size
	k = atoi(argv[7]); // Number of processes

	signal(SIGUSR2, signal_handler);
	pffreq = (int *)malloc(k * sizeof(int)); // page fault frequency
	for (int i = 0; i < k; i++)
		pffreq[i] = 0;

	file = fopen("result.txt", "w");

	// Handle memory requests until termination signal is received
	while (flag)
		MemReq();

	printf("Page fault Count for each Process:\n");
	printf("Process Id\tFreq\n");
	fprintf(file, "Page fault Count for each Process:\n");
	fprintf(file, "Process_id\tFreq\n");

	for (int i = 0; i < k; i++)
	{
		printf("%d\t%d\n", i, pffreq[i]);
		fprintf(file, "%d\t%d\n", i, pffreq[i]);
	}
	fclose(file);
	return 0;
}