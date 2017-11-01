#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <getopt.h>
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <sys/msg.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>

#define MSGSIZE 256
#define NANOPERSECOND 1000000000

//keys for shared memory
static const key_t pcbGroupKey = 438543;
static const key_t clockKey = 234543;
static const key_t signalKey = 465365;
static const key_t messageKey = 136532;
static const key_t scheduleKey = 764346;

//
static void initializeQs();
static void forkAndExecChild();
static void signalHandler(int);
static void killAllChildProcesses(int);
static pid_t getScheduledProcessPid();
static void interruptHandler(int);
static void updateProcessInfoAfterRunning(int);
static void pcbInitialization();
static int waitForMessageFromChild(int);
static bool processShouldBeMoved(pid_t *q, int index, int queueNum);
static void moveSomeProcessesToAnotherQ();
static void pushFront(pid_t, pid_t*, char*);
static void pushRear(pid_t, pid_t*, char*, int);
static pid_t getPIDfromQ(pid_t *m ,char*);
static void createAttachShmAndMsgQs();
static void updateStatsForFinalReport(int);
static void printHelpMenu();
static void printFinalStats();
static void detachShmAndDeleteMsgQ();
static void cleanup();

//process control block 
typedef struct PCB {
	pid_t processID;
	long long quantumTime;
	long long totalTimeRan;
	long long burst;
	long long queueSpecificTime;
	long long spawnTime;
	int toDoRandomNum; 
} PCB;

typedef struct childMsg {
	long mType;
	struct infoOfUser {
    	pid_t child_pid;
    	int childNum;
    	char msgText[MSGSIZE];
	} infoOfUser;
} childMsg;

#endif
