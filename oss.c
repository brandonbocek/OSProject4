#include "scheduler.h"

const static unsigned long long int quantumLength = QUANTUM_LENGTH * 1E9;

static char *added = "Added";
static char *moved = "Moved";
static char *readded = "Re-Added";
static char *zero = "0";

static FILE *fp;


/*  Keeps track of what pcbs are already taken */
static int bitArray[MAX_USER_PROCESSES] = {0};
static int fileLinesWritten = 0;
static int movedLast, totalProcesses, totalSchedules, totalTime, totalTimesLooped, totalWaitTime = 0;
static int movedLast, msgid_critical, msgid_receiving, msgid_sending, pcbid, shmid;

static struct msg_buf msgbuff_critical, msgbuff_receive,msgbuff_send;
static struct ProcessControlBlock (*pcb)[MAX_USER_PROCESSES];
static struct ProcessQueue *queueOne, *queueTwo, *queueThree, *queueCleanUp;
static struct SharedMemory *shm;

static unsigned long long int totalProcessCPUTime = 0;


int main(int argc, char* argv[]) {
	/* Setting up the signal handler */
	signal(SIGINT, signalHandler);
	signal(SIGSEGV, signalHandler);
	
	/* The file for the output log to track progress */
	char *filename = "ossLog.out";
	
	/* Seeding generator for random numbers */
	srand((unsigned)(getpid() ^ time(NULL) ^ ((getpid()) >> MAX_USER_PROCESSES)));
	
	int opt, linesToWrite = 10000;

	/* Handling command line arguments w/ ./oss  */
	while ((opt = getopt (argc, argv, "hl:")) != -1) {
		switch (opt) {
			case 'h':
				printHelpMenu();
				exit(EXIT_SUCCESS);
					
			case 'l':
				/*  Change the name of the file to write to */
				filename = optarg;
				break;
	
			case '?':
				/* User entered a valid flag but nothing after it */
				if(optopt == 'l') {
					fprintf(stderr, "-%c needs to have an argument!\n", optopt);
				} else {
					fprintf(stderr, "%s is an unrecognized flag\n", argv[optind - 1]);
				}
			default:	
				/* User entered an invalid flag */
				printHelpMenu();
		}
	}
	
	/* Shared memory attach for clock and PCBs */
	if((shmid = shmget(key, sizeof(struct SharedMemory *) * 3, IPC_CREAT | 0666)) < 0) {
		fprintf(stderr, "ERROR: shmget() faild.\n");
		exit(EXIT_FAILURE);
	}
	
    if((shm = (struct SharedMemory *)shmat(shmid, NULL, 0)) == (struct SharedMemory *) -1) {
        fprintf(stderr, "ERROR: shmat() failed.\n");
        exit(EXIT_FAILURE); 
    }
	
	if((pcbid = shmget(pcbKey, sizeof(struct ProcessControlBlock *) * (MAX_USER_PROCESSES * 100), IPC_CREAT | 0666)) < 0) {
		fprintf(stderr, "ERROR: shmget() failed.\n");
		exit(EXIT_FAILURE);
	}
	
    if((pcb = (void *)(struct ProcessControlBlock *)shmat(pcbid, NULL, 0)) == (void *) -1) {
        fprintf(stderr, "ERROR: shmat() failed.\n");
        exit(EXIT_FAILURE); 
    }
	
	/*  Set the file pointer to the correct file */
	fp = fopen(filename, "w");
	if(fp == NULL) {
		printf("ERROR: Failed to open file.");
		killAll();
		exit(EXIT_FAILURE);
	}
	
	/*  The Queues are set up */
	shm->turn = 0;
	shm->scheduledCount = 0;
	shm->timePassedSec = 0;
	
	queueOne = malloc(sizeof(*queueOne));
	queueOne->numProcesses = 0;
	
	queueTwo = malloc(sizeof(*queueTwo));
	queueTwo->numProcesses = 0;
	
	queueThree = malloc(sizeof(*queueThree));
	queueThree->numProcesses = 0;
	
	queueCleanUp = malloc(sizeof(*queueCleanUp));
	queueCleanUp->numProcesses = 0;
	
	// General variables
	int c, status, index = 0;
	pid_t pid, temp, wpid;
	
	for(c = 0; c < MAX_USER_PROCESSES; c++) {
		shm->flag[c] = 0;
	}
	
	/*  Variable for message passing */
    int msgflg = IPC_CREAT | 0666;
	
	// Attach message queues
	if ((msgid_receiving = msgget(toParent_key, msgflg)) < 0) {
		perror("msgget");
		killAll();
		exit(EXIT_FAILURE);
	}
	
	if ((msgid_sending = msgget(toChild_key, msgflg)) < 0) {
		perror("msgget");
		killAll();
		exit(EXIT_FAILURE);
	}
	
	if ((msgid_critical = msgget(critical_Key, msgflg)) < 0) {
		perror("msgget");
		killAll();
		exit(EXIT_FAILURE);
	}

	/* Starting the Virtual Clock */
	pid = fork();
	if(pid == 0) {
		execl("./clock", NULL);
	} else if(pid < 0) {
		printf("ERROR: Clock process forked incorrectly.\n");
		signalHandler();
	}
	usleep(5000);
	
	int spawnNewProcess = 0;
	int waitUntil, result;
	
	/* OSS Process Loops until the user gives the signal to end */
	while(1) {
		
		result = -1;
		
		/*  Spawn a new process every 0-2 seconds or 1 second average */
		if(!spawnNewProcess) {
			waitUntil = (rand() % PROCESS_SPAWN_RATE);
			if(waitUntil == 3) {
				waitUntil = 0;
			}
			
			waitUntil += shm->timePassedSec;
			spawnNewProcess = 1;
		}
		
		/* Child process spawns if enough time has passed and there is space available */
		if((index < MAX_USER_PROCESSES) && (spawnNewProcess == 1) && (shm->timePassedSec >= waitUntil)) {
			index++;
			result = createProcess(totalProcesses);
			spawnNewProcess = 0;
			totalProcesses++;
			
			pid = fork();
			if (pid == 0) {
				
				/* ./user's process is spawned */
				execl("./user", NULL);

				/* Exit if the exec failed */
				_exit(EXIT_FAILURE);

			} else if(pid < 0) {
				printf("ERROR: The new process failed to fork correctly.\n");

			/* Save the process to the pcb and update the user and the log with process creation info */
			} else {
				pcb[result]->index = result;
				pcb[result]->pid = pid;
				printf("OSS: Created Process with ID:%i at %03i.%09lu... PROCESS #%i @ INDEX: %i\n", pid, shm->timePassedSec, shm->timePassedNansec, totalProcesses, index);
				fprintf(fp, "OSS: Created Process with ID:%i at %03i.%09lu... PROCESS #%i @ INDEX: %i\n", pid, shm->timePassedSec, shm->timePassedNansec, totalProcesses, index);
			}
		} else {
			pid = -1;
		}
		
		usleep(MASTER_OVERHEAD_TIME * 10000);
		
		/* If a child was spawned, add it to the queue */
		if(result != -1 && pid != -1) {
			addToQueue(pcb[result]->queue, pid, added);
		}
		
		// Run the scheduler
		scheduleProcess(index, result);
		
		// If OSS receives a message from a child requesting control
		if(msgrcv(msgid_receiving, &msgbuff_receive, MSGSZ, 1, IPC_NOWAIT) > 0) {
			printf("OSS: Handing control to #%s process at %03i.%09lu\n", msgbuff_receive.mtext, shm->timePassedSec, shm->timePassedNansec);
			fprintf(fp, "OSS: Handing control to #%s process %03i.%09lu\n", msgbuff_receive.mtext, shm->timePassedSec, shm->timePassedNansec);
			
			shm->childControl = 1;
			temp = atoi(msgbuff_receive.mtext);
			
			usleep(MASTER_OVERHEAD_TIME * 10000);
			
			msgbuff_critical.mtype = 1;
			sprintf(msgbuff_critical.mtext, "%i", temp);
			
			// Message child saying it has control
			if(msgsnd(msgid_critical, &msgbuff_critical, MSGSZ, IPC_NOWAIT) < 0) {
				printf("ERROR: The msg to child process did not send\n");
				signalHandler();
			}
			
			
			// Wait for child to relinquish control
			while(msgrcv(msgid_receiving, &msgbuff_receive, MSGSZ, 0, 0) < 0);
			
			printf("OSS: Taking control back from process #%i @ %03i.%09lu\n", temp, shm->timePassedSec, shm->timePassedNansec);
			fprintf(fp, "OSS: Taking control back from process #%i @ %03i.%09lu\n", temp, shm->timePassedSec, shm->timePassedNansec);
			
			usleep(MASTER_OVERHEAD_TIME * 10000);
			shm->childControl = 0;
			
			msgbuff_critical.mtype = 1;
			sprintf(msgbuff_critical.mtext, "%i", temp);
			
			if(msgsnd(msgid_critical, &msgbuff_critical, MSGSZ, IPC_NOWAIT) < 0) {
				printf("ERROR: The msg to child process did not send\n");
				signalHandler();
			}
			
			
			/* Requeue the child if it failed to finish */
			if(atoi(msgbuff_receive.mtext) == 1) {
				if(msgbuff_receive.mtype <= 5E8) {
					addToQueue(1, temp, readded);
				} else if(msgbuff_receive.mtype <= 1E9) {
					addToQueue(2, temp, readded);
				} else {
					addToQueue(3, temp, readded);
				}
			}
		}

		usleep(MASTER_OVERHEAD_TIME * 10000);
		
		/* Make room in the cleanup queue  */
		if(queueCleanUp->numProcesses > 5) {
			queueCleanUp->pid[0] = 0;
			queueCleanUp->numProcesses--;
			advanceQueues();
		}
		
		
		/* Clear the PCB if a child terminated and update the user and the log */
		if ((wpid = waitpid(-1, &status, WNOHANG)) > 0 ) {
			index--;
			for(c = 0; c < MAX_USER_PROCESSES; c++) {
				if(pcb[c]->pid == wpid) {
					printf("OSS: Terminating process #%i @ %03i.%09lu\n", wpid, shm->timePassedSec, shm->timePassedNansec);
					fprintf(fp, "OSS: Terminating process #%i @ %03i.%09lu\n", wpid, shm->timePassedSec, shm->timePassedNansec);
					
					bitArray[c] = 0;
					shm->scheduledCount--;
					getProcessStats(c);
					addToQueue(4, wpid, added);
					advanceQueues();
					break;
				}
			}
		}
		usleep(MASTER_OVERHEAD_TIME * 40000);
		totalTimesLooped++;
	}	
		/* END MAIN LOOP */
	
	for(c = 0; c < queueCleanUp->numProcesses; c++) {
		queueCleanUp->pid[c] = 0;
	}
	
	killAll();
	printf("OSS: Exiting normally -- forcing children to exit\n");
	printStats();
	killpg(getpgrp(), SIGINT);
	
	sleep(1);
	return 0;
}

/* END MAIN */

void addToQueue(int queue, pid_t pid, char *word) {
	int c;
	
	int location;

	/* Find the correct location to add to */
	for(location = 0; location < MAX_USER_PROCESSES; location++) {
		if(pcb[location]->pid == queueOne->pid[0]) {
			break;
		}
	}
	
	/* Saving the time in the queue */
	if(pcb[location]->alive == 0) {
		pcb[location]->alive = 1;
		pcb[location]->inQueueSec = shm->timePassedSec;
		pcb[location]->inQueueNansec = shm->timePassedNansec;
	}
	
	/* Enqueue the process in the correct queue */
	switch(queue) {
		case 1:
			for(c = 0; c < MAX_USER_PROCESSES; c++) {
				if(queueOne->pid[c] == 0) {
					queueOne->pid[c] = pid;
					queueOne->index[c] = location;
					usleep(10);
					printf("OSS: %s #%i to Queue #%i @ %03i.%09lu\n", word, pid, queue, shm->timePassedSec, shm->timePassedNansec);
					fprintf(fp, "OSS: %s #%i to Queue #%i @ %03i.%09lu\n", word, pid, queue, shm->timePassedSec, shm->timePassedNansec);
					queueOne->numProcesses++;
					break;
				}
			}
			
			break;
			
		case 2:
			for(c = 0; c < MAX_USER_PROCESSES; c++) {
				if(queueTwo->pid[c] == 0) {
					queueTwo->pid[c] = pid;
					queueTwo->index[c] = location;
					usleep(10);
					printf("OSS: %s #%i to Queue #%i @ %03i.%09lu\n", word, pid, queue, shm->timePassedSec, shm->timePassedNansec);
					fprintf(fp, "OSS: %s #%i to Queue #%i @ %03i.%09lu\n", word, pid, queue, shm->timePassedSec, shm->timePassedNansec);
					queueTwo->numProcesses++;
					break;
				}
			}
			
			break;
			
		case 3: 
			for(c = 0; c < MAX_USER_PROCESSES; c++) {
				if(queueThree->pid[c] == 0) {
					queueThree->pid[c] = pid;
					queueThree->index[c] = location;
					usleep(10);
					printf("OSS: %s #%i to Queue #%i @ %03i.%09lu\n", word, pid, queue, shm->timePassedSec, shm->timePassedNansec);
					fprintf(fp, "OSS: %s #%i to Queue #%i @ %03i.%09lu\n", word, pid, queue, shm->timePassedSec, shm->timePassedNansec);
					queueThree->numProcesses++;
					break;
				}
			}
			
			break;
			
		case 4: 
			for(c = 0; c < MAX_USER_PROCESSES; c++) {
				if(queueCleanUp->pid[c] == 0) {
					queueCleanUp->pid[c] = pid;
					queueCleanUp->numProcesses++;
					break;
				}
			}
			
			break;	
	} 
}

void advanceQueues() {
	int c;
	int x;
	
	for(c = 0; c < MAX_USER_PROCESSES; c++) {
		if(queueOne->pid[c] == 0) {
			x = c + 1;
			if(x < MAX_USER_PROCESSES) {
				if(queueOne->pid[x] > 0) {
					queueOne->pid[c] = queueOne->pid[x];
					queueOne->pid[x] = 0;
					queueOne->index[c] = queueOne->index[x];
					queueOne->index[x] = 0;
				}
			}
		}
	}
	
	for(c = 0; c < MAX_USER_PROCESSES; c++) {
		if(queueTwo->pid[c] == 0) {
			x = c + 1;
			if(x < MAX_USER_PROCESSES) {
				if(queueTwo->pid[x] > 0) {
					queueTwo->pid[c] = queueTwo->pid[x];
					queueTwo->pid[x] = 0;
					queueTwo->index[c] = queueTwo->index[x];
					queueTwo->index[x] = 0;
				}
			}
		}
	}
	
	for(c = 0; c < MAX_USER_PROCESSES; c++) {
		if(queueThree->pid[c] == 0) {
			x = c + 1;
			if(x < MAX_USER_PROCESSES) {
				if(queueThree->pid[x] > 0) {
					queueThree->pid[c] = queueThree->pid[x];
					queueThree->pid[x] = 0;
					queueThree->index[c] = queueThree->index[x];
					queueThree->index[x] = 0;
				}
			}
		}
	}
	
	for(c = 0; c < MAX_USER_PROCESSES; c++) {
		if(queueCleanUp->pid[c] == 0) {
			x = c + 1;
			if(x < MAX_USER_PROCESSES) {
				if(queueCleanUp->pid[x] > 0) {
					queueCleanUp->pid[c] = queueCleanUp->pid[x];
					queueCleanUp->pid[x] = 0;
					queueCleanUp->index[c] = queueCleanUp->index[x];
					queueCleanUp->index[x] = 0;
				}
			}
		}
	}
}

int createProcess(int processNumber) {
	int indx;

	/*  Find the first open index in the bit vector */
	for(indx = 0; indx < MAX_USER_PROCESSES; indx++) {
		if(bitArray[indx] == 0) {
			bitArray[indx] = 1;
			break;
		}
	}
	
	/*  Don't create a process if there isn't any room for it */
	if(indx == MAX_USER_PROCESSES) {
		return -1;
	}
	
	pcb[indx]->index = indx;
	pcb[indx]->pid = 0;
	pcb[indx]->processNumber = processNumber;
	
	/*  Generate random event 0-3 */
	pcb[indx]->randToDo = rand() % 3 + 0;
	
	pcb[indx]->cpuTime = pcb[indx]->quantum = quantumLength;
	pcb[indx]->queue = 0;
	pcb[indx]->creationSec = shm->timePassedSec;
	pcb[indx]->creationNansec = shm->timePassedNansec;
	pcb[indx]->finishSec = 0;
	pcb[indx]->inQueueSec = 0;
	pcb[indx]->inQueueNansec = 0;
	pcb[indx]->moveFlag = 0;

	/*  A new process always starts in the first queue */
	pcb[indx]->queue = 1;	
	
	return indx;
}

/*  Called when a child process has finished */
void getProcessStats(int indx) {

	/*  Variables for printing terminated process's final info */
	unsigned long long int temp = shm->timePassedNansec - pcb[indx]->creationNansec;
	if(temp < 0) {
		temp = (shm->timePassedNansec + pcb[indx]->creationNansec) - 1E9;
	}
	
	totalTime += (pcb[indx]->finishSec - pcb[indx]->creationSec);
	totalProcessCPUTime += pcb[indx]->cpuTime;

	/*  Update the user and the log */
	fprintf(fp, "**********************************************\n");
	fprintf(fp, "\tProcess with PID:%i - Number of CPU Seconds used:%f seconds\n", pcb[indx]->pid, (double)(pcb[indx]->cpuTime/1E9));
	fprintf(fp, "\tThis process was in the system for a total of:%i.%.5u seconds\n", (pcb[indx]->finishSec - pcb[indx]->creationSec), temp/1E9);
	fprintf(fp, "**********************************************\n");

	printf("**********************************************\n");
	printf("\tProcess with PID:%i - Number of CPU Seconds used:%f seconds\n", pcb[indx]->pid, (double)(pcb[indx]->cpuTime/1E9));
	printf("\tThis process was in the system for a total of:%i.%.5u seconds\n", (pcb[indx]->finishSec - pcb[indx]->creationSec), temp/1E9);
	printf("**********************************************\n");	
	
	/*  Reset the PCB */	
	pcb[indx]->alive = 0;
	pcb[indx]->index = 0;
	pcb[indx]->pid = 0;
	pcb[indx]->processNumber = 0;
	pcb[indx]->cpuTime = pcb[indx]->quantum = 0;
	pcb[indx]->queue = 0;
	pcb[indx]->creationSec = 0;
	pcb[indx]->creationNansec = 0;
	pcb[indx]->finishSec = 0;
	pcb[indx]->inQueueSec = 0;
	pcb[indx]->inQueueNansec = 0;
	pcb[indx]->moveFlag = 0;
}

/*  Cleanup */
void killAll() {
	msgctl(msgid_sending, IPC_RMID, NULL);
	msgctl(msgid_receiving, IPC_RMID, NULL);
	msgctl(msgid_critical, IPC_RMID, NULL);
	
	shmdt(shm);
	shmctl(shmid, IPC_RMID, NULL);
	
	shmdt(pcb);
	shmctl(pcbid, IPC_RMID, NULL);
	
	free(queueOne);
	free(queueTwo);
	free(queueThree);
	free(queueCleanUp);
	
	fclose(fp);
}

void printHelpMenu() {
	printf("\n\t\t~~Help Menu~~\n\t-h This Help Menu Printed\n");
	//printf("\t-s *# of slave processes to spawn*\t\tie. '-s 5'\n");
	printf("\t-l *log file used*\t\t\t\tie. '-l log.out'\n");
	//printf("\t-t *time in seconds the master will terminate*\tie. -t 20\n\n");
}


/*  Called when the entire program ends */
void printStats() {
	sleep(1);
	printf("*******************************************************************\n");
	printf("\tTotal Program Run Time: %i.%09lu seconds\n", shm->timePassedSec, shm->timePassedNansec);
	printf("\tTotal CPU Down Time: %f seconds\n", 
		((double)shm->timePassedSec - ((((double)(MASTER_OVERHEAD_TIME + 2) * 1e-2) * totalTimesLooped) + (double)(totalProcessCPUTime / 1E9))));
	printf("\tAverage time waiting in Queue: %f seconds\n", ((double)totalWaitTime / (double)totalSchedules));
	printf("\tAverage CPU Usage by Processes: %f seconds\n", ((double)(totalProcessCPUTime / 1E9) / (double)totalProcesses));
	printf("\tAverage Turnaround Time: %03f seconds\n", ((double)totalTime / (double)totalProcesses));	
	printf("*******************************************************************\n");
}

int removeFromQueue(int queue, pid_t pid) {
	int c, tmp;
	
	switch(queue) {
		case 1:
			for(c = 0; c < MAX_USER_PROCESSES; c++) {
				if(queueOne->pid[c] == pid) {
					tmp = queueOne->index[c];
					unsigned long long int temp = shm->timePassedNansec - pcb[tmp]->inQueueNansec;
					if(temp < 0) {
						temp = (shm->timePassedNansec + pcb[tmp]->inQueueNansec) - 1E9;
					}
					
					totalWaitTime += (shm->timePassedSec - pcb[tmp]->inQueueSec);
					totalSchedules++;
					
					printf("OSS: Process #%i was in queue for %03i.%09lu seconds\n", queueOne->pid[c], (shm->timePassedSec - pcb[tmp]->inQueueSec), temp);
					fprintf(fp, "OSS: Process #%i was in queue for %03i.%09lu seconds\n", queueOne->pid[c], (shm->timePassedSec - pcb[tmp]->inQueueSec), temp);
					
					pcb[tmp]->inQueueSec = pcb[tmp]->inQueueNansec = shm->timePassedSec;
					pcb[tmp]->moveFlag = 0;
					pcb[tmp]->queue = 0;
					queueOne->pid[c] = 0;
					queueOne->index[c] = 0;
					queueOne->numProcesses--;
					advanceQueues();
					return tmp;
				}
			}
			break;
		
		case 2:
			for(c = 0; c < MAX_USER_PROCESSES; c++) {
				if(queueTwo->pid[c] == pid) {
					tmp = queueTwo->index[c];
					
					queueTwo->pid[c] = 0;
					queueTwo->index[c] = 0;
					queueTwo->numProcesses--;
					advanceQueues();
					return tmp;
				}
			}
			break;
		
		case 3:
			for(c = 0; c < MAX_USER_PROCESSES; c++) {
				if(queueThree->pid[c] == pid) {
					tmp = queueThree->index[c];
					
					queueThree->pid[c] = 0;
					queueThree->index[c] = 0;
					queueThree->numProcesses--;
					advanceQueues();
					return tmp;
				}
			}
			break;
			
		default:
			printf("Something went wrong\n");
			advanceQueues();
			return -1;
	}
}


void scheduleProcess() {
	int i;
	usleep(MASTER_OVERHEAD_TIME * 10000);
	
	/* Loop through all the processes in this queue */
	for(i = 0; i < queueThree->numProcesses; i++) {


		/*  If the time in queue is greater than 2 seconds, the process is alive and did not go last */
		if((shm->timePassedSec - pcb[queueThree->index[i]]->inQueueSec) > BETA*(((double)totalWaitTime / (double)totalSchedules)) && (pcb[queueThree->index[i]]->moveFlag == 0) && (pcb[queueThree->index[i]]->alive == 1)) {
			pcb[queueThree->index[i]]->queue = 2;
			pcb[queueThree->index[i]]->moveFlag = 1;
			usleep(MASTER_OVERHEAD_TIME * 10000);

		/*  Move starving process from queue 3 to queue 2 */	
			addToQueue(2, queueThree->pid[i], "Shifted 3 to 2");
			removeFromQueue(3, queueThree->pid[i]);
			return;
		}
	}
	
	for(i = 0; i < queueTwo->numProcesses; i++) {
		if((shm->timePassedSec - pcb[queueTwo->index[i]]->inQueueSec) > ALPHA*(((double)totalWaitTime / (double)totalSchedules)) && (pcb[queueTwo->index[i]]->moveFlag == 0) && (pcb[queueTwo->index[i]]->alive == 1)) {
			pcb[queueTwo->index[i]]->queue = 1;
			pcb[queueTwo->index[i]]->moveFlag = 1;
			
			usleep(MASTER_OVERHEAD_TIME * 10000);
		/*  Move starving process from queue 2 to queue 1 */
			addToQueue(1, queueTwo->pid[i], "Shifted 2 to 1");
			removeFromQueue(2, queueTwo->pid[i]);
			return;
		}
	}
	

	/* Schedules from the highest priority queue with a process */
	if(queueOne->pid[0] > 0) {	
		shm->scheduledCount++;
		msgbuff_send.mtype = queueOne->pid[0];
		sprintf(msgbuff_send.mtext, "%i", shm->scheduledCount);
		printf("OSS: Scheduled process with pid:%i at %03i.%05lu\n", msgbuff_send.mtype, shm->timePassedSec, shm->timePassedNansec);
		fprintf(fp, "OSS: Scheduled process with pid:%i at %03i.%05lu\n", msgbuff_send.mtype, shm->timePassedSec, shm->timePassedNansec);
		
		if(msgsnd(msgid_sending, &msgbuff_send, MSGSZ, IPC_NOWAIT) < 0) {
			printf("The reply to child did not send\n");
			signalHandler();
		}
		
		removeFromQueue(1, queueOne->pid[0]);
		pcb[queueOne->index[0]]->queue = 0;
		usleep(MASTER_OVERHEAD_TIME * 10000);
		return;

	} else if(queueTwo->pid[0] > 0) {	
		shm->scheduledCount++;
		msgbuff_send.mtype = queueTwo->pid[0];
		sprintf(msgbuff_send.mtext, "%i", shm->scheduledCount);
		printf("OSS: Scheduled process with pid:%i at %03i.%05lu\n", msgbuff_send.mtype, shm->timePassedSec, shm->timePassedNansec);
		fprintf(fp, "OSS: Scheduled process with pid:%i at %03i.%05lu\n", msgbuff_send.mtype, shm->timePassedSec, shm->timePassedNansec);
		
		if(msgsnd(msgid_sending, &msgbuff_send, MSGSZ, IPC_NOWAIT) < 0) {
			printf("The reply to child did not send\n");
			signalHandler();
		}
		pcb[queueOne->index[0]]->queue = 1;
		removeFromQueue(2, queueTwo->pid[0]);
		usleep(MASTER_OVERHEAD_TIME * 10000);
		return;
		
	} else if(queueThree->pid[0] > 0) {
		shm->scheduledCount++;
		msgbuff_send.mtype = queueTwo->pid[0];
		sprintf(msgbuff_send.mtext, "%i", shm->scheduledCount);
		printf("OSS: Scheduled process with pid:%i at %03i.%05lu\n", msgbuff_send.mtype, shm->timePassedSec, shm->timePassedNansec);
		fprintf(fp, "OSS: Scheduled process with pid:%i at %03i.%05lu\n", msgbuff_send.mtype, shm->timePassedSec, shm->timePassedNansec);
		
		if(msgsnd(msgid_sending, &msgbuff_send, MSGSZ, IPC_NOWAIT) < 0) {
			printf("The reply to child did not send\n");
			signalHandler();
		}

		pcb[queueOne->index[0]]->queue = 2;
		removeFromQueue(3, queueThree->pid[0]);
		usleep(MASTER_OVERHEAD_TIME * 10000);
		return;
	}

}

/* Completely Terminates the program and prints final stats */
void signalHandler() {
	printStats();	
	printf("A signal was given. Everything should clear.\n");
	killAll();
    pid_t id = getpgrp();
    killpg(id, SIGINT);
    exit(EXIT_SUCCESS);
}

