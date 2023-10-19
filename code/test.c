#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "thread-worker.h"

/* A scratch program template on which to call and
 * test thread-worker library functions as you implement
 * them.
 *
 * You can modify and use this program as much as possible.
 * This will not be graded.
 */


// Define a simple thread function
void* thread_function(void* arg) {
    printf("Thread is running\n");
    // worker_yield(); // Yield to other threads
    // printf("Thread is running again\n");
    return NULL;
}

int main(int argc, char **argv) {

	// Initialize your thread library
    // worker_init();

    // Create multiple worker threads
    worker_t thread1, thread2;
    worker_create(&thread1, NULL, thread_function, NULL);
    worker_create(&thread2, NULL, thread_function, NULL);

    // // Start the threads
    // worker_join(thread1, NULL);
    // worker_join(thread2, NULL);

    // // Clean up and print statistics
    // worker_finalize();
    // print_app_stats();

    return 0;

	return 0;
}
