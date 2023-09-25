#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#define MS_TO_US(x) (x*1000)

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{
    int rc = 0;
    struct thread_data* tdata = (struct thread_data *) thread_param;

    usleep(MS_TO_US(tdata->wait_to_obtain_ms));

    rc = pthread_mutex_lock(tdata->mutex);
    if(rc != 0) {
        ERROR_LOG("pthread_mutex_lock failed with %d\n", rc);
    } else {
        usleep(MS_TO_US(tdata->wait_to_release_ms));

        rc = pthread_mutex_unlock(tdata->mutex);
        if(rc != 0) {
            ERROR_LOG("pthread_mutex_unlock failed with %d\n", rc);
        } else {
            tdata->thread_complete_success = true;
        }
    }

    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    int pthread_create_status = 0;
    struct thread_data *tdata = (struct thread_data*) malloc(sizeof(struct thread_data));
    if(tdata == NULL) {
        ERROR_LOG("Failed to malloc() thread_data structure\n");
        return false;
    }
    tdata->thread_complete_success = false;
    tdata->wait_to_obtain_ms = wait_to_obtain_ms;
    tdata->wait_to_release_ms = wait_to_release_ms;
    tdata->mutex = mutex;

    pthread_create_status = pthread_create(thread, NULL, threadfunc, (void*) tdata);

    if(pthread_create_status != 0) {
        ERROR_LOG("Failed to create the thread\n");
        return false;
    }


    return true;
}

