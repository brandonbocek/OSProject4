
#include "scheduler.h"

#define MAX_SLAVES 20
#define MAX_PCB_CREATED 18
#define MAX_RUN_TIME 20000000000 //20 seconds
#define MAX_TIME_SPAWN 2000000000 //2 seconds
#define QUEUE_SIZE 20
#define QUANTUM_LENGTH 1
#define ALPHA 1
#define BETA 1
#define CLOCK_ITERATION 1000

// Global Variables
struct PCB *pcbGroup;
int *signalRecieved;
pid_t *scheduledProcess;
int clockShmid, signalShmid, pcbGroupShmid, scheduleShmid;
FILE *fp;
char *fileName;
int msgQueueID;
int childStatus;
pid_t child_pid;
long long idleTime;
long long timeToSpawn;
long long processWaitTime;
long long turnaroundTime;
long long timeProcessLifespan;
long long *virtualClock;
int vclockIncrement;
int totalNumProcesses;
int pcbIndexSpawned;
pid_t processedPID;
int maxNumSlavesCanCreate;
int alarmTimeLimit;
pid_t queue1[QUEUE_SIZE];
pid_t queue2[QUEUE_SIZE];
pid_t queue3[QUEUE_SIZE];

int main(int argc, char **argv) {

	// initialize global vars
	alarmTimeLimit = 20;
	fileName = "log.out";
	maxNumSlavesCanCreate = 20;
	timeToSpawn = 0;
	timeProcessLifespan = 0;
	totalNumProcesses = 0;
	turnaroundTime= 0;
	processWaitTime = 0;
	pcbIndexSpawned = -1;
	
	int opt;
	/* Handling command line arguments w/ ./oss  */
	while ((opt = getopt (argc, argv, "hl:")) != -1) {
		switch (opt) {
			case 'h':
				printHelpMenu();
				exit(EXIT_SUCCESS);
					
			case 'l':
				/*  Change the name of the file to write to */
				fileName = optarg;
	
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

    //create queues and shared memory and attach shmemory
	initializeQs();
    createAttachShmAndMsgQs();

    //initialize signal handlers
    signal(SIGALRM, interruptHandler);
    signal(SIGINT, interruptHandler);
    signal(SIGCHLD, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);

    //seed the alarm with the desired time to terminate
    alarm(alarmTimeLimit);


    //Set file pointer to open file 
    fp = fopen(fileName, "w");
    if (!fp) {
        fprintf(stderr, "Error: Failed to open log file\n");
        abort();
    }

    //initialize shared memory variables
    *signalRecieved = 1;
    *virtualClock = 0;
    *scheduledProcess = -1;


    /*  Set all process fields to 0 */
    pcbInitialization();

	// the Main Loop begins
    do {

        //if time to spawn child is true
        if((*virtualClock) >= timeToSpawn ) {
            //spawn child and set new random time to spawn
            forkAndExecChild();
            timeToSpawn = (*virtualClock) + rand() % MAX_TIME_SPAWN;
        }
//		printf("Calling scheduling Algo\n");
        *scheduledProcess = getScheduledProcessPid();
		
		/* Increment the virtual clock */
        vclockIncrement = 1 + rand() % CLOCK_ITERATION;
        idleTime += vclockIncrement;
        *virtualClock += vclockIncrement;

		/* Run the Scheduling Algorithm */
		moveSomeProcessesToAnotherQ();
        
		/* Waiting for message to be recieved from child before possibly clearing its pcb */
        processedPID = waitForMessageFromChild(3);
		
        if(processedPID != -1) {
            updateProcessInfoAfterRunning(processedPID);
        }
		
		//moveSomeProcessesToAnotherQ();

    } while(*virtualClock < MAX_RUN_TIME && *signalRecieved && totalNumProcesses <= maxNumSlavesCanCreate);
	/* End Main Loop */

    // End of program cleanup
    cleanup();


    return 0;
}	/* END MAIN METHOD*/


//check for message from slave indicating burst finished
int waitForMessageFromChild(int msgtype) {

    struct childMsg cMsg;

    while((*scheduledProcess) != -1) {


        if (msgrcv(msgQueueID, (void *) &cMsg, sizeof (cMsg.infoOfUser.msgText), msgtype, MSG_NOERROR | IPC_NOWAIT) == -1) {
            if (errno != ENOMSG) {
                fprintf(stderr,"Unexpected message type found in master\n");
            }
        } else {
            int processNum = atoi(cMsg.infoOfUser.msgText);
            return processNum;
        }

    }
}

void forkAndExecChild() {

	// Resetting for new fork & exec
    pcbIndexSpawned = -1;

    int i;

    //find first pcb for a given process that's open
    for(i = 0; i < MAX_PCB_CREATED; i++) {
        if(pcbGroup[i].processID == 0) {
            pcbIndexSpawned = i;
            break;
        }
    }
    //if PCB full
    if(pcbIndexSpawned == -1) {
        printf("Can't spawn anymore processes, PCB is full.\n");
    }

    //if not spawn processes
    if(pcbIndexSpawned != -1) {
        totalNumProcesses++;

        if ((child_pid = fork()) < 0) {
            fprintf(stderr, "Error: Failed to fork child\n");
			cleanup();
        }

        //If good fork, continue to call exec with all the necessary args
        if (child_pid == 0) {

            pcbGroup[pcbIndexSpawned].spawnTime = (*virtualClock);
            pcbGroup[pcbIndexSpawned].processID = getpid();
			pcbGroup[pcbIndexSpawned].quantumTime = QUANTUM_LENGTH * NANOPERSECOND;
			
			
            printf("OSS: Generating process with PID %d with duration %llu.%09llu  at time %llu.%09llu\n", getpid(), pcbGroup[pcbIndexSpawned].quantumTime / NANOPERSECOND, pcbGroup[pcbIndexSpawned].quantumTime % NANOPERSECOND,
                   (*virtualClock) / NANOPERSECOND, (*virtualClock) % NANOPERSECOND);

            fprintf(fp, "OSS: Generating process with PID %d with duration %llu.%09llu  at time %llu.%09llu\n", getpid(), pcbGroup[pcbIndexSpawned].quantumTime / NANOPERSECOND, pcbGroup[pcbIndexSpawned].quantumTime % NANOPERSECOND,
                    (*virtualClock) / NANOPERSECOND, (*virtualClock) % NANOPERSECOND);
            //close stream was causing issues with printing
            fclose(fp);

            char arg1[25], arg2[25], arg3[25], arg4[25], arg5[26], arg6[10];

            //passing message queue ID
            sprintf(arg1, "%i", msgQueueID);

            //passing clock segment ID
            sprintf(arg2, "%i", clockShmid);

            //passing signal segment ID
            sprintf(arg3, "%i", signalShmid);

            //passing pcb segment id
            sprintf(arg4,"%i",pcbGroupShmid);

            //passing scheduled process segment ID
            sprintf(arg5,"%i",scheduleShmid);

            //passing process number being spawned
            sprintf(arg6,"%i", pcbIndexSpawned);
			printf("About to exec with index: %d", pcbIndexSpawned);
			
            //executing a child process
            execl("./user", "user", arg1, arg2, arg3, arg4, arg5, arg6, (char *) NULL);
            fprintf(stderr, "Error: Failed to exec a child process");
        }

    }
    //when process spawned, push to highest priotity queue
    if(pcbIndexSpawned != -1) {
        while(pcbGroup[pcbIndexSpawned].processID <= 1);
        pushRear(pcbGroup[pcbIndexSpawned].processID,queue1, "Queue 1", pcbIndexSpawned);

    }
}

//update average turn around time after each process finishes
void updateStatsForFinalReport(int pcb) {

    long long startToFinish = (*virtualClock) - pcbGroup[pcb].spawnTime;
    timeProcessLifespan += startToFinish;
    processWaitTime += startToFinish - pcbGroup[pcb].quantumTime;
}

// determine if process should be moved to another priority queue
bool processShouldBeMoved(pid_t *q, int index, int queueNum){
	int i, j, count;
	long long sum, avg, currentWait;
	long long startToFinish;
	
    for(i = 0; i < QUEUE_SIZE; i++) {
        if(q[i] > 0) {
			for(j = 0; j < MAX_PCB_CREATED; j++){
				
				//finding a PCB in the queue to get avg wait time
				if(pcbGroup[j].processID == q[i]){
					startToFinish = (*virtualClock) - pcbGroup[j].spawnTime;
					sum += startToFinish - pcbGroup[j].quantumTime;
					count++;
				}
				// The current process's wait time
				if(i == index) {
					startToFinish = (*virtualClock) - pcbGroup[j].spawnTime;
					currentWait = startToFinish - pcbGroup[j].quantumTime;
				}
			}
        }
    }
	
	// The average wait time for the queue the process is in
	avg = sum / count;
	
	if(queueNum == 1 && (ALPHA*avg > currentWait)) {
		return 1;
	}else if(queueNum == 2 && (BETA*avg > currentWait)) {
		return 1;
	}
	return 0;
}

// pcbs with a longer wait time that the avg*x are sent back a queue
void moveSomeProcessesToAnotherQ(void) {
	
	int i;
	pid_t tempPid;
	
    for(i = 0; i < QUEUE_SIZE; i++) {
		if(queue1[i] != 0 && processShouldBeMoved(queue1, i, 1)) {
			tempPid = queue1[i];
			fprintf(fp, "OSS: Pushing process with PID: %d to Queue 2\n", queue1[i]);
			pushRear(tempPid, queue2, "Queue 2", i);
		}
	}
	
	for(i = 0; i < QUEUE_SIZE; i++) {
		if(queue2[i] != 0 && processShouldBeMoved(queue2, i, 2)) {
			tempPid = queue2[i];
			fprintf(fp, "OSS: Pushing process with PID: %d to Queue 3\n", queue2[i]);
			pushRear(tempPid, queue3, "Queue 3", i);
		}
	}
}


//update process info after burst for that round completed
void updateProcessInfoAfterRunning(int index) {

	pid_t cPid = pcbGroup[index].processID;

	long long cpuBurst = pcbGroup[index].burst;

    printf("OSS: Process with PID: %d just ran for %llu.%09llu seconds\n", pcbGroup[index].processID, cpuBurst / NANOPERSECOND, cpuBurst % NANOPERSECOND);
    fprintf(fp,"OSS: Process with PID: %d just ran for %llu.%09llu seconds\n", pcbGroup[index].processID, cpuBurst / NANOPERSECOND, cpuBurst % NANOPERSECOND);

    if(cPid == 0) {
	
		//process complete
        printf("\nProcess %d completed its scheduled time\n", pcbGroup[index].processID);
        fprintf(fp,"Process %d completed its scheduled time\n", pcbGroup[index].processID);

        updateStatsForFinalReport(index);
        pcbGroup[index].quantumTime = 0;
		pcbGroup[index].spawnTime = 0;
        pcbGroup[index].totalTimeRan = 0;
		pcbGroup[index].burst = 0;
    }

}

//Read from highest priority queue, then next highest, the finally lowest priority
pid_t getScheduledProcessPid(void) {

    if(queue1[0] != 0) {
		
        fprintf(fp, "OSS: Dispatching process %d from Queue 1 at time %llu.%09llu\n", queue1[0],(*virtualClock) / NANOPERSECOND, (*virtualClock)  % NANOPERSECOND);
        printf("OSS: Dispatching process %d from Queue 1 at time %llu.%09llu\n", queue1[0],(*virtualClock) / NANOPERSECOND, (*virtualClock)  % NANOPERSECOND);
		return getPIDfromQ(queue1, "Queue 1");
    }
    else if(queue2[0] != 0) {

        fprintf(fp, "OSS: Dispatching process %d from Queue 2 at time %llu.%09llu\n", queue2[0],(*virtualClock) / NANOPERSECOND, (*virtualClock)  % NANOPERSECOND);
        printf("OSS: Dispatching process %d from Queue 2 at time %llu.%09llu\n", queue2[0],(*virtualClock) / NANOPERSECOND, (*virtualClock)  % NANOPERSECOND);
		return getPIDfromQ(queue2, "Queue 2");
    }
    else if(queue3[0] != 0) {

        fprintf(fp, "OSS: Dispatching process %d from Queue 3 at time %llu.%09llu\n", queue3[0],(*virtualClock) / NANOPERSECOND, (*virtualClock)  % NANOPERSECOND);
        printf("OSS: Dispatching process %d from Queue 3 at time %llu.%09llu\n", queue3[0],(*virtualClock) / NANOPERSECOND, (*virtualClock)  % NANOPERSECOND);
		return getPIDfromQ(queue3, "Queue 3");
    }
    else {
        return -1;
    }

}

//place a PID to front of a queue
void pushFront(pid_t pid, pid_t *q, char* qID) {

    int i;

    for(i = QUEUE_SIZE -1; i > 0; i--) {
        q[i] = q[i-1];
    }
    q[0] = pid;


}

//place a PID to back of a queue
void pushRear(pid_t pid, pid_t *q, char *qID,int pcbLocation) {

    int i;
    for(i = 0; i < QUEUE_SIZE; i++) {
        if(q[i] == 0) {
            q[i] = pid;
            break;
        }
    }
}

//gets called when a process is scheduled
//assigns potenitally new quantum time based on queue
//returns pid at front of the specified Queue
pid_t getPIDfromQ(pid_t *q, char *qID) {

//get first element ID
    pid_t toReturnPid = q[0];
    int start = 0;

    while(q[start] != 0) {
        q[start] = q[start +1];
        start++;
    }
	
	srand(time(NULL));   	// should only be called once
	int r = rand() % 4;		// returns a pseudo-random integer between 0 and 3
	int i, j;
	for(i = 0; i < MAX_PCB_CREATED; i++) {
		
        if((pcbGroup[i].processID > 0) && (toReturnPid == pcbGroup[i].processID)) {
			fprintf(fp, "OSS: Assigning quantum to process with PID:%d\n", pcbGroup[i].processID);
			printf("OSS: Assigning quantum to process with PID:%d\n", pcbGroup[i].processID);
			pcbGroup[i].toDoRandomNum = r;
			
			if(strcmp(qID, "Queue 1") == 0) {
				pcbGroup[i].quantumTime = QUANTUM_LENGTH * NANOPERSECOND;
			} else if(strcmp(qID, "Queue 2") == 0) {
				pcbGroup[i].quantumTime = QUANTUM_LENGTH * NANOPERSECOND / 2;
			} else if(strcmp(qID, "Queue 3") == 0) {
				pcbGroup[i].quantumTime = QUANTUM_LENGTH * NANOPERSECOND / 4;
			}
		}
	}
	
    return toReturnPid;
}


// Initializing all 3 priority queues with value of 0
void initializeQs() {
    int i;
    for(i = 0; i < QUEUE_SIZE; i++) {
        queue1[i] = 0;
    }

    for(i = 0; i < QUEUE_SIZE; i++) {
        queue2[i] = 0;
    }

    for(i = 0; i < QUEUE_SIZE; i++) {
        queue3[i] = 0;
    }

}

//initialize local PCB struct before child fork
void pcbInitialization(void) {
    int i;
    for (i = 0; i < MAX_PCB_CREATED; i++) {
        pcbGroup[i].processID = 0;
        pcbGroup[i].quantumTime = 0;
        pcbGroup[i].burst = 0;
        pcbGroup[i].totalTimeRan = 0;
        pcbGroup[i].spawnTime = 0;
    }
}

void createAttachShmAndMsgQs() {

    //create message queue id
    if ((msgQueueID = msgget(messageKey, IPC_CREAT | 0666)) == -1) {
        fprintf(stderr, "Error: Failed to create message queue\n");
    }

    //create segment and attach memory segment virtual clock
    if ((clockShmid = shmget(clockKey, sizeof (long long *), 0666 | IPC_CREAT)) == -1) {
        fprintf(stderr, "Shared memory segment for virtual clock has failed during creation.\n");
        abort();
    }

    if ((virtualClock = (long long *) shmat(clockShmid, NULL, 0)) == (void *) - 1) {
        fprintf(stderr, "Error: Failed to attach memory segment %i for the virtual clock.\n", clockShmid);
        abort();
    }

    //create segment and attach memory segment for signal recieved flag
    if ((signalShmid = shmget(signalKey, sizeof (int *), 0666 | IPC_CREAT)) == -1) {
        fprintf(stderr, "Shared memory segment for signal flag has failed during creation.\n");
        abort();
    }

    if ((signalRecieved = (int*) shmat(signalShmid, NULL, 0)) == (void *) - 1) {
        fprintf(stderr, "Error: Failed to attach memory segment %i for the signal flag\n", signalShmid);
        abort();
    }
    //create segment and attach memory segment for process control block (PCB) group
    if ((pcbGroupShmid = shmget(pcbGroupKey, (sizeof(*pcbGroup) * MAX_PCB_CREATED), 0666 | IPC_CREAT)) == -1) {
        fprintf(stderr, "Error: Shared memory segment for process control block (PCB) has failed during creation.\n");
        abort();
    }

    if ((pcbGroup = (struct PCB *) shmat(pcbGroupShmid, NULL, 0)) == (void *) - 1) {
        fprintf(stderr, "Error: Failed to attach memory segment %i for the process control block (PCB)\n", pcbGroupShmid);
        abort();
    }

    //create segment and attach memory segment for process control block (PCB) group
    if ((scheduleShmid = shmget(scheduleKey, sizeof(pid_t*), 0666 | IPC_CREAT)) == -1) {
        fprintf(stderr, "Error: Shared memory segment for process control block (PCB) has failed during creation.\n");
        abort();
    }

    if ((scheduledProcess = (pid_t *) shmat(scheduleShmid, NULL, 0)) == (void *) - 1) {
        fprintf(stderr, "Error: Failed to attach memory segment %i for the process control block (PCB)\n", pcbGroupShmid);
        abort();
    }
}

void detachShmAndDeleteMsgQ() {

    //Deleting message Q
    msgctl(msgQueueID, IPC_RMID, NULL);

    //Detaching and deleting shmemory
    if (shmdt(virtualClock) == -1) {
        fprintf(stderr, "Error: Failed to detach memory segment virtual clock\n");
    }

    if (shmctl(clockShmid, IPC_RMID, NULL) == -1) {
        fprintf(stderr, "Error: Failed to remove memory segment %i for virtual clock\n", clockShmid);
    }

    if (shmdt(signalRecieved) == -1) {
        fprintf(stderr, "Error: Failed to detach memory segment for signal flag\n");
    }

    if (shmctl(signalShmid, IPC_RMID, NULL) == -1) {
        fprintf(stderr, "Error: Failed to remove memory segment %i for signal flag\n", signalShmid);
    }
    if (shmdt(pcbGroup) == -1) {
        fprintf(stderr, "Error: Failed to detach memory segment for PCB array\n");
    }

    if (shmctl(pcbGroupShmid, IPC_RMID, NULL) == -1) {
        fprintf(stderr, "Error: Failed to remove memory segment %i for PCB array\n", signalShmid);
    }

    if (shmdt(scheduledProcess) == -1) {
        fprintf(stderr, "Error: Failed to detach memory segment scheduled process ID\n");
    }

    if (shmctl(scheduleShmid, IPC_RMID, NULL) == -1) {
        fprintf(stderr, "Error: Failed to remove memory segment %i for scheduled process ID\n", pcbGroupShmid);
    }
}

/* Handling of interrupts and the alarm */
void interruptHandler(int SIG) {
    signal(SIGQUIT, SIG_IGN);
    signal(SIGINT, SIG_IGN);

    if (SIG == SIGINT) {
        fprintf(stderr, "TERMINATING MASTER FROM CTRL-C SIGNAL\n");
    }
    if (SIG == SIGALRM) {
        fprintf(stderr, "TIME LIMIT REACHED, MASTER IS TERMINATING NOW\n");
    }
    cleanup();
}

//signals children to terminate and cleans up master
void cleanup() {
    signal(SIGQUIT, SIG_IGN);
    *signalRecieved = 0;

    //kill all master/slave processes
    kill(-getpgrp(), SIGQUIT);

    //wait for children to terminate
    child_pid = wait(&childStatus);

    //End the shared resources
    detachShmAndDeleteMsgQ();
	
	fprintf(fp, "OSS: Finished now.\n");
    printf("OSS: Finished now.\n");
    
    printFinalStats();

	//finally close the file pointer
    if (fclose(fp)) {
        fprintf(stderr, "Error closing log file\n");
    }
    abort();
}

// help message
void printHelpMenu() {
	printf("\n\t\t~~Help Menu~~\n\t-h This Help Menu Printed\n");
	printf("\t-l *log file used*\t\t\t\tie. '-l log.out'\n");
}

//print report for run
void printFinalStats(void) {

	printf("*******************************************************************\n");
    printf("Total Idle time of CPU: %llu.%09llu\n",idleTime / NANOPERSECOND, idleTime % NANOPERSECOND);
	fprintf(fp, "Total Idle time of CPU: %llu.%09llu\n",idleTime / NANOPERSECOND, idleTime % NANOPERSECOND);
    
	printf("Average Turn Around Time: %llu.%09llu\n", (timeProcessLifespan / totalNumProcesses) / NANOPERSECOND, (timeProcessLifespan / totalNumProcesses) % NANOPERSECOND);
    fprintf(fp, "Average Turn Around Time: %llu.%09llu\n", (timeProcessLifespan / totalNumProcesses) / NANOPERSECOND, (timeProcessLifespan / totalNumProcesses) % NANOPERSECOND);
    
	printf("Average Time Waiting in Queue: %llu.%09llu\n",(processWaitTime / totalNumProcesses) / NANOPERSECOND, (processWaitTime / totalNumProcesses) % NANOPERSECOND );
    fprintf(fp, "Average Time Waiting in Queue: %llu.%09llu\n",(processWaitTime / totalNumProcesses) / NANOPERSECOND, (processWaitTime / totalNumProcesses) % NANOPERSECOND );

    printf("*******************************************************************\n");

}
