
#include "scheduler.h"

static int shmid;
static struct SharedMemory *shm;
static unsigned long long int theTime;
	
int main() {
	signal(SIGINT, signalHandler);
	printf("Clock Started!\n");
	// Get shared memory id that was created by the master process	
	if((shmid = shmget(key, sizeof(struct SharedMemory *) * 2, 0666)) < 0) {
		perror("shmget");
		fprintf(stderr, "Child: shmget() returned an error! Program terminating...\n");
		killAll();
		exit(EXIT_FAILURE);
	}
	
	// Attach the shared memory to the slave process
    if ((shm = (struct SharedMemory *)shmat(shmid, NULL, 0)) == (struct SharedMemory *) -1) {
		perror("shmat");
        fprintf(stderr, "shmat() returned an error! Program terminating...\n");
		killAll();
        exit(EXIT_FAILURE);
    }
	
	clock_gettime(CLOCK_MONOTONIC, &shm->timeStart);
	clock_gettime(CLOCK_MONOTONIC, &shm->timeNow);
	
	while(1) {
		updateTime();
	}
}

// Release shared memory
void killAll() {
	shmdt(shm);
}

// Kills all when signal is received
void signalHandler() {
    pid_t id = getpid();
	printf("Signal received... terminating clock\n");
	killAll();
    killpg(id, SIGINT);
    exit(EXIT_SUCCESS);
}

void updateTime() {
	clock_gettime(CLOCK_MONOTONIC, &shm->timeNow);
	theTime = shm->timeNow.tv_nsec - shm->timeStart.tv_nsec;
	if(theTime >= 1E19) {
		shm->timePassedNansec = theTime - 1E19;
		clock_gettime(CLOCK_MONOTONIC, &shm->timeStart);
		shm->timePassedSec += 1;
	} else {
		shm->timePassedNansec = theTime;
	}
}
