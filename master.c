#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <limits.h>
#include <math.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <sys/wait.h>

// Page Table Entry
typedef struct
{
	int fn;	   // frame number
	int valid; // valid bit
	int count; // Reference count
} ptb;

// Process Control Block
typedef struct
{
	pid_t pid;	// process id
	int m;		// memory size required
	int f_cnt;	// frame count
	int f_allo; // frames allocated
} pcb;

// Free Frame List
typedef struct
{
	int curr;	 // current index
	int flist[]; // free frame array
} freelist;

int k, m, f;		 // Number of processes, frames, and memory size
int pid, spid, mpid; // Process IDs
int flag = 0;		 // Flag to indicate termination

key_t SM2_key, SM1_key;			 // Keys for shared memory segments
key_t MQ1_key, MQ2_key, MQ3_key; // Keys for message queues
key_t pcbkey;					 // Key for PCB shared memory segment

int SM1_id, SM2_id;			// Shared memory segment IDs
int MQ1_id, MQ2_id, MQ3_id; // Message queue IDs
int pcbid;					// PCB shared memory segment ID

void exit_handler(int status);

void FreeFrameList() // Initialize the free frame list
{
	SM2_key = ftok("master.c", 56);
	if (SM2_key == -1)
	{
		perror("SM2_key");
		exit_handler(EXIT_FAILURE);
	}

	SM2_id = shmget(SM2_key, sizeof(freelist) + f * sizeof(int), 0666 | IPC_CREAT | IPC_EXCL);
	if (SM2_id == -1)
	{
		perror("free-shmget");
		exit_handler(EXIT_FAILURE);
	}

	freelist *SM2 = (freelist *)(shmat(SM2_id, NULL, 0));
	if (*((int *)SM2) == -1)
	{
		perror("freel-shmat");
		exit_handler(EXIT_FAILURE);
	}
	for (int i = 0; i < f; i++)
		SM2->flist[i] = i;

	SM2->curr = f - 1;

	if (shmdt(SM2) == -1)
	{
		perror("freel-shmdt");
		exit_handler(EXIT_FAILURE);
	}
}

void PageTable() // Initialize the page table
{
	SM1_key = ftok("master.c", 100);
	if (SM1_key == -1)
	{
		perror("SM1_key");
		exit_handler(EXIT_FAILURE);
	}

	SM1_id = shmget(SM1_key, m * sizeof(ptb) * k, 0666 | IPC_CREAT | IPC_EXCL);
	if (SM1_id == -1)
	{
		perror("pcb-shmget");
		exit_handler(EXIT_FAILURE);
	}

	ptb *SM1 = (ptb *)(shmat(SM1_id, NULL, 0));
	if (*(int *)SM1 == -1)
	{
		perror("pcb-shmat");
		exit_handler(EXIT_FAILURE);
	}

	for (int i = 0; i < k * m; i++)
		SM1[i].fn = -1, SM1[i].valid = 0;

	if (shmdt(SM1) == -1)
	{
		perror("pcb-shmdt");
		exit_handler(EXIT_FAILURE);
	}
}

void MessageQueue() // Initialize the message queues
{
	MQ1_key = ftok("master.c", 200);
	if (MQ1_key == -1)
	{
		perror("MQ1_key");
		exit_handler(EXIT_FAILURE);
	}

	MQ1_id = msgget(MQ1_key, 0666 | IPC_CREAT | IPC_EXCL);
	if (MQ1_id == -1)
	{
		perror("ready-msgget");
		exit_handler(EXIT_FAILURE);
	}

	MQ2_key = ftok("master.c", 300);
	if (MQ2_key == -1)
	{
		perror("MQ2_key");
		exit_handler(EXIT_FAILURE);
	}

	MQ2_id = msgget(MQ2_key, 0666 | IPC_CREAT | IPC_EXCL);
	if (MQ2_id == -1)
	{
		perror("msgq2-msgget");
		exit_handler(EXIT_FAILURE);
	}

	MQ3_key = ftok("master.c", 400);
	if (MQ3_key == -1)
	{
		perror("MQ3_key");
		exit_handler(EXIT_FAILURE);
	}
	MQ3_id = msgget(MQ3_key, 0666 | IPC_CREAT | IPC_EXCL);
	if (MQ3_id == -1)
	{
		perror("msgq3-msgget");
		exit_handler(EXIT_FAILURE);
	}
}

void PCB() // Initialize the Process Control Block
{
	pcbkey = ftok("master.c", 500);
	if (pcbkey == -1)
	{
		perror("pcbkey");
		exit_handler(EXIT_FAILURE);
	}

	pcbid = shmget(pcbkey, sizeof(pcb) * k, 0666 | IPC_CREAT | IPC_EXCL);
	if (pcbid == -1)
	{
		perror("pcb-shmget");
		exit_handler(EXIT_FAILURE);
	}

	pcb *ptr = (pcb *)(shmat(pcbid, NULL, 0));
	if (*(int *)ptr == -1)
	{
		perror("pcb-shmat");
		exit_handler(EXIT_FAILURE);
	}

	int page_cnt = 0;
	for (int i = 0; i < k; i++)
	{
		ptr[i].pid = i;
		ptr[i].m = rand() % m + 1;
		ptr[i].f_allo = 0;
		page_cnt += ptr[i].m;
	}

	int allo_frame = 0;
	// printf("total = %d, k = %d, f=  %d\n", page_cnt, k, f);
	int max = 0, idx = 0;
	for (int i = 0; i < k; i++)
	{
		ptr[i].pid = -1;
		int allo = (int)round(ptr[i].m * (f - k) / (float)page_cnt) + 1;
		if (ptr[i].m > max)
		{
			max = ptr[i].m;
			idx = i;
		}

		allo_frame = allo_frame + allo;
		ptr[i].f_cnt = allo;
	}
	ptr[idx].f_cnt += f - allo_frame;

	for (int i = 0; i < k; i++)
		printf("PID = %d m = %d f_cnt = %d\n", ptr[i].pid, ptr[i].m, ptr[i].f_cnt);

	if (shmdt(ptr) == -1)
	{
		perror("freel-shmdt");
		exit_handler(EXIT_FAILURE);
	}
}

void exit_handler(int status) // custom exit to remove shared memory
{
	if (shmctl(SM1_id, IPC_RMID, NULL) == -1)
		perror("shmctl-ptb");

	if (shmctl(SM2_id, IPC_RMID, NULL) == -1)
		perror("shmctl-freel");

	if (shmctl(pcbid, IPC_RMID, NULL) == -1)
		perror("shmctl-pcb");

	if (msgctl(MQ1_id, IPC_RMID, NULL) == -1)
		perror("msgctl-ready");

	if (msgctl(MQ2_id, IPC_RMID, NULL) == -1)
		perror("msgctl-msgq2");

	if (msgctl(MQ3_id, IPC_RMID, NULL) == -1)
		perror("msgctl-msgq3");

	exit(status);
}

void initProcess() // exec process
{
	pcb *ptr = (pcb *)(shmat(pcbid, NULL, 0));
	for (int i = 0; i < k; i++)
	{
		int reflen = rand() % (8 * ptr[i].m) + 2 * ptr[i].m + 1;
		char ref_string[m * 20 * 40];
		// printf("reflen = %d\n", reflen);
		int l = 0;
		for (int j = 0; j < reflen; j++)
		{
			int r = rand() % ptr[i].m;
			float p = (rand() % 100) / 100.0;

			if (p < 0.2)
				r = rand() % (1000 * m) + ptr[i].m;

			l += sprintf(ref_string + l, "%d|", r);
		}
		// printf("Ref string = %s\n", ref_string);

		if (fork() == 0)
		{
			char arg1[20], arg2[20], arg3[20];
			sprintf(arg1, "%d", i);
			sprintf(arg2, "%d", MQ1_key);
			sprintf(arg3, "%d", MQ3_key);
			execlp("./process", "./process", arg1, arg2, arg3, ref_string, (char *)(NULL));
			exit(0);
		}
		usleep(250 * 1000); // creates on a delay of 250ms
	}
}

void terminate_handler(int sig)
{
	sleep(1);
	kill(spid, SIGTERM);
	kill(mpid, SIGUSR2);
	sleep(2);
	flag = 1;
}

int main(int argc, char const *argv[])
{
	srand(time(NULL));
	signal(SIGUSR1, terminate_handler);
	signal(SIGINT, exit_handler);

	printf("Enter the number of processes, frames and memory size\n");
	scanf("%d %d %d", &k, &f, &m);

	pid = getpid();
	if (k <= 0 || m <= 0 || f <= 0 || f < k)
	{
		printf("Invalid input\n");
		exit_handler(EXIT_FAILURE);
	}

	PageTable();
	FreeFrameList();
	PCB();
	MessageQueue();

	if ((spid = fork()) == 0)
	{
		char arg1[20], arg2[20], arg3[20], arg4[20];
		sprintf(arg1, "%d", MQ1_key);
		sprintf(arg2, "%d", MQ2_key);
		sprintf(arg3, "%d", k);
		sprintf(arg4, "%d", pid);
		execlp("./scheduler", "./scheduler", arg1, arg2, arg3, arg4, (char *)(NULL));
		exit(0);
	}
	if ((mpid = fork()) == 0)
	{
		char arg1[20], arg2[20], arg3[20], arg4[20], arg5[20], arg6[20], arg7[20];
		sprintf(arg1, "%d", MQ2_id);
		sprintf(arg2, "%d", MQ3_id);
		sprintf(arg3, "%d", SM1_id);
		sprintf(arg4, "%d", SM2_id);
		sprintf(arg5, "%d", pcbid);
		sprintf(arg6, "%d", m);
		sprintf(arg7, "%d", k);
		execlp("./mmu", "./mmu", arg1, arg2, arg3, arg4, arg5, arg6, arg7, (char *)(NULL));
		exit(0);
	}
	initProcess();
	if (!flag)
		pause();
	exit_handler(EXIT_SUCCESS);
	return 0;
}