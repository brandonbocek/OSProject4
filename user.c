
#include "scheduler.h"

#define CHILD_TIMEOUT 10

//static variables
long long *virtualClock; //virtual clock in nanoseconds
int *signalRecieved; //signal flag
struct PCB *pcbGroup; //group of process ctl blocks
pid_t *scheduledProcess;
int clockShmid, signalShmid, pcbGroupShmid,scheduleShmid;
int messageQueueID;
pid_t user_pid;
int timeoutValue;
int processNumber;
int childNum;
int processDuration;
int notFinished;

int main(int argc, char **argv) {

    user_pid = getpid();
	struct childMsg cMsg;
    srand(time(0) + getpid());

    //recieving command line arguments
	messageQueueID = atoi(argv[1]);
    clockShmid = atoi(argv[2]);
    signalShmid = atoi(argv[3]);
    pcbGroupShmid = atoi(argv[4]);
    scheduleShmid = atoi(argv[5]);
    processNumber = atoi(argv[6]);

    //Attaching Shared Memory Segments
	if ((virtualClock = (long long *) shmat(clockShmid, NULL, 0)) == (void *) - 1) {
        fprintf(stderr, "Error: Failed to attach memory segment %i for the shared nanoseconds.\n", clockShmid);
        abort();
	}
	if ((signalRecieved = (int *) shmat(signalShmid, NULL, 0)) == (void *) - 1) {
        fprintf(stderr, "Error: Failed to attach memory segment %i for the signal flag.\n", signalShmid);
        abort();
    }
    if ((pcbGroup = (struct PCB *) shmat(pcbGroupShmid, NULL, 0)) == (void *) - 1) {
        fprintf(stderr, "Error: Failed to attach memory segment %i for the process control block (PCB)\n", pcbGroupShmid);
        abort();
    }

    if ((scheduledProcess = (pid_t *) shmat(scheduleShmid, NULL, 0)) == (void *) - 1) {
        fprintf(stderr, "Error: Failed to attach memory segment %i for the process control block (PCB)\n", scheduleShmid);
        abort();
    }

    //initialize signal handlers
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, signalHandler);
    signal(SIGALRM, killAllChildProcesses);
    alarm(CHILD_TIMEOUT); //gives child 10 seconds to process or terminates


    notFinished = 1;
	int r, s, p, willUseEntireQuantum = 1, processIsCompleted;
	long long timeToWait;

    do {

        while(*scheduledProcess != getpid() && *signalRecieved);
		printf("The process number is %d\n", processNumber);
		// determine what is to happen with the process.
		switch(pcbGroup[processNumber].toDoRandomNum) {
			case 0:
				willUseEntireQuantum = 1;
				notFinished = 0;
				pcbGroup[processNumber].quantumTime = 0;
				pcbGroup[processNumber].processID = 0;
				goto endTheProcessNow;
				break;
			case 1:
				willUseEntireQuantum = 1;
				break;
			case 2:
				willUseEntireQuantum = 1;
				r = rand() % 6;
				s = rand() % 1001;
				timeToWait = r*1000000 + s*1000;
				usleep(timeToWait);
				break;
			case 3:
				willUseEntireQuantum = 0;
				p = rand() % 100;
				break;
			default:
				printf("Process destiny is undetermined.\n");
				break;
		}
		
		if(willUseEntireQuantum) {
			processDuration = pcbGroup[processNumber].quantumTime;
		} else{
			processDuration = pcbGroup[processNumber].quantumTime * p/100;
		}
		
        pcbGroup[processNumber].burst = processDuration;
        pcbGroup[processNumber].totalTimeRan += processDuration;

		if(pcbGroup[processNumber].totalTimeRan >= 50000) {
			notFinished = rand() % 2;
		}

        //if a process has completed it's quantum time it can be ended
        if(pcbGroup[processNumber].totalTimeRan >= pcbGroup[processNumber].quantumTime) {
			
			notFinished = 0;
			processDuration -= (pcbGroup[processNumber].totalTimeRan - pcbGroup[processNumber].quantumTime);
			pcbGroup[processNumber].totalTimeRan = pcbGroup[processNumber].quantumTime;
			pcbGroup[processNumber].processID = 0;
        }

		(*virtualClock) += processDuration;

		//Sending message to the master
		cMsg.mType = 3;
		sprintf(cMsg.infoOfUser.msgText, "%d", processNumber);

		if (msgsnd(messageQueueID, (void *) &cMsg, sizeof (cMsg.infoOfUser.msgText), IPC_NOWAIT) == -1) {
			fprintf(stderr, "Failed to send msg from user to oss\n");
		}

        (*scheduledProcess) = -1;

    } while (notFinished && (*signalRecieved));

    printf("Child %d has finished processing\n", pcbGroup[processNumber].processID);
	
	// If process is set to terminate it goes straight to here
endTheProcessNow:
    // terminating the process
    kill(user_pid, SIGTERM);
    sleep(1);
    kill(user_pid, SIGKILL);


} /*  END MAIN */


//recieves signal from master to quit process and kill child
void signalHandler(int sig) {
    printf("Child %d terminates now\n", processNumber);
    kill(user_pid, SIGKILL);
}

// Clean up
void killAllChildProcesses(int sig) {
    printf("Slave %d didn't end correctly but is ending now.\n", processNumber);
    kill(user_pid, SIGTERM);
    sleep(1);
    kill(user_pid, SIGKILL);
}
