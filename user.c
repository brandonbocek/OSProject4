
#include "scheduler.h"

const static unsigned long long int min = IO_INTERRUPT_CHANCE * 100000000;

FILE *fp;

static int fileLinesWritten = 0;
static int msgid_sending, msgid_receiving, msgid_critical, shmid, pcbid;

static struct ProcessControlBlock (*pcb)[MAX_USER_PROCESSES];
static struct SharedMemory *shm;

int main(int argc, char* argv[]) {
	// Signal Handler
	signal(SIGINT, signalHandler);
	signal(SIGSEGV, signalHandler);
	
	
	char *fileName = "log.out";
	fp = fopen(fileName, "a");
	if(fp == NULL) {
		printf("Couldn't open file");
		errno = ENOENT;
		killAll();
		exit(EXIT_FAILURE);
	}
	
	// Seed the random number generator
	srand((unsigned)(getpid() ^ time(NULL) ^ ((getpid()) >> MAX_USER_PROCESSES)));
	
	// Get shared memory id 
	if((shmid = shmget(key, sizeof(struct SharedMemory *) * 3, 0666)) < 0) {
		perror("shmget");
		fprintf(stderr, "Child: shmget() $ returned an error! Program terminating...\n");
		killAll();
		exit(EXIT_FAILURE);
	}
	
	// Attach the shared memory
    if ((shm = (struct SharedMemory *)shmat(shmid, NULL, 0)) == (struct SharedMemory *) -1) {
		perror("shmat");
        fprintf(stderr, "shmat() returned an error! Program terminating...\n");
		killAll();
        exit(EXIT_FAILURE);
    }
	
	// Get process control block id
	if((pcbid = shmget(pcbKey, sizeof(struct ProcessControlBlock *) * (MAX_USER_PROCESSES * 100), 0666)) < 0) {
		perror("shmget");
		fprintf(stderr, "shmget() $$ returned an error! Program terminating...\n");
		exit(EXIT_FAILURE);
	}
	
	// Attach PCB
	if((pcb = (void *)(struct ProcessControlBlock *)shmat(pcbid, NULL, 0)) == (void *) -1) {
		perror("shmat");
        fprintf(stderr, "shmat() returned an error! Program terminating...\n");
        exit(EXIT_FAILURE); 
    }

	// Attach message queues
	if((msgid_receiving = msgget(toChild_key, 0666)) < 0) {
		fprintf(stderr, "Child %i has failed attaching the sending queue\n", getpid());
		killAll();
		exit(EXIT_FAILURE);
	}
	
	if((msgid_sending = msgget(toParent_key, 0666)) < 0) {
		fprintf(stderr, "Child %i has failed attaching the sending queue\n", getpid());
		killAll();
		exit(EXIT_FAILURE);
	}
	
	if ((msgid_critical = msgget(critical_Key, 0666)) < 0) {
		perror("msgget");
		killAll();
		exit(EXIT_FAILURE);
	}
	
	struct msg_buf msgbuff_receive, msgbuff_send, msgbuff_critical;
	int i = getIndex();
	
	// Wait to be scheduled by processor
	// If process does not complete in time, come back to this point
backToWait:
	while(msgrcv(msgid_receiving, &msgbuff_receive, MSGSZ, getpid(), 0) < 0);
	while(shm->childControl);
	printf("USER: #%i received scheduling message @ %03i.%09lu\n", getpid(), shm->timePassedSec, shm->timePassedNansec);
	
	if(fileLinesWritten < 10000) {
		fprintf(fp, "USER: #%i received scheduling message @ %03i.%09lu\n", getpid(), shm->timePassedSec, shm->timePassedNansec);
		fileLinesWritten++;
	}

	int c, j,
	n = MAX_USER_PROCESSES,
	toSleep,
	statusOne = 1,
	statusZero = 0;
	
	// Peterson's Algorithm for more than two processes 
	do {
		shm->flag[i] = want_in;
		//Set local variable
		j = shm->turn; 
		
		while(j != i) {
			j = (shm->flag[j] != idle) ? shm->turn : (j + 1) % n;
		}
		
		// Declare intention to enter critical section
		shm->flag[i] = in_cs;
		
		// Check that no one else is in critical section
		for(j = 0; j < n; j++) {
			if((j != i) && (shm->flag[j] == in_cs)) {
				break;
			}
		}
	} while((j < n) || ((shm->turn != i) && (shm->flag[shm->turn] != idle)));
	
	// Assign to self and enter critical section
	shm->turn = i; 
	
	// **Critical Section**
	if(fileLinesWritten < 10000) {
		fprintf(fp, "PROCESS: #%i entered critical section @ %03i.%09lu\n", getpid(), shm->timePassedSec, shm->timePassedNansec);
		fileLinesWritten++;
	}
	
	msgbuff_send.mtype = 1;
	sprintf(msgbuff_send.mtext, "%i", getpid());
	
	if(msgsnd(msgid_sending, &msgbuff_send, MSGSZ, IPC_NOWAIT) < 0) {
		printf("ERROR: the msg to oss failed to send\n");
		signalHandler();
	}
	
	// Request OSS to stop
	printf("USER: #%i requesting control @ %03i.%09lu\n", getpid(), shm->timePassedSec, shm->timePassedNansec);
	if(fileLinesWritten < 10000) {
		fprintf(fp, "USER: #%i requesting control @ %03i.%09lu\n", getpid(), shm->timePassedSec, shm->timePassedNansec);
		fileLinesWritten++;
	}
	
	// Begin running when OSS stops
	while(msgrcv(msgid_critical, &msgbuff_critical, MSGSZ, 1, 0) < 0);
	printf("USER: #%i began running @ %03i.%09lu\n", getpid(), shm->timePassedSec, shm->timePassedNansec);
	if(fileLinesWritten < 10000) {
		fprintf(fp, "USER: #%i began running @ %03i.%09lu\n", getpid(), shm->timePassedSec, shm->timePassedNansec);
		fileLinesWritten++;
	}
	
	// If quantum > running time, this process cant finish in this iteration and must be requeued
	unsigned long long int runningTime = rand() % (1000000000 - min) + min;	
	if(pcb[i]->quantum > runningTime) {
		usleep((runningTime / 1000));
		
		pcb[i]->quantum = pcb[i]->quantum - runningTime;
		
		msgbuff_send.mtype = pcb[i]->quantum;
		sprintf(msgbuff_send.mtext, "%i", statusOne);
		
		// **Exit Critical Section**
		printf("USER: #%i was interrupted @ %03i.%09lu\n", getpid(), shm->timePassedSec, shm->timePassedNansec);
		if(fileLinesWritten < 10000) {
			fprintf(fp, "USER: #%i was interrupted @ %03i.%09lu\n", getpid(), shm->timePassedSec, shm->timePassedNansec);
			fileLinesWritten++;
		}
		
		j = (shm->turn + 1) % n;
		while(shm->flag[j] == idle) {
			j = (j + 1) % n;
		}

		//printf("\t\t\t\t\tPROCESS: #%i exited critical section @ %03i.%09lu\n", getpid(), shm->timePassedSec, shm->timePassedNansec);
		
		if(msgsnd(msgid_sending, &msgbuff_send, MSGSZ, IPC_NOWAIT) < 0) {
			perror("msgsnd");
			printf("The reply to child did not send\n");
			signalHandler();
		}
		
		// Give OSS control
		printf("USER: #%i relinquished control @ %03i.%09lu\n", getpid(), shm->timePassedSec, shm->timePassedNansec);
		if(fileLinesWritten < 10000) {
			fprintf(fp, "USER: #%i relinquished control @ %03i.%09lu\n", getpid(), shm->timePassedSec, shm->timePassedNansec);
			fileLinesWritten++;
		}
		
		// Wait for OSS to take over
		while(msgrcv(msgid_critical, &msgbuff_critical, MSGSZ, 1, 0) < 0);
		
		shm->turn = j;
		shm->flag[i] = idle;
		
		// Go back and requeue
		goto backToWait;
		
	} else {
		// If quantum < runningTime, the process can finish
		long int toSleep = pcb[i]->quantum / 1000;
		usleep(toSleep);
		
		msgbuff_send.mtype = 1;
		sprintf(msgbuff_send.mtext, "%i", statusZero);
		
		// **Exit Critical Section**
		printf("USER: #%i completed @ %03i.%09lu\n", getpid(), shm->timePassedSec, shm->timePassedNansec);
		if(fileLinesWritten < 10000) {
			fprintf(fp, "USER: #%i completed @ %03i.%09lu\n", getpid(), shm->timePassedSec, shm->timePassedNansec);
			fileLinesWritten++;
		}
	}
	
	j = (shm->turn + 1) % n;
	while(shm->flag[j] == idle) {
		j = (j + 1) % n;
	}
	
	// **EXITED**
	if(fileLinesWritten < 10000) {
		fprintf(fp, "PROCESS: #%i exited critical section @ %03i.%09lu\n", getpid(), shm->timePassedSec, shm->timePassedNansec);
		fileLinesWritten++;
	}
	
	// Assign turn to next waiting process; change own flag to idle
	shm->turn = j;
	shm->flag[i] = idle;
	
	// Give OSS back control
	printf("USER: #%i quitting and relinquishing control @ %03i.%09lu\n", getpid(), shm->timePassedSec, shm->timePassedNansec);
	if(fileLinesWritten < 10000) {
		fprintf(fp, "USER: #%i quitting and relinquishing control @ %03i.%09lu\n", getpid(), shm->timePassedSec, shm->timePassedNansec);
		fileLinesWritten++;
	}

	usleep(1000);
	
	if(msgsnd(msgid_sending, &msgbuff_send, MSGSZ, IPC_NOWAIT) < 0) {
		printf("ERROR: the msg to oss failed to send\n");
		signalHandler();
	}
	
	// Wait for OSS to take over
	while(msgrcv(msgid_critical, &msgbuff_critical, MSGSZ, 1, 0) < 0);
	
	pcb[i]->finishSec = shm->timePassedSec;
	pcb[i]->alive = 0;
	killAll();
	exit(3);
	
}

/* Completely Terminates the child process  */
void signalHandler() {
    pid_t id = getpid();
	printf("A signal was given, child with pid:%i has been removed.\n", id);
	killAll();
    killpg(id, SIGINT);
    exit(EXIT_SUCCESS);
}

/* Releases all Shared Memory */
void killAll() {
	shmdt(pcb);
	shmdt(shm);
	fclose(fp);
}

int getIndex() {
	int i;
	for(i = 0; i < MAX_USER_PROCESSES; i++) {
		if(pcb[i]->pid == getpid()) {
			return i;
		}
	}
}
