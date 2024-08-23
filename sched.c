#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/msg.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>

#define MAX_BUFFER_SIZE 100
#define MAX_PAGES 1000
#define MAX_PROCESS 1000
#define FROMPROCESS 10
#define TOPROCESS 20 // TOPROCESS+id will be used to read msg from sch
#define FROMMMU 20

#define PAGEFAULT_HANDLED 5
#define TERMINATED 10

int k; // no. of processes
typedef struct _sch_send
{
	long mtype;
	char mbuf[1];
} sch_send;

typedef struct msgbuf
{
	long mtype;
	int id;
} msgbuf;

int send_msg(int qid, struct msgbuf *qbuf)
{
	int res;
	int len = sizeof(struct msgbuf) - sizeof(long); // size of the structure minus sizeof(mtype)

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
	int len = sizeof(struct msgbuf) - sizeof(long); //  size of the structure minus sizeof(mtype)

	if ((res = msgrcv(qid, qbuf, len, type, 0)) == -1)
	{
		perror("Error in receiving message");
		exit(1);
	}
	return (res);
}

int mmu_read_msg(int qid, long type, sch_send *qbuf)
{
	int res;
	int len = sizeof(sch_send) - sizeof(long); // size of the structure minus sizeof(mtype)
	if ((res = msgrcv(qid, qbuf, len, type, 0)) == -1)
	{
		perror("Error in receiving message");
		exit(1);
	}
	return (res);
}

int main(int argc, char *argv[])
{
	int mq1_key = atoi(argv[1]);
	int mq2_key = atoi(argv[2]);
	k = atoi(argv[3]);
	int master_pid = atoi(argv[4]);
	int mq1 = msgget(mq1_key, 0666);
	int mq2 = msgget(mq2_key, 0666);

	if (mq1 == -1 || mq2 == -1)
	{
		perror("Message Queue creation failed");
		exit(1);
	}
	printf("Total No. of Process received = %d\n", k);

	int terminated_process = 0;
	msgbuf msg_send, msg_recv;
	while (1)
	{
		read_msg(mq1, FROMPROCESS, &msg_recv);
		int curr_id = msg_recv.id;

		msg_send.mtype = TOPROCESS + curr_id;
		send_msg(mq1, &msg_send);

		// recv messages from mmu
		sch_send mmu_recv;
		mmu_read_msg(mq2, 0, &mmu_recv);
		// printf("received %ld\n", mmu_recv.mtype);

		if (mmu_recv.mtype == PAGEFAULT_HANDLED)
		{
			msg_send.mtype = FROMPROCESS;
			msg_send.id = curr_id;
			send_msg(mq1, &msg_send);
		}
		else if (mmu_recv.mtype == TERMINATED)
		{
			terminated_process++;
		}
		else
		{
			perror("Wrong message from mmu\n");
			exit(1);
		}
		if (terminated_process == k)
			break;
	}
	kill(master_pid, SIGUSR1);
	pause();
	printf("Scheduler terminating ...\n");
	exit(1);
}