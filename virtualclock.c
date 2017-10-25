/*
 *  Brandon Bocek
 *  10/24/17
 *  CS 4260
 *  Project 4 Scheduler
 */

#include "scheduler.h"

static unsigned long long int theTime;
static int shmid;
static struct SharedMemory *shm;
	
int main(int argc, char *argv[]) {
	
	signal(SIGINT, signalHandler);

	/* Get the Shared Memory ID the OSS created */
	if((shmid = shmget(key, sizeof(struct SharedMemory *) * 2, 0666)) < 0) {
		fprintf(stderr, "ERROR: shmget() failed for the clock.\n");
		
		/*  Releasing shared memory and Exit */
		shmdt(shm);
		exit(EXIT_FAILURE);
	}
	
	/* Attaching Shared Memory */
    if ((shm = (struct SharedMemory *)shmat(shmid, NULL, 0)) == (struct SharedMemory *) -1) {
        fprintf(stderr, "ERROR: shmat() failed for the clock.\n");
		shmdt(shm);
        exit(EXIT_FAILURE);
    }
	
	clock_gettime(CLOCK_MONOTONIC, &shm->timeStart);
	clock_gettime(CLOCK_MONOTONIC, &shm->timeNow);

	printf("The clock has begun ticking...\n");
	
	/* Continuously update the time */	
	while(1) {
		updateTime();
	}
}

void updateTime() {
	clock_gettime(CLOCK_MONOTONIC, &shm->timeNow);
	theTime = shm->timeNow.tv_nsec - shm->timeStart.tv_nsec;

	/*  Convert nanoseconds to seconds at the threshold */
	if(theTime >= 1E19) {
		shm->timePassedNansec = theTime - 1E19;
		clock_gettime(CLOCK_MONOTONIC, &shm->timeStart);
		shm->timePassedSec += 1;
	} else {
		shm->timePassedNansec = theTime;
	}
}

/* When a kill signal from the user is received the process is killed */
void signalHandler() { 
	printf("Signal was received; Clock process will terminate.\n");
 	pid_t id = getpid();
	shmdt(shm);
    killpg(id, SIGINT);
    exit(EXIT_SUCCESS);
}

