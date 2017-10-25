
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define IO_INTERRUPT_CHANCE 5 // This number is multipled by 1000000000 to give a time in nanoseconds -- Lower number = more frequent interupts -- Higher number reduces frequency, but does not eliminate them
#define MASTER_OVERHEAD_TIME 5 // This number is multiplied by 10,000 to give a process sleep time in microseconds. DEFAULT: 5 = .05 second sleep per cycle (spread out over cycle)
#define MAX_QUANTUM_LENGTH 5 // Quantum length is chosen at random, but this number is the maximum value allowed (in seconds)
#define MAX_USER_PROCESSES 18 // Changes max number of children process spawns
#define MSGSZ 64
#define PROCESS_SPAWN_RATE 3 // How frequent processes spawn -- calculated by rand() % PROCESS_SPAWN_RATE and then sleeping for the result in seconds

#define ALPHA 2
#define BETA 2


struct msg_buf {
	long mtype;
	char mtext[MSGSZ];
};

struct ProcessControlBlock {
	int alive;
	int creationSec;
	int finishSec;
	int index;
	int inQueueSec;
	int moveFlag;
	int pid;
	int processNumber;
	int queue;
	
	unsigned long long int cpuTime;
	unsigned long long int creationNansec;
	unsigned long long int finishNansec;
	unsigned long long int inQueueNansec;
	unsigned long long int quantum;
};

struct ProcessQueue {
	int index[MAX_USER_PROCESSES];
	int numProcesses;
    int pid[MAX_USER_PROCESSES];
};

struct SharedMemory {
	int childControl;
	int flag[MAX_USER_PROCESSES];
	int scheduledCount;
	int schedule[MAX_USER_PROCESSES];
	int timePassedSec;
	int turn;
	
	pid_t pid;
	
	struct timespec timeStart, timeNow, timePassed;
	
	unsigned long int timePassedNansec;
};

enum state {idle, want_in, in_cs};

int createProcess(int);
int removeFromQueue(int, pid_t);

key_t critical_Key = 548395;
key_t key = 325453;
key_t pcbKey = 9032582;
key_t toChild_key = 543262;
key_t toParent_key = 3925744;

//long getRightTime();

void addToQueue(int, pid_t, char*);
void advanceQueues();
void clearPCB(int);
void getProcessStats(int);
void killAll();
void printHelpMenu();
void printStats();
void scheduleProcess();
void signalHandler();
void updateTime();

#endif
