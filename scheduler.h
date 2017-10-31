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

//static constants
static const key_t clockKey = 5235423;
static const key_t signalKey = 1562345;
static const key_t scheduleKey = 823458;
static const key_t messageKey = 6547345;
static const key_t pcbGroupKey = 783452;
static const int MAX_SLAVES = 20;

//static protoypes
static void signalHandler(int);
static void killAllChildProcesses(int);

//process control block 
typedef struct PCB {
	pid_t processID;
	long long quantumTime;
	long long totalTimeRan;
	long long burst;
	long long queueSpecificTime;
	long long spawnTime;
	long long quantum;
	int toDoRandomNum; 
} PCB;

#define MSGSIZE 256
#define NANOPERSECOND 1000000000


typedef struct childMsg {
  long mType;
  struct infoOfUser {
    pid_t child_pid;
    int childNum;
    char msgText[MSGSIZE];
  }
  infoOfUser;
}
childMsg;

#endif