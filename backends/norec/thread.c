#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <assert.h>
#include <stdlib.h>
#include <sched.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include "thread.h"
#include "types.h"
// #include "rapl.h"

#define B2POW(e) (((e) == 0) ? 1 : (2 << ((e) - 1)))
#define MSR_RAPL_POWER_UNIT 0x606
#define MSR_PKG_ENERGY_STATUS 0x611
#define RAPL_ENERGY_UNIT 0.000000001

static THREAD_LOCAL_T    global_threadId;
static long              global_numThread       = 1;
static THREAD_BARRIER_T* global_barrierPtr      = NULL;
static long*             global_threadIds       = NULL;
static THREAD_ATTR_T     global_threadAttr;
static THREAD_T*         global_threads         = NULL;
static void            (*global_funcPtr)(void*) = NULL;
static void*             global_argPtr          = NULL;
static volatile bool_t   global_doShutdown      = FALSE;

static double 		 package_before		= 0;
static double		 energy_units		= 0;

void start_energy()
{
    int fd;
    unsigned long long msr;

    fd = open("/dev/cpu/0/msr", O_RDONLY);
    if (fd < 0) {
        perror("open");
        exit(1);
    }

    if (pread(fd, &msr, sizeof(msr), MSR_RAPL_POWER_UNIT) != sizeof(msr)) {
        perror("pread");
        exit(1);
    }

    //energy_units = RAPL_ENERGY_UNIT * pow(0.5, (double)((msr >> 8) & 0x1f));
    energy_units = 1.0 / (double)B2POW(((msr >> 8) & 0x1f));

    if (pread(fd, &msr, sizeof(msr), MSR_PKG_ENERGY_STATUS) != sizeof(msr)) {
        perror("pread");
        exit(1);
    }

    package_before = (double)msr * energy_units;

    if (close(fd) < 0)
    {
        perror("close");
	exit(1);
    }
}

void end_energy()
{
    int fd;
    unsigned long long msr;
    double package_after, package_energy;

    fd = open("/dev/cpu/0/msr", O_RDONLY);
    if (fd < 0) {
        perror("open");
        exit(1);
    }
	
    if (pread(fd, &msr, sizeof(msr), MSR_PKG_ENERGY_STATUS) != sizeof(msr)) {
        perror("pread");
        exit(1);
    }

    package_after = (double)msr * energy_units;

    package_energy = package_after - package_before;

    printf("Energy = %.6f\n", package_energy);
}

static void threadWait (void* argPtr)
{
    long threadId = *(long*)argPtr;

    THREAD_LOCAL_SET(global_threadId, (long)threadId);

    cpu_set_t my_set;
    CPU_ZERO(&my_set);
    CPU_SET(threadId % 64, &my_set);
    sched_setaffinity(0, sizeof(cpu_set_t), &my_set);

    while (1) {
        THREAD_BARRIER(global_barrierPtr, threadId); /* wait for start parallel */
        if (global_doShutdown) {
            break;
        }
        global_funcPtr(global_argPtr);
        THREAD_BARRIER(global_barrierPtr, threadId); /* wait for end parallel */
        if (threadId == 0) {
            // endEnergy();
            break;
        }
    }
}

void thread_startup (long numThread)
{
    long i;

    global_numThread = numThread;
    global_doShutdown = FALSE;

    /* Set up barrier */
    assert(global_barrierPtr == NULL);
    global_barrierPtr = THREAD_BARRIER_ALLOC(numThread);
    assert(global_barrierPtr);
    THREAD_BARRIER_INIT(global_barrierPtr, numThread);

    /* Set up ids */
    THREAD_LOCAL_INIT(global_threadId);
    assert(global_threadIds == NULL);
    global_threadIds = (long*)malloc(numThread * sizeof(long));
    assert(global_threadIds);
    for (i = 0; i < numThread; i++) {
        global_threadIds[i] = i;
    }

    /* Set up thread list */
    assert(global_threads == NULL);
    global_threads = (THREAD_T*)malloc(numThread * sizeof(THREAD_T));
    assert(global_threads);

    start_energy();

    /* Set up pool */
    THREAD_ATTR_INIT(global_threadAttr);
    for (i = 1; i < numThread; i++) {
        THREAD_CREATE(global_threads[i],
                      global_threadAttr,
                      &threadWait,
                      &global_threadIds[i]);
    }

    /*
     * Wait for primary thread to call thread_start
     */
}


void thread_start (void (*funcPtr)(void*), void* argPtr)
{
    global_funcPtr = funcPtr;
    global_argPtr = argPtr;

    long threadId = 0; /* primary */
    threadWait((void*)&threadId);
}


void thread_shutdown ()
{
    /* Make secondary threads exit wait() */
    global_doShutdown = TRUE;
    THREAD_BARRIER(global_barrierPtr, 0);

    long numThread = global_numThread;

    long i;
    for (i = 1; i < numThread; i++) {
        THREAD_JOIN(global_threads[i]);
    }

    THREAD_BARRIER_FREE(global_barrierPtr);
    global_barrierPtr = NULL;

    free(global_threadIds);
    global_threadIds = NULL;

    free(global_threads);
    global_threads = NULL;

    global_numThread = 1;
}

barrier_t *barrier_alloc() {
    return (barrier_t *)malloc(sizeof(barrier_t));
}

void barrier_free(barrier_t *b) {
    free(b);
}

void barrier_init(barrier_t *b, int n) {
    pthread_cond_init(&b->complete, NULL);
    pthread_mutex_init(&b->mutex, NULL);
    b->count = n;
    b->crossing = 0;
}

void barrier_cross(barrier_t *b) {
    pthread_mutex_lock(&b->mutex);
    /* One more thread through */
    b->crossing++;
    /* If not all here, wait */
    if (b->crossing < b->count) {
        pthread_cond_wait(&b->complete, &b->mutex);
    } else {
        /* Reset for next time */
        b->crossing = 0;
        pthread_cond_broadcast(&b->complete);
    }
    pthread_mutex_unlock(&b->mutex);
}


void thread_barrier_wait()
{
    long threadId = thread_getId();
    THREAD_BARRIER(global_barrierPtr, threadId);
}

long thread_getId()
{
    return (long)THREAD_LOCAL_GET(global_threadId);
}

long thread_getNumThread()
{
    return global_numThread;
}

