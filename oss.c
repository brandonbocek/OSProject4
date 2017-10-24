
#include "scheduler.h"

const static unsigned long long int max = MAX_QUANTUM_LENGTH * 1E9;

static char *added = "Added";
static char *moved = "Moved";
static char *readded = "Re-Added";
static char *zero = "0";

static FILE *fp;

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
	// Signal Handler
	signal(SIGINT, signalHandler);
	signal(SIGSEGV, signalHandler);
	
	// Seed the random number generator
	srand((unsigned)(getpid() ^ time(NULL) ^ ((getpid()) >> MAX_USER_PROCESSES)));
	
	// Logfile name and execl binaries path
	const char *PATH = "./user";
	char *fileName = "logOSS.out";
	
	// Attach shared memory (clock) and Process Control Blocks
	if((shmid = shmget(key, sizeof(struct SharedMemory *) * 3, IPC_CREAT | 0666)) < 0) {
		perror("shmget");
		fprintf(stderr, "shmget() returned an error! Program terminating...\n");
		exit(EXIT_FAILURE);
	}
	
    if((shm = (struct SharedMemory *)shmat(shmid, NULL, 0)) == (struct SharedMemory *) -1) {
		perror("shmat");
        fprintf(stderr, "shmat() returned an error! Program terminating...\n");
        exit(EXIT_FAILURE); 
    }
	
	if((pcbid = shmget(pcbKey, sizeof(struct ProcessControlBlock *) * (MAX_USER_PROCESSES * 100), IPC_CREAT | 0666)) < 0) {
		perror("shmget");
		fprintf(stderr, "shmget() returned an error! Program terminating...\n");
		exit(EXIT_FAILURE);
	}
	
    if((pcb = (void *)(struct ProcessControlBlock *)shmat(pcbid, NULL, 0)) == (void *) -1) {
		perror("shmat");
        fprintf(stderr, "shmat() 1 returned an error! Program terminating...\n");
        exit(EXIT_FAILURE); 
    }
	
	fp = fopen(fileName, "w");
	if(fp == NULL) {
		printf("Couldn't open file");
		errno = ENOENT;
		killAll();
		exit(EXIT_FAILURE);
	}
	
	// Initialize the queues and set everything to 0
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
	int c, status,
		index = 0;
	pid_t pid, temp, wpid;
	
	for(c = 0; c < MAX_USER_PROCESSES; c++) {
		shm->flag[c] = 0;
	}
	
	// Message passing variables
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

	// Start Clock
	pid = fork();
	if(pid == 0) {
		execl("./clock", NULL);
	} else if(pid < 0) {
		printf("There was an error creating the clock\n");
		signalHandler();
	}
	usleep(5000);
	
	int spawnNewProcess = 0;
	int waitUntil, result;
	
	// Begin OSS simulation and stop if it writes 10,000 lines to the log
	while(fileLinesWritten < 10000) {
		
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
		
		// Spawn child if allowed
		if((index < MAX_USER_PROCESSES) && (spawnNewProcess == 1) && (shm->timePassedSec >= waitUntil)) {
			index++;
			result = createProcess(totalProcesses);
			spawnNewProcess = 0;
			totalProcesses++;
			
			pid = fork();
			if (pid == 0) {
				
				// Spawn slave process
				execl(PATH, NULL);

				//If child program exec fails, _exit()
				_exit(EXIT_FAILURE);
			} else if(pid < 0) {
				printf("Error forking\n");
			} else {
				pcb[result]->pid = pid;
				pcb[result]->index = result;
				printf("OSS: Created #%i @ %03i.%09lu - PROCESS #%i - INDEX: %i\n", pid, shm->timePassedSec, shm->timePassedNansec, totalProcesses, index);
				if(fileLinesWritten < 10000) {
					fprintf(fp, "OSS: Created #%i @ %03i.%09lu - PROCESS #%i - INDEX: %i\n", pid, shm->timePassedSec, shm->timePassedNansec, totalProcesses, index);
					fileLinesWritten++;
				}
			}
		} else {
			pid = -1;
		}
		
		usleep(MASTER_OVERHEAD_TIME * 10000);
		
		// If a child was spawned, add it to the queue
		if(result != -1 && pid != -1) {
			addToQueue(pcb[result]->queue, pid, added);
		}
		
		// Run the scheduler
		scheduleProcess(index, result);
		//updateTime();
		
		// If OSS receives a message from a child requesting control
		if(msgrcv(msgid_receiving, &msgbuff_receive, MSGSZ, 1, IPC_NOWAIT) > 0) {
			printf("\t\t\t\tOSS: Giving control to process #%s @ %03i.%09lu\n", msgbuff_receive.mtext, shm->timePassedSec, shm->timePassedNansec);
			if(fileLinesWritten < 10000) {
				fprintf(fp, "OSS: Giving control to process #%s @ %03i.%09lu\n", msgbuff_receive.mtext, shm->timePassedSec, shm->timePassedNansec);
				fileLinesWritten++;
			}
			
			shm->childControl = 1;
			temp = atoi(msgbuff_receive.mtext);
			
			usleep(MASTER_OVERHEAD_TIME * 10000);
			
			msgbuff_critical.mtype = 1;
			sprintf(msgbuff_critical.mtext, "%i", temp);
			
			// Message child saying it has control
			if(msgsnd(msgid_critical, &msgbuff_critical, MSGSZ, IPC_NOWAIT) < 0) {
				perror("msgsnd");
				printf("The reply to child did not send\n");
				signalHandler();
			}
			
			
			// Wait for child to relinquish control
			while(msgrcv(msgid_receiving, &msgbuff_receive, MSGSZ, 0, 0) < 0);
			
			printf("\t\t\t\tOSS: Taking control back from process #%i @ %03i.%09lu\n", temp, shm->timePassedSec, shm->timePassedNansec);
			if(fileLinesWritten < 10000) {
				fprintf(fp, "OSS: Taking control back from process #%i @ %03i.%09lu\n", temp, shm->timePassedSec, shm->timePassedNansec);
				fileLinesWritten++;
			}
			
			usleep(MASTER_OVERHEAD_TIME * 10000);
			shm->childControl = 0;
			
			msgbuff_critical.mtype = 1;
			sprintf(msgbuff_critical.mtext, "%i", temp);
			
			if(msgsnd(msgid_critical, &msgbuff_critical, MSGSZ, IPC_NOWAIT) < 0) {
				perror("msgsnd");
				printf("The reply to child did not send\n");
				signalHandler();
			}
			
			
			// If the child did not finish, requeue
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
		
		// Clean up queue
		if(queueCleanUp->numProcesses > 5) {
			queueCleanUp->pid[0] = 0;
			queueCleanUp->numProcesses--;
			advanceQueues();
		}
		
		
		// If a child finished, clear the PCB and reset for a new child to spawn
		if ((wpid = waitpid(-1, &status, WNOHANG)) > 0 ) {
			index--;
			for(c = 0; c < MAX_USER_PROCESSES; c++) {
				if(pcb[c]->pid == wpid) {
					printf("OSS: Terminating process #%i @ %03i.%09lu\n", wpid, shm->timePassedSec, shm->timePassedNansec);
					if(fileLinesWritten < 10000) {
						fprintf(fp, "OSS: Terminating process #%i @ %03i.%09lu\n", wpid, shm->timePassedSec, shm->timePassedNansec);
						fileLinesWritten++;
					}
					
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
	
	printf("*****************File size limit reached... waiting for all processes to terminate @ %03i.%09lu\n************************", shm->timePassedSec, shm->timePassedNansec);
	
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

void addToQueue(int queue, pid_t pid, char *word) {
	int c;
	
	int location;
	for(location = 0; location < MAX_USER_PROCESSES; location++) {
		if(pcb[location]->pid == queueOne->pid[0]) {
			break;
		}
	}
	
	if(pcb[location]->alive == 0) {
		pcb[location]->alive = 1;
		pcb[location]->inQueueSec = shm->timePassedSec;
		pcb[location]->inQueueNansec = shm->timePassedNansec;
	}
	
	switch(queue) {
		case 1:
			for(c = 0; c < MAX_USER_PROCESSES; c++) {
				if(queueOne->pid[c] == 0) {
					queueOne->pid[c] = pid;
					queueOne->index[c] = location;
					usleep(10);
					printf("\tOSS: %s #%i to Queue #%i @ %03i.%09lu\n", word, pid, queue, shm->timePassedSec, shm->timePassedNansec);
					if(fileLinesWritten < 10000) {
						fprintf(fp, "OSS: %s #%i to Queue #%i @ %03i.%09lu\n", word, pid, queue, shm->timePassedSec, shm->timePassedNansec);
						fileLinesWritten++;
					}
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
					printf("\tOSS: %s #%i to Queue #%i @ %03i.%09lu\n", word, pid, queue, shm->timePassedSec, shm->timePassedNansec);
					if(fileLinesWritten < 10000) {
						fprintf(fp, "OSS: %s #%i to Queue #%i @ %03i.%09lu\n", word, pid, queue, shm->timePassedSec, shm->timePassedNansec);
						fileLinesWritten++;
					}
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
					printf("\tOSS: %s #%i to Queue #%i @ %03i.%09lu\n", word, pid, queue, shm->timePassedSec, shm->timePassedNansec);
					if(fileLinesWritten < 10000) {
						fprintf(fp, "OSS: %s #%i to Queue #%i @ %03i.%09lu\n", word, pid, queue, shm->timePassedSec, shm->timePassedNansec);
						fileLinesWritten++;
					}
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
		
		default:
			printf("Error: Could not add child #%i to a queue!\n", pid);
			fprintf(stderr, "Error adding to queue\n");
			fprintf(fp, "Error: Could not add child #%i to a queue!\n", pid);
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
	
	for(indx = 0; indx < MAX_USER_PROCESSES; indx++) {
		if(bitArray[indx] == 0) {
			bitArray[indx] = 1;
			break;
		}
	}
	
	if(indx == MAX_USER_PROCESSES) {
		return -1;
	}
	
	pcb[indx]->index = indx;
	pcb[indx]->pid = 0;
	pcb[indx]->processNumber = processNumber;
	pcb[indx]->cpuTime = pcb[indx]->quantum = rand() % max + 1;
	pcb[indx]->queue = 0;
	pcb[indx]->creationSec = shm->timePassedSec;
	pcb[indx]->creationNansec = shm->timePassedNansec;
	pcb[indx]->finishSec = 0;
	pcb[indx]->inQueueSec = 0;
	pcb[indx]->inQueueNansec = 0;
	pcb[indx]->moveFlag = 0;

	
	if(pcb[indx]->quantum <= 500000000) {
		pcb[indx]->queue = 1;
	} else if(pcb[indx]->quantum <= 1000000000) {
		pcb[indx]->queue = 2;
	} else {
		pcb[indx]->queue = 3;
	}
	
	int num = rand() % 101;
	if(CHANCE_HIGH_PRIORITY > num) {
		pcb[indx]->queue = 1;
	}
	
	return indx;
}

void getProcessStats(int indx) {
	unsigned long long int temp = shm->timePassedNansec - pcb[indx]->creationNansec;
	if(temp < 0) {
		temp = (shm->timePassedNansec + pcb[indx]->creationNansec) - 1E9;
	}
	
	totalTime += (pcb[indx]->finishSec - pcb[indx]->creationSec);
	totalProcessCPUTime += pcb[indx]->cpuTime;
	printf("-----------------------------------------------------------------------------------\n");
	printf("----------Process %i was in the system for %i.%.9u seconds-----------------\n", pcb[indx]->pid, (pcb[indx]->finishSec - pcb[indx]->creationSec), temp/1E9);
	printf("--------------Process %i used %f seconds of CPU time----------------------\n", pcb[indx]->pid, (double)(pcb[indx]->cpuTime/1E9));
	printf("-----------------------------------------------------------------------------------\n");
	
	if(fileLinesWritten < 9994) {					
		fprintf(fp, "-----------------------------------------------------------------------------------\n");
		fprintf(fp,"----------Process %i was in the system for %i.%.9u seconds-----------------\n", pcb[indx]->pid, (pcb[indx]->finishSec - pcb[indx]->creationSec), temp/1E9);
		fprintf(fp, "--------------Process %i used %f seconds of CPU time---------------------\n", pcb[indx]->pid, (double)(pcb[indx]->cpuTime/1E9));
		fprintf(fp, "-----------------------------------------------------------------------------------\n");
		fileLinesWritten += 4;
	}
		
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

void printHelp() {
	printf("\nCS4760 Project 4 Help!\n");
	printf("-h flag prints this help message\n");
	printf("-s [int x] will spawn x slave processes\n");
	printf("-l [string fileName] will change the output filename to something of your choosing\n");
	printf("-t [int x] will change how long the program can execute before being forcefully terminated\n\n");
}

void printQueues() {
	int c;
	for(c = 0; c < MAX_USER_PROCESSES; c++) {
		printf("QueueOne SLOT %i -- %i\n", c, queueOne->pid[c]);
	}
	
	for(c = 0; c < MAX_USER_PROCESSES; c++) {
		printf("QueueTwo SLOT %i -- %i\n", c, queueTwo->pid[c]);
	}
	
	for(c = 0; c < MAX_USER_PROCESSES; c++) {
		printf("QueueThree SLOT %i -- %i\n", c, queueThree->pid[c]);
	}
	
	for(c = 0; c < MAX_USER_PROCESSES; c++) {
		printf("QueueCleanUp SLOT %i -- %i\n", c, queueCleanUp->pid[c]);
	}
	printf("----------------------\n");
}

void printStats() {
	sleep(1);
	printf("-----------------------------------------------------------------------------------\n");
	printf("\t\tAverage Turnaround Time: %f seconds\n", ((double)totalTime / (double)totalProcesses));
	printf("\t\tAverage Wait Time in Queue: %f seconds\n", ((double)totalWaitTime / (double)totalSchedules));
	printf("\t\tAverage CPU Usage by Processes: %f seconds\n", ((double)(totalProcessCPUTime / 1E9) / (double)totalProcesses));
	printf("\t\tTotal Program Run Time: %i.%09lu seconds\n", shm->timePassedSec, shm->timePassedNansec);
	printf("\t\tTotal CPU Down Time: %f seconds\n", ((double)shm->timePassedSec - ((((double)(MASTER_OVERHEAD_TIME + 2) * 1e-2) * totalTimesLooped) + (double)(totalProcessCPUTime / 1E9))));
	printf("\t\tTotal Lines Printed: %i\n", fileLinesWritten);
	printf("-----------------------------------------------------------------------------------\n");

	sleep(4);
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
					printf("\t\tOSS: Process #%i was in queue for %03i.%09lu seconds\n", queueOne->pid[c], (shm->timePassedSec - pcb[tmp]->inQueueSec), temp);
					if(fileLinesWritten < 10000) {
						fprintf(fp, "OSS: Process #%i was in queue for %03i.%09lu seconds\n", queueOne->pid[c], (shm->timePassedSec - pcb[tmp]->inQueueSec), temp);
						fileLinesWritten++;
					}
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
			printf("You're not supposed to be here...\n");
			advanceQueues();
			return -1;
	}
}

void scheduleProcess() {
	int c;
	usleep(MASTER_OVERHEAD_TIME * 10000);
	for(c = 0; c < queueThree->numProcesses; c++) {
		if(((shm->timePassedSec - pcb[queueThree->index[c]]->inQueueSec) > 2) && (pcb[queueThree->index[c]]->moveFlag == 0) && (pcb[queueThree->index[c]]->alive == 1) && (movedLast = 0)) {
			pcb[queueThree->index[c]]->queue = 2;
			pcb[queueThree->index[c]]->moveFlag = 1;
			movedLast++;
			
			usleep(MASTER_OVERHEAD_TIME * 10000);
			
			addToQueue(2, queueThree->pid[c], "Moved Starving");
			removeFromQueue(3, queueThree->pid[c]);
			return;
		}
	}
	
	for(c = 0; c < queueTwo->numProcesses; c++) {
		if(((shm->timePassedSec - pcb[queueTwo->index[c]]->inQueueSec) > 2) && (pcb[queueTwo->index[c]]->moveFlag == 0) && (pcb[queueTwo->index[c]]->alive == 1) && (movedLast = 0)) {
			pcb[queueTwo->index[c]]->queue = 2;
			pcb[queueTwo->index[c]]->moveFlag = 1;
			movedLast++;
			
			usleep(MASTER_OVERHEAD_TIME * 10000);
			
			addToQueue(2, queueTwo->pid[c], "Moved Starving");
			removeFromQueue(3, queueTwo->pid[c]);
			return;
		}
	}
	
	if(movedLast == 2) {
		movedLast = 0;
	} else {
		movedLast++;
	}
	
	if(queueOne->pid[0] > 0) {	
		shm->scheduledCount++;
		
		msgbuff_send.mtype = queueOne->pid[0];
		sprintf(msgbuff_send.mtext, "%i", shm->scheduledCount);
		printf("\t\tOSS: Scheduled #%i @ %03i.%09lu\n", msgbuff_send.mtype, shm->timePassedSec, shm->timePassedNansec);
		if(fileLinesWritten < 10000) {
			fprintf(fp, "OSS: Scheduled #%i @ %03i.%09lu\n", msgbuff_send.mtype, shm->timePassedSec, shm->timePassedNansec);
			fileLinesWritten++;
		}
		
		if(msgsnd(msgid_sending, &msgbuff_send, MSGSZ, IPC_NOWAIT) < 0) {
			perror("msgsnd");
			printf("The reply to child did not send\n");
			signalHandler();
		}
		
		removeFromQueue(1, queueOne->pid[0]);
		pcb[queueOne->index[0]]->queue = 0;
		usleep(MASTER_OVERHEAD_TIME * 10000);
		return;

	} else if(queueTwo->pid[0] > 0) {
		addToQueue(1, queueTwo->pid[0], moved);
		pcb[queueOne->index[0]]->queue = 1;
		removeFromQueue(2, queueTwo->pid[0]);
		usleep(MASTER_OVERHEAD_TIME * 10000);
		return;
		
	} else if(queueThree->pid[0] > 0) {
		addToQueue(2, queueThree->pid[0], moved);
		pcb[queueOne->index[0]]->queue = 2;
		removeFromQueue(3, queueThree->pid[0]);
		usleep(MASTER_OVERHEAD_TIME * 10000);
		return;
	}
}

void signalHandler() {
	printStats();
	
	killAll();
	
    pid_t id = getpgrp();
    killpg(id, SIGINT);
	printf("Signal received... terminating master\n");
	
	sleep(1);
    exit(EXIT_SUCCESS);
}

