
#include "scheduler.h"

#define QUEUE_SIZE 20
#define QUANTUM_LENGTH 1
#define ALPHA 1
#define BETA 1

//key time variables
static const long long CLOCK_ITERATION = 1000; //.0001s
static const int MAX_PCB_CREATED = 18;
static const int MAX_TIME_SPAWN = 200000000; //.1s
static const int MAX_SCHEDULED_TIME = 900000000; //.09s THE QUANTUM TIME
static const long MAX_TIME = 20000000000; //20s max run time



//static variables
static long long *virtualClock; //virtual clock in nanoseconds
static int *signalRecieved; //flag to tell current child when to exit/die
static pid_t *scheduledProcess; //PID of next process being processed
static int clockShmid, signalShmid, pcbGroupShmid, scheduleShmid;
static int numSlaves, timeToTerminate;
static char *logFileName;
static FILE *fp;
static int childStatus;
static int messageQueueID;
static pid_t child_pid;
static int childNum;
static int childProcessed;
static struct PCB *pcbGroup; //process control block struct
static pid_t queue1[QUEUE_SIZE], queue2[QUEUE_SIZE],queue3[QUEUE_SIZE]; //queues
static  int loopIncremement;
static long long idleTime;
static long long timeToSpawn;
static long long turnaroundTime;
static long long processWaitTime;
static long long totalProcessLifeTime;
static int totalNumProcesses;
static int processNumberBeingSpawned;
static pid_t processedPID;
static int maxNumSlaves;

//static prototypes
static void createAttachShmAndMsgQs();
static void evaluateCmdLineArguments(int, char **);
static void printHelpMenu();
static void interruptHandler(int);
static void cleanup();
static void detachShmAndDeleteMsgQ();
static void pcbInitialization();
static void initializeQs();
static void printQueue(pid_t *);
static pid_t scheduleNextProcess();
static void forkMasterExecuteSlave();
static pid_t popFront(pid_t *m ,char*);
static void pushFront(pid_t, pid_t*, char*);
static void pushRear(pid_t, pid_t*, char*, int);
static int waitForMessageFromChild(int);
static bool processShouldBeMoved(pid_t *q, int index, int queueNum);
static void moveSomeProcessesToAnotherQ();
static void updateStatsForFinalReport(int);
static void updateProcessInfoAfterRunning(int);
static void printFinalStats();



int main(int argc, char **argv) {

    numSlaves = 5;
    timeToTerminate = 20;
    logFileName = "log.out"; 
    childNum = 1;
    childProcessed = 0;
	//new cmd line arguments
	maxNumSlaves = 20;

    //initialize other key variablesstatic long long idleTime;
    timeToSpawn = 0;
    turnaroundTime= 0;
    processWaitTime = 0;
    totalProcessLifeTime = 0;
    totalNumProcesses = 0;
    processNumberBeingSpawned = -1;
	
	int opt;
	/* Handling command line arguments w/ ./oss  */
	while ((opt = getopt (argc, argv, "hl:")) != -1) {
		switch (opt) {
			case 'h':
				printHelpMenu();
				exit(EXIT_SUCCESS);
					
			case 'l':
				/*  Change the name of the file to write to */
				logFileName = optarg;
	
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
    alarm(timeToTerminate);


    //Open file and mark the beginning of the new log
    fp = fopen(logFileName, "w");
    if (!fp) {
        fprintf(stderr, "Error opening log file\n");
        abort();
    }

    //initialize shared memory variables
    *signalRecieved = 1;
    *virtualClock = 0;
    *scheduledProcess = -1;


    /*  Set all process fields to 0 */
    pcbInitialization();

    //main loop which spanws and updates metrics for a process until time is up or signal flag recieved
    do {

        //if time to spawn child is true
        if((*virtualClock) >= timeToSpawn ) {
            //spawn child and set new random time to spawn
            forkMasterExecuteSlave();
            timeToSpawn = (*virtualClock) + rand() % MAX_TIME_SPAWN;
        }

        *scheduledProcess = scheduleNextProcess();
		
		/* Increment the virtual clock */
        loopIncremement = 1 + rand() % CLOCK_ITERATION;
        idleTime += loopIncremement;
        *virtualClock += loopIncremement;

        /* Waiting for message to be recieved from child before possibly clearing its pcb */
        processedPID = waitForMessageFromChild(3);
		
        if(processedPID != -1) {
            updateProcessInfoAfterRunning(processedPID);
        }
		
		/* Run the Scheduling Algorithm */
		moveSomeProcessesToAnotherQ();

    } while(*virtualClock < MAX_TIME && *signalRecieved && totalNumProcesses <= maxNumSlaves);


    // End of program cleanup
    cleanup();


    return 0;
}	/* END MAIN */


static void forkMasterExecuteSlave() {

    processNumberBeingSpawned = -1; //reinitialize variable

    int i;

    //find first pcb for a given process that's open
    for(i = 0; i < MAX_PCB_CREATED; i++) {
        if(pcbGroup[i].processID == 0) {
            processNumberBeingSpawned = i;
            pcbGroup[i].processID = 1;
            break;
        }
    }
    //if PCB full
    if(processNumberBeingSpawned == -1) {
        printf("Can't spawn anymore processes, PCB is full.\n");
    }

    //if not spawn processes
    if(processNumberBeingSpawned != -1) {
        totalNumProcesses++;

        //exit on bad fork
        if ((child_pid = fork()) < 0) {
            fprintf(stderr, "Failed to fork Child# %d\n", childNum);
        }

        //If good fork, continue to call exec with all the necessary args
        if (child_pid == 0) {

            //pcbGroup[processNumberBeingSpawned].queueSpecificTime = queue1TimeValue;
            pcbGroup[processNumberBeingSpawned].quantumTime =  1 + rand() % MAX_SCHEDULED_TIME;
            pcbGroup[processNumberBeingSpawned].spawnTime = (*virtualClock);
            pcbGroup[processNumberBeingSpawned].processID = getpid();

            printf("OSS: Generating process with PID %d with duration %llu.%09llu  at time %llu.%09llu\n", getpid(), pcbGroup[processNumberBeingSpawned].quantumTime / NANOPERSECOND, pcbGroup[processNumberBeingSpawned].quantumTime % NANOPERSECOND,
                   (*virtualClock) / NANOPERSECOND, (*virtualClock) % NANOPERSECOND);

            fprintf(fp, "OSS: Generating process with PID %d with duration %llu.%09llu  at time %llu.%09llu\n", getpid(), pcbGroup[processNumberBeingSpawned].quantumTime / NANOPERSECOND, pcbGroup[processNumberBeingSpawned].quantumTime % NANOPERSECOND,
                    (*virtualClock) / NANOPERSECOND, (*virtualClock) % NANOPERSECOND);
            //close stream was causing issues with printing
            fclose(fp);

            //main block to pass arguments to children

            //pass message queue ID
            char arg1[25];
            sprintf(arg1, "%i", messageQueueID);

            //pass clock segment ID
            char arg2[25];
            sprintf(arg2, "%i", clockShmid);

            //pass signal segment ID
            char arg3[25];
            sprintf(arg3, "%i", signalShmid);

            //pass pcb segment id
            char arg4[25];
            sprintf(arg4,"%i",pcbGroupShmid);

            //pass scheduled process segment ID
            char arg5[25];
            sprintf(arg5,"%i",scheduleShmid);

            //pass proces number being spawned
            char arg6[5];
            sprintf(arg6,"%i", processNumberBeingSpawned);

            //invoke slave executable and pass arguments
            execl("./user", "user", arg1, arg2, arg3, arg4, arg5, arg6, (char *) NULL);
            fprintf(stderr, "Error: Failed to exec a child process");
        }

    }
    //when process spawned, push to queue 1
    if(processNumberBeingSpawned != -1) {
        while(pcbGroup[processNumberBeingSpawned].processID <= 1);
        pushRear(pcbGroup[processNumberBeingSpawned].processID,queue1, "Queue 1", processNumberBeingSpawned);

    }
}

//check for message from slave indicating burst finished
static int waitForMessageFromChild(int msgtype) {

    struct childMsg cMsg;

    while((*scheduledProcess) != -1) {


        if (msgrcv(messageQueueID, (void *) &cMsg, sizeof (cMsg.infoOfUser.msgText), msgtype, MSG_NOERROR | IPC_NOWAIT) == -1) {
            if (errno != ENOMSG) {
                fprintf(stderr,"Unexpected message type found in master\n");
            }
        } else {
            int processNum = atoi(cMsg.infoOfUser.msgText);

            printf("OSS: Process with PID %d has ran for %llu.%09llu out of %llu.%09llu\n",pcbGroup[processNum].processID,pcbGroup[processNum].totalTimeRan / NANOPERSECOND, pcbGroup[processNum].totalTimeRan % NANOPERSECOND,
                   pcbGroup[processNum].quantumTime / NANOPERSECOND,pcbGroup[processNum].quantumTime % NANOPERSECOND );
            fprintf(fp,"OSS: Process with PID %d has ran for %llu.%09llu out of %llu.%09llu\n",pcbGroup[processNum].processID,pcbGroup[processNum].totalTimeRan / NANOPERSECOND, pcbGroup[processNum].totalTimeRan % NANOPERSECOND,
                    pcbGroup[processNum].quantumTime / NANOPERSECOND,pcbGroup[processNum].quantumTime % NANOPERSECOND );

            return processNum;
        }

    }
}

//update average turn around time after each process finishes
static void updateStatsForFinalReport(int pcb) {

    long long startToFinish = (*virtualClock) - pcbGroup[pcb].spawnTime;
    totalProcessLifeTime += startToFinish;
    processWaitTime += startToFinish - pcbGroup[pcb].quantumTime;
}

static bool processShouldBeMoved(pid_t *q, int index, int queueNum){
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
	avg = sum / count;
	
	if(queueNum == 1 && (ALPHA*avg > currentWait)) {
		return 1;
	}else if(queueNum == 2 && (BETA*avg > currentWait)) {
		return 1;
	}
	return 0;
}

static void moveSomeProcessesToAnotherQ(void) {
	
	int i;
	pid_t tempPid;
	
    for(i = 0; i < QUEUE_SIZE; i++) {
		if(queue1[i] != 0 && processShouldBeMoved(queue1, i, 1)) {
			tempPid = queue1[i];
			fprintf(fp, "OSS: Pushing process with PID %d to Queue 2\n", queue1[i]);
			pushRear(tempPid, queue2, "Queue 2", i);
		}
	}
	
	for(i = 0; i < QUEUE_SIZE; i++) {
		if(queue2[i] != 0 && processShouldBeMoved(queue2, i, 2)) {
			tempPid = queue2[i];
			fprintf(fp, "OSS: Pushing process with PID %d to Queue 3\n", queue2[i]);
			pushRear(tempPid, queue3, "Queue 3", i);
		}
	}
}


//update process info after burst for that round completed
static void updateProcessInfoAfterRunning(int index) {

	pid_t cPid = pcbGroup[index].processID;

	long long cpuBurst = pcbGroup[index].burst;

    printf("OSS: Process with PID:%d just ran for %llu.%09llu seconds\n", pcbGroup[index].processID, cpuBurst / NANOPERSECOND, cpuBurst % NANOPERSECOND);
    fprintf(fp,"OSS: Process with PID:%d just ran for %llu.%09llu seconds\n", pcbGroup[index].processID, cpuBurst / NANOPERSECOND, cpuBurst % NANOPERSECOND);

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

//get first process in a non empty queue for processing

static  pid_t scheduleNextProcess(void) {

    //look at first element in queue, process first queue where it isn't empty at position 100000000

    if(queue1[0] != 0) {
		
        fprintf(fp, "OSS: Dispatching process %d from Queue 1 at time %llu.%09llu\n", queue1[0],(*virtualClock) / NANOPERSECOND, (*virtualClock)  % NANOPERSECOND);
        printf("OSS: Dispatching process %d from Queue 1 at time %llu.%09llu\n", queue1[0],(*virtualClock) / NANOPERSECOND, (*virtualClock)  % NANOPERSECOND);
		return popFront(queue1, "Queue 1");
    }
    else if(queue2[0] != 0) {

        fprintf(fp, "OSS: Dispatching process %d from Queue 2 at time %llu.%09llu\n", queue2[0],(*virtualClock) / NANOPERSECOND, (*virtualClock)  % NANOPERSECOND);
        printf("OSS: Dispatching process %d from Queue 2 at time %llu.%09llu\n", queue2[0],(*virtualClock) / NANOPERSECOND, (*virtualClock)  % NANOPERSECOND);
		return popFront(queue2, "Queue 2");
    }
    else if(queue3[0] != 0) {

        fprintf(fp, "OSS: Dispatching process %d from Queue 3 at time %llu.%09llu\n", queue3[0],(*virtualClock) / NANOPERSECOND, (*virtualClock)  % NANOPERSECOND);
        printf("OSS: Dispatching process %d from Queue 3 at time %llu.%09llu\n", queue3[0],(*virtualClock) / NANOPERSECOND, (*virtualClock)  % NANOPERSECOND);
		return popFront(queue3, "Queue 3");
    }
    else {
        return -1;
    }

}


//push pid to front of array
static void pushFront(pid_t pid, pid_t *q, char* qID) {

    int i;

    for(i = QUEUE_SIZE -1; i > 0; i--) {
        q[i] = q[i-1];
    }
    q[0] = pid;


}

//push pid to back of queue
static void pushRear(pid_t pid, pid_t *q, char *qID,int pcbLocation) {

    int i;
    for(i = 0; i < QUEUE_SIZE; i++) {
        if(q[i] == 0) {
            q[i] = pid;
            break;
        }
    }
}

//pop pid off front of queue, shift array then return pid
static pid_t popFront(pid_t *q, char *qID) {

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
				pcbGroup[i].quantum = QUANTUM_LENGTH * NANOPERSECOND;
			} else if(strcmp(qID, "Queue 2") == 0) {
				pcbGroup[i].quantum = QUANTUM_LENGTH * NANOPERSECOND / 2;
			} else if(strcmp(qID, "Queue 3") == 0) {
				pcbGroup[i].quantum = QUANTUM_LENGTH * NANOPERSECOND / 4;
			}
		}
	}
	
    return toReturnPid;
}


//initialize queues with value of 0
static void initializeQs() {
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
static void pcbInitialization(void) {
    int i;
    for (i = 0; i < MAX_PCB_CREATED; i++) {
        pcbGroup[i].processID = 0;
        pcbGroup[i].processID = 0;
        pcbGroup[i].quantumTime = 0;
        pcbGroup[i].burst = 0;
        pcbGroup[i].totalTimeRan = 0;
        pcbGroup[i].spawnTime = 0;
    }

}

static void createAttachShmAndMsgQs() {

    //create message queue id
    if ((messageQueueID = msgget(messageKey, IPC_CREAT | 0777)) == -1) {
        fprintf(stderr, "Error: Failed to create message queue\n");
    }

    //create segment and attach memory segment virtual clock
    if ((clockShmid = shmget(clockKey, sizeof (long long *), 0777 | IPC_CREAT)) == -1) {
        fprintf(stderr, "Shared memory segment for virtual clock has failed during creation.\n");
        abort();
    }

    if ((virtualClock = (long long *) shmat(clockShmid, NULL, 0)) == (void *) - 1) {
        fprintf(stderr, "Error: Failed to attach memory segment %i for the virtual clock.\n", clockShmid);
        abort();
    }

    //create segment and attach memory segment for signal recieved flag
    if ((signalShmid = shmget(signalKey, sizeof (int *), 0777 | IPC_CREAT)) == -1) {
        fprintf(stderr, "Shared memory segment for signal flag has failed during creation.\n");
        abort();
    }

    if ((signalRecieved = (int*) shmat(signalShmid, NULL, 0)) == (void *) - 1) {
        fprintf(stderr, "Error: Failed to attach memory segment %i for the signal flag\n", signalShmid);
        abort();
    }
    //create segment and attach memory segment for process control block (PCB) group
    if ((pcbGroupShmid = shmget(pcbGroupKey, (sizeof(*pcbGroup) * MAX_PCB_CREATED), 0777 | IPC_CREAT)) == -1) {
        fprintf(stderr, "Shared memory segment for process control block (PCB) has failed during creation.\n");
        abort();
    }

    if ((pcbGroup = (struct PCB *) shmat(pcbGroupShmid, NULL, 0)) == (void *) - 1) {
        fprintf(stderr, "Error: Failed to attach memory segment %i for the process control block (PCB)\n", pcbGroupShmid);
        abort();
    }

    //create segment and attach memory segment for process control block (PCB) group
    if ((scheduleShmid = shmget(scheduleKey, sizeof(pid_t*), 0777 | IPC_CREAT)) == -1) {
        fprintf(stderr, "Shared memory segment for process control block (PCB) has failed during creation.\n");
        abort();
    }

    if ((scheduledProcess = (pid_t *) shmat(scheduleShmid, NULL, 0)) == (void *) - 1) {
        fprintf(stderr, "Error: Failed to attach memory segment %i for the process control block (PCB)\n", pcbGroupShmid);
        abort();
    }
}

void detachShmAndDeleteMsgQ() {

    //Deleting message Q
    msgctl(messageQueueID, IPC_RMID, NULL);

    //Detaching and deleting shmemory
    if (shmdt(virtualClock) == -1) {
        fprintf(stderr, "Error: Failed to detach memory segment virtual clock\n");
    };

    if (shmctl(clockShmid, IPC_RMID, NULL) == -1) {
        fprintf(stderr, "Error: Failed to remove memory segment %i for virtual clock\n", clockShmid);
    }

    if (shmdt(signalRecieved) == -1) {
        fprintf(stderr, "Error: Failed to detach memory segment for signal flag\n");
    };

    if (shmctl(signalShmid, IPC_RMID, NULL) == -1) {
        fprintf(stderr, "Error: Failed to remove memory segment %i for signal flag\n", signalShmid);
    }
    if (shmdt(pcbGroup) == -1) {
        fprintf(stderr, "Error: Failed to detach memory segment for PCB array\n");
    };

    if (shmctl(pcbGroupShmid, IPC_RMID, NULL) == -1) {
        fprintf(stderr, "Error: Failed to remove memory segment %i for PCB array\n", signalShmid);
    }

    if (shmdt(scheduledProcess) == -1) {
        fprintf(stderr, "Error: Failed to detach memory segment scheduled process ID\n");
    };

    if (shmctl(scheduleShmid, IPC_RMID, NULL) == -1) {
        fprintf(stderr, "Error: Failed to remove memory segment %i for scheduled process ID\n", pcbGroupShmid);
    }
}

/* Handling of interrupts and the alarm */
static void interruptHandler(int SIG) {
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
static void cleanup() {
    signal(SIGQUIT, SIG_IGN);
    *signalRecieved = 0;

    //kill all master/slave processes
    kill(-getpgrp(), SIGQUIT);

    //wait for children to terminate
    child_pid = wait(&childStatus);

    //eliminate shared resources
    detachShmAndDeleteMsgQ();
	
	fprintf(fp, "OSS: Ending now.\n");
    printf("OSS: Ending now.\n");
    
    printFinalStats();

    if (fclose(fp)) {
        fprintf(stderr, "Error closing log file\n");
    }
    abort();
}

// help message
void printHelpMenu() {
	printf("\n\t\t~~Help Menu~~\n\t-h This Help Menu Printed\n");
	//printf("\t-s *# of slave processes to spawn*\t\tie. '-s 5'\n");
	printf("\t-l *log file used*\t\t\t\tie. '-l log.out'\n");
	//printf("\t-t *time in seconds the master will terminate*\tie. -t 20\n\n");
}

//print report for run
static void printFinalStats(void) {

	printf("*******************************************************************\n");
    printf("Total Idle time of CPU: %llu.%09llu\n",idleTime / NANOPERSECOND, idleTime % NANOPERSECOND);
	fprintf(fp, "Total Idle time of CPU: %llu.%09llu\n",idleTime / NANOPERSECOND, idleTime % NANOPERSECOND);
    
	printf("Average Turn Around Time: %llu.%09llu\n", (totalProcessLifeTime / totalNumProcesses) / NANOPERSECOND, (totalProcessLifeTime / totalNumProcesses) % NANOPERSECOND);
    fprintf(fp, "Average Turn Around Time: %llu.%09llu\n", (totalProcessLifeTime / totalNumProcesses) / NANOPERSECOND, (totalProcessLifeTime / totalNumProcesses) % NANOPERSECOND);
    
	printf("Average Time Waiting in Queue: %llu.%09llu\n",(processWaitTime / totalNumProcesses) / NANOPERSECOND, (processWaitTime / totalNumProcesses) % NANOPERSECOND );
    fprintf(fp, "Average Time Waiting in Queue: %llu.%09llu\n",(processWaitTime / totalNumProcesses) / NANOPERSECOND, (processWaitTime / totalNumProcesses) % NANOPERSECOND );

    printf("*******************************************************************\n");

}
