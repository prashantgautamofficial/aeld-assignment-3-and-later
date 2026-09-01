#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void* threadfunc(void* thread_param)
{

    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    // Default flag to false in case of an early return error
    thread_func_args->thread_complete_success = false;

    // 1. Wait before attempting to obtain a mutex
    usleep(thread_func_args->wait_to_obtain_ms * 1000);

    // 2. Obtain the mutex lock
    if(pthread_mutex_lock(thread_func_args->mutex) !=0){
        ERROR_LOG("Failed to lock mutex.");
        // thread_func_args->thread_complete_success = false;
        return thread_param;
    }

    // 3. Hold the mutex for the specified duration
    usleep(thread_func_args->wait_to_obtain_ms * 1000);

    // 4. Release the mutex lock
    if(pthread_mutex_unlock(thread_func_args->mutex) !=0){
        ERROR_LOG("Failed to unlock mutex.");
        // thread_func_args->thread_complete_success = true;
        return thread_param;
    }

    // Mark the execution status as successful
    thread_func_args->thread_complete_success = true;
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
     *
     * return true if successful.
     *
     * See implementation details in threading.h file comment block
     */

    // 1. Dynamically allocate memory for the thread context parameters
    struct thread_data *data = (struct thread_data *)malloc(sizeof(struct thread_data));
    if(data == NULL){
        ERROR_LOG("Memory allocation failed for thread_data.");
        return false;
    }

    // 2. Initialze the struct properties
    data->mutex = mutex;
    data->wait_to_obtain_ms = wait_to_obtain_ms;
    data->wait_to_release_ms = wait_to_release_ms;
    data->thread_complete_success = false;  // Initialize to false until thread completely executes

    // 3. Create the POSIX thread passing the allocated data context
    int rc = pthread_create(thread, NULL, threadfunc, (void *)data);
    if(rc != 0){
        ERROR_LOG("Failed to spawn the POSIX thread.");
        free(data); // Clean up allocated memory on thread creation failure
        return false;
    }

    return true;
}

