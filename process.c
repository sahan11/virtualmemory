#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <signal.h>

#define MAX_BUFFER_SIZE 100
#define MAX_PAGES 1000
#define TOSCH 10
#define FROMSCH 20 // FROMSCH+id will be used to read msg from sch
#define TOMMU 10
#define FROMMMU 20 // FROMMMU+id will be used to read msg from MMU

int pg_no[MAX_PAGES];
int no_of_pages;

typedef struct send_mmu
{
	long mtype; // Message type
	int id;
	int pageno;
} send_mmu;

typedef struct recv_mmu
{
	long mtype; // Message type
	int frameno;
} recv_mmu;

typedef struct msgbuf
{
	long mtype; // Message type
	int id;
} msgbuf;

void conv_ref_pg_no(char *refs)
{
	const char s[2] = "|";
	char *token;
	token = strtok(refs, s);
	while (token != NULL)
	{
		pg_no[no_of_pages] = atoi(token);
		no_of_pages++;
		token = strtok(NULL, s);
	}
}

int mmu_send_msg(int qid, struct send_mmu *qbuf)
{
	int res;
	int len = sizeof(struct send_mmu) - sizeof(long);
	if ((res = msgsnd(qid, qbuf, len, 0)) == -1)
	{
		perror("Error in sending message");
		exit(1);
	}
	return (res);
}

int mmu_read_msg(int qid, long type, struct recv_mmu *qbuf)
{
	int res;
	int len = sizeof(struct recv_mmu) - sizeof(long);
	if ((res = msgrcv(qid, qbuf, len, type, 0)) == -1)
	{
		perror("Error in receiving message");
		exit(1);
	}
	return (res);
}

int send_msg(int qid, struct msgbuf *qbuf)
{
	int res;
	int len = sizeof(struct msgbuf) - sizeof(long);
	if ((res = msgsnd(qid, qbuf, len, 0)) == -1)
	{
		perror("Error in sending message");
		exit(1);
	}
	return (res);
}

int read_msg(int qid, long type, struct msgbuf *qbuf)
{
	int res;
	int len = sizeof(struct msgbuf) - sizeof(long);
	if ((res = msgrcv(qid, qbuf, len, type, 0)) == -1)
	{
		perror("Error in receiving message");
		exit(1);
	}
	return (res);
}

int main(int argc, char *argv[]) // argv[] ={id,mq1,mq3,ref_string}
{
	no_of_pages = 0;
	int id = atoi(argv[1]);
	int mq1_k = atoi(argv[2]);
	int mq3_k = atoi(argv[3]);

	conv_ref_pg_no(argv[4]);
	int mq1 = msgget(mq1_k, 0666);
	int mq3 = msgget(mq3_k, 0666);
	if (mq1 == -1 || mq3 == -1)
	{
		perror("Message Queue creation failed");
		exit(1);
	}

	printf("Process id = %d\n", id);

	// sending to scheduler
	msgbuf msg_send;
	msg_send.mtype = TOSCH;
	msg_send.id = id;
	send_msg(mq1, &msg_send);

	// Wait until msg receive from scheduler
	msgbuf msg_recv;
	read_msg(mq1, FROMSCH + id, &msg_recv);

	send_mmu mmu_send;
	recv_mmu mmu_recv;
	int cpg = 0; // counter for page number array
	while (cpg < no_of_pages)
	{
		// sending msg to mmu the page number
		printf("Sent request for %d page number\n", pg_no[cpg]);
		mmu_send.mtype = TOMMU;
		mmu_send.id = id;
		mmu_send.pageno = pg_no[cpg];
		mmu_send_msg(mq3, &mmu_send);

		mmu_read_msg(mq3, FROMMMU + id, &mmu_recv);
		if (mmu_recv.frameno >= 0)
		{
			printf("Frame number from MMU received for process %d: %d\n", id, mmu_recv.frameno);
			cpg++;
		}
		else if (mmu_recv.frameno == -1) // here cpg will not be incremented
		{
			printf("Page fault occured for process %d\n", id);
			// read_msg(mq1, FROMSCH + id, &msg_recv);
		}
		else if (mmu_recv.frameno == -2)
		{
			printf("Invalid page reference for process %d terminating ...\n", id);
			exit(1);
		}
	}
	printf("Process %d Terminated successfly\n", id);
	mmu_send.pageno = -9;
	mmu_send.id = id;
	mmu_send.mtype = TOMMU;
	mmu_send_msg(mq3, &mmu_send);

	exit(1);
	return 0;
}