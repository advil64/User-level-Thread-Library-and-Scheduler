// File:	thread-worker.c

// List all group member's name:
// username of iLab: ac1771, tsc95
// iLab Server: cs416f23-40

#include "thread-worker.h"

// Global counter for total context switches and
// average turn around and response time
long tot_cntx_switches = 0;
double avg_turn_time = 0;
double avg_resp_time = 0;
uint first_run = 1;

/*QUEUE FUNCTIONS START HERE*/

// Define a structure for the elements in the scheduler queue and finished queue
struct QueueNode
{
    struct TCB *myTCB;
    struct QueueNode *next;
};

struct MutexQueue{
    struct QueueNode *front;
    struct QueueNode *rear;
};

struct FinishedNode
{
    worker_t thread_id;
    struct FinishedNode *next;
};

struct Queue
{
    struct QueueNode *front;
    struct QueueNode *rear;
    struct FinishedNode *finished_threads;
    struct QueueNode *running_thread;
};

struct Queue *myQueue;
struct MutexQueue *mutexQueue;

// Function to check if the queue is empty
int isEmpty()
{
    return (myQueue->front == NULL);
}

// TODO: Need to put this in the thread-worker.h
int isEmptyMultiQueue(struct Queue *currentQueue) {
    return (currentQueue->front == NULL);
}

void enqueue_mutex(struct QueueNode *newNode) {

    newNode->next = NULL;

    // If the queue is empty, set the new node as both front and rear
    if (mutexQueue->rear == NULL) {
        mutexQueue->front = newNode;
        mutexQueue->rear = newNode;
    } else {
        // Otherwise, add the new node to the rear
        mutexQueue->rear->next = newNode;
        mutexQueue->rear = newNode;
    }
}

struct QueueNode *dequeue_mutex()
{
    if (mutexQueue->front == NULL)
    {
        return NULL;
    }

    struct QueueNode *temp = mutexQueue->front;
    mutexQueue->front = mutexQueue->front->next;
    puts("Mutex dequeued");
    return temp;
}

// Adds a queue node at the point where its elapsed time is less than the next elapsed time
// IE it adds the node in elapsed time order from least elapsed to most elapsed
void enqueue_psjf(struct QueueNode *newNode)
{
    if (!newNode)
    {
        perror("Memory allocation failed");
        exit(1);
    }

    if (isEmpty(myQueue))
    {
        myQueue->front = myQueue->rear = newNode;
    }
    else
    {
        struct QueueNode *curr = myQueue->front;
        struct QueueNode *prev = NULL;
        while (curr != NULL)
        {
            if (newNode->myTCB->elapsed < curr->myTCB->elapsed)
            {
                newNode->next = curr;
                if (prev == NULL)
                {
                    myQueue->front = newNode;
                }
                else
                {
                    prev->next = newNode;
                }
				break;
            }
			prev = curr;
            curr = curr->next;
        }
        if (curr == NULL)
        {
            myQueue->rear->next = newNode;
            myQueue->rear = newNode;
            newNode->next = NULL;
        }
    }
    puts("Node enqueued psjf style");
}

// Function to add an element to the rear of the finished threads queue
void enqueue_finished(struct FinishedNode *newNode)
{
    if (myQueue->finished_threads == NULL)
    {
        myQueue->finished_threads = newNode;
    }
    else
    {
        newNode->next = myQueue->finished_threads;
        myQueue->finished_threads = newNode;
    }

    puts("Finished node enqueued");
}

// Function to remove an element from the front of the queue
struct QueueNode *dequeue_psjf()
{
    if (isEmpty(myQueue))
    {
        printf("Queue is empty\n");
        exit(1);
    }

    struct QueueNode *temp = myQueue->front;
    myQueue->front = myQueue->front->next;
    puts("Node dequeued");
    return temp;
}

struct FinishedNode *find_finished_node_by_thread_id(worker_t target_thread_id)
{
    struct FinishedNode *current = myQueue->finished_threads;

    while (current != NULL)
    {
        if (current->thread_id == target_thread_id)
        {
            return current; // Found the node with the target thread ID
        }
        current = current->next;
    }

    return NULL; // Target thread ID not found in the queue
};

void remove_finished_node_from_queue(struct FinishedNode* target_thread)
{
    struct FinishedNode* previous = NULL;
    struct FinishedNode* current = myQueue->finished_threads;

    while (current != NULL)
    {
        if (current == target_thread)
        {
            // Found the node with the target thread ID, remove it
            if (previous != NULL)
            {
                previous->next = current->next; // Update the next pointer of the previous node
            }
            else
            {
                myQueue->finished_threads = current->next; // Update the head of the queue
            }

            return; // Return the removed node
        }

        previous = current;
        current = current->next;
    }

    return; // Target thread ID not found in the queue
}


// Function to remove a specific node from the queue
void remove_node(struct QueueNode *targetNode)
{
    if (myQueue == NULL || isEmpty(myQueue) || targetNode == NULL)
    {
        return; // Invalid input or target node not found
    }

    // If the target node is the front of the queue
    if (myQueue->front == targetNode)
    {
        myQueue->front = targetNode->next;
        if (myQueue->front == NULL)
        {
            // If the removed node was the last node, update rear
            myQueue->rear = NULL;
        }
        return;
    }

    // If the target node is not the front
    struct QueueNode *current = myQueue->front;
    while (current != NULL && current->next != targetNode)
    {
        current = current->next;
    }

    if (current == NULL)
    {
        return; // Target node not found in the queue
    }

    current->next = targetNode->next;
    if (current->next == NULL)
    {
        // If the removed node was the rear, update rear
        myQueue->rear = current;
    }
}

/*QUEUE FUNCTIONS END HERE*/

// Here the scheduler context is set, it gets swapped to when a thread is interrupted/finished running
ucontext_t sched_cctx;
// And here's one for the main context!
struct QueueNode *main_thread;

/*TIMER FUNCTIONS START HERE*/
struct sigaction sa;
struct itimerval timer;

void timer_handler(int signum) {
    printf("RING RING! The timer has gone off\n");
    myQueue->running_thread->myTCB->thread_status = 0; // ready for execution
    enqueue_psjf(myQueue->running_thread);
    tot_cntx_switches++;
    swapcontext(&myQueue->running_thread->myTCB->cctx, &sched_cctx);
}
/*TIMER FUNCTIONS END HERE*/

/*SCHEDULER FUNCTIONS START HERE*/

// Function to get the current time
void get_current_time(struct timespec* ts) {
    clock_gettime(CLOCK_MONOTONIC, ts);
}

/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf()
{
    clock_t start, end;
    double cpu_time_used;
    // process having the smallest executing time is chosen for next execution
    while (!isEmpty(myQueue))
    {
        struct QueueNode *shortestJob = myQueue->running_thread = dequeue_psjf(); // already starts at shortest job by default
        myQueue->running_thread->myTCB->thread_status = 1; // thread is running
        setitimer(ITIMER_PROF, &timer, NULL);
        tot_cntx_switches++;
        start = clock(); // Get the start time
        swapcontext(&sched_cctx, &shortestJob->myTCB->cctx);
        end = clock(); // Get the end time
        cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC;
        if (myQueue->running_thread != NULL){
            myQueue->running_thread->myTCB->elapsed += cpu_time_used;
        }
    }
}





/* FIGURING OUT MLFQ IMPLEMENTATION FROM HERE */

struct Queue* createQueue() {
    struct Queue* queue = (struct Queue*)malloc(sizeof(struct Queue));
    puts("creating mlfq queues");
    if (queue == NULL) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }
    queue->front = NULL;
    queue->rear = NULL;
    return queue;
}

void enqueue(struct Queue* queue, struct QueueNode new_node) 
{
    // Allocate memory for the new node
    puts("enqued_mlfq");
    struct QueueNode* newNode = (struct QueueNode*)malloc(sizeof(struct QueueNode));
    if (newNode == NULL) {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }
    *newNode = new_node;
    newNode->next = NULL;

    // If the queue is empty, set the new node as both front and rear
    if (queue->rear == NULL) {
        queue->front = newNode;
        queue->rear = newNode;
    } else {
        // Otherwise, add the new node to the rear
        queue->rear->next = newNode;
        queue->rear = newNode;
    }
}

struct QueueNode dequeue(struct Queue* queue) {
    // Check if the queue is empty
    puts("dequuing mlfq ");
    if (queue->front == NULL) {
        // You might want to handle this case based on your requirements.
        // For now, return a dummy node with NULL pointer and handle it accordingly in your logic.
        struct QueueNode dummy_node = { .myTCB = NULL, .next = NULL };
        return dummy_node;
    }

    // Get the front node and move front pointer to the next node
    struct QueueNode* temp = queue->front;
    queue->front = queue->front->next;

    // If the queue becomes empty, update the rear pointer to NULL
    if (queue->front == NULL) {
        queue->rear = NULL;
    }

    // Store the dequeued node for further processing if needed
    struct QueueNode dequeued_node = *temp;

    // Free the memory of the dequeued node
    free(temp);

    return dequeued_node;
}

/* Preemptive MLFQ scheduling algorithm */
int time_quantum[LEVELS] = {8, 6, 4, 2}; // testing levels with different time quantums (Level 0 to Level 3)

static void sched_mlfq()
{
    // TODO create an array with priorities and different queues, this isn't a great implementation
    puts("MLFQ beginning...");

   

    /*
        LEVEL 3
        LEVEL 2
        LEVEL 1
        LEVEL 0
    */

    // Create an array of queues for MLFQ
    struct Queue* mlfq_queues[LEVELS];
    for (int i = 0; i < LEVELS; ++i) 
    {
        mlfq_queues[i] = createQueue();
    }


    // put threads from myQueue into MLFQ queues based on their priority levels
    while (!isEmpty(myQueue)) {
        struct QueueNode* currentThread = dequeue_psjf();

        // Determine the MLFQ level based on priority
        int mlfq_level = currentThread->myTCB->thread_p;
        if (mlfq_level >= LEVELS) {
            mlfq_level = LEVELS - 1; // Cap the level to the maximum available
        }
        printf("initial mlfq_level %d \n" , mlfq_level);

        // Enqueue the thread into the appropriate MLFQ queue
        enqueue(mlfq_queues[mlfq_level], *currentThread);
    }

    // DEBUGGING: After enqueueing threads into MLFQ queues
    for (int i = 0; i < LEVELS; ++i) {
        printf("MLFQ Level %d Queue: ", i);
        struct QueueNode *currentNode = mlfq_queues[i]->front;
        while (currentNode != NULL) {
            printf("%d ", currentNode->myTCB->thread_id);
            currentNode = currentNode->next;
        }
        printf("\n");
    }

    // Execute threads in MLFQ queues
    for (int i = 0; i < LEVELS; ++i) {
       
        //!isEmpty(mlfq_queues[i]
        while (!isEmptyMultiQueue(mlfq_queues[i])) {
            printf("not empty\n");
            struct QueueNode currentThread = dequeue(mlfq_queues[i]); // Dequeue the thread

            // Context switch if the thread has exceeded the time quantum
            if (currentThread.myTCB->elapsed >= time_quantum[i]) 
            {
                //move the thread to lower level (unless its already at the lowest)
                if (i < LEVELS - 1) {
                    printf("mlfq: process is going to a lower priority queue");
                    currentThread.myTCB->thread_p = i + 1;
                }
                printf("mlfq: process is being enqueued in new priority queue");
                enqueue(mlfq_queues[currentThread.myTCB->thread_p], currentThread);
            } else {
                // If the thread has not exceeded the time quantum, let it continue executing
                printf("mlfq: process will continue on this level\n");
                currentThread.myTCB->elapsed += 1;
                swapcontext(&sched_cctx, &currentThread.myTCB->cctx);
            }
        }

        printf("finished executing the threads at this level\n");
    }

    /*
    for (int currentLevel = LEVELS - 1; currentLevel >= 0; --currentLevel) {
        
         int current_quantum = time_quantum[currentLevel];
        if (!isEmpty(myQueue) && myQueue->front->myTCB->thread_p == currentLevel)
         {
            struct QueueNode *currentThread = myQueue->running_thread = myQueue->front;

            // Context switch if the thread has exceeded the time quantum
            if (currentThread->myTCB->elapsed >= current_quantum) {
                if (currentLevel == 0) {
                    currentThread->myTCB->thread_p = 0;
                } else {
                    currentThread->myTCB->thread_p = currentLevel - 1;
                }

                remove_node(currentThread);
                enqueue_psjf(currentThread);
            } else {
                // If the thread has not exceeded the time quantum, let it continue executing
                currentThread->myTCB->elapsed += 1;
                swapcontext(&sched_cctx, &currentThread->myTCB->cctx);
            }
        }
    }
    */


    puts("done with mlfq");


}

static void schedule()
{
    #ifndef MLFQ
        sched_psjf();
    #else
        sched_mlfq();
    #endif
}

/*SCHEDULER FUNCTIONS END HERE*/

/* create a new thread */
int worker_create(worker_t *thread, pthread_attr_t *attr, void *(*function)(void *), void *arg)
{

    // - create Thread Control Block (TCB)
    struct TCB *myTCB = (struct TCB *)malloc(sizeof(struct TCB));
    myTCB->thread_id = *thread;
    myTCB->elapsed = 0;

    //set the tcb level to the highest priority level whenever a worker is first created
    //if the process hits a time quantum go to a lower level
    myTCB->thread_p = 0;

    // - allocate space of stack for this thread to run
    myTCB->stack = malloc(STACK_SIZE);
    if (myTCB->stack == NULL)
    {
        perror("Failed to allocate stack");
        exit(1);
    }

    // - create and initialize the context of this worker thread
    myTCB->cctx.uc_link = NULL;
    myTCB->cctx.uc_stack.ss_sp = myTCB->stack;
    myTCB->cctx.uc_stack.ss_size = STACK_SIZE;
    myTCB->cctx.uc_stack.ss_flags = 0;

    if (getcontext(&myTCB->cctx) < 0)
    {
        perror("getcontext");
        exit(1);
    }
    makecontext(&myTCB->cctx, (void *)function, 0);

    // - make it ready for the execution.
    myTCB->thread_status = 0;

    struct QueueNode *qn = (struct QueueNode *)malloc(sizeof(struct QueueNode));
    qn->myTCB = myTCB;
    qn->next = NULL;

    // after everything is set, push this thread into run queue and
    if (first_run)
    {
        first_run = 0;

        /*SETUP FOR THE QUEUE START*/
        myQueue = (struct Queue *)malloc(sizeof(struct Queue));
        if (getcontext(&sched_cctx) < 0)
        {
            perror("getcontext");
            exit(1);
        }

        sched_cctx.uc_link = NULL;
        sched_cctx.uc_stack.ss_sp = malloc(STACK_SIZE);
        sched_cctx.uc_stack.ss_size = STACK_SIZE;
        sched_cctx.uc_stack.ss_flags = 0;
        makecontext(&sched_cctx, (void *)&schedule, 0);
        /*SETUP FOR THE QUEUE END*/

        /*SETUP FOR THE MAIN THREAD START*/
        main_thread = (struct QueueNode *)malloc(sizeof(struct QueueNode));
        struct TCB *mainTCB = (struct TCB *)malloc(sizeof(struct TCB));
        main_thread->myTCB = mainTCB;
        main_thread->next = NULL;
        mainTCB->thread_id = (worker_t)0; // 0 reserved for the main context

        mainTCB->stack = malloc(STACK_SIZE);
        if (mainTCB->stack == NULL)
        {
            perror("Failed to allocate stack");
            exit(1);
        }
        mainTCB->cctx.uc_link = NULL;
        mainTCB->cctx.uc_stack.ss_sp = mainTCB->stack;
        mainTCB->cctx.uc_stack.ss_size = STACK_SIZE;
        mainTCB->cctx.uc_stack.ss_flags = 0;
        /*SETUP FOR THE MAIN THREAD END*/

        /*SETUP THE TIMER START*/
        memset (&sa, 0, sizeof (sa));
        sa.sa_handler = &timer_handler;
        sigaction (SIGPROF, &sa, NULL);
        
        timer.it_interval.tv_usec = 0;
        timer.it_interval.tv_sec = 0;
        timer.it_value.tv_usec = 0;
	    timer.it_value.tv_sec = 1;
        /*SETUP THE TIMER END*/
    }

    enqueue_psjf(qn);
    enqueue_psjf(main_thread);
    swapcontext(&main_thread->myTCB->cctx, &sched_cctx);
    puts("Back in main thread");
    return 0;
};

/* give CPU possession to other user-level worker threads voluntarily */
int worker_yield()
{

    // - change worker thread's state from Running to Ready
    myQueue->running_thread->myTCB->thread_status = 0;
    // - save context of this thread to its thread control block
    // - switch from thread context to scheduler context
    enqueue_psjf(myQueue->running_thread);
    swapcontext(&myQueue->running_thread->myTCB->cctx, &sched_cctx);

    return 0;
};

/* terminate a thread */
void worker_exit(void *value_ptr)
{
    // - de-allocate any dynamic memory created when starting this thread
    struct QueueNode *temp = myQueue->running_thread;
    worker_t thread_id = temp->myTCB->thread_id;

    // free all of temp's allocated memory
    free(temp->myTCB->stack);
    free(temp->myTCB);
    free(temp);
    myQueue->running_thread = NULL;

    // create a new finished node and enqueue
    struct FinishedNode *finished_node = (struct FinishedNode *)malloc(sizeof(struct FinishedNode));
    finished_node->thread_id = thread_id;
    printf("Deallocated thread %d\n", thread_id);
    enqueue_finished(finished_node);
    setcontext(&sched_cctx);
};

/* Wait for thread termination */
int worker_join(worker_t thread, void **value_ptr)
{

    // retrieve the chosen thread based on its ID
    struct FinishedNode *target_node = find_finished_node_by_thread_id(thread);

    if (target_node == NULL)
    {
        while(target_node == NULL){
            target_node = find_finished_node_by_thread_id(thread);
            // printf("Waiting for a thread %d\n", thread);
        }
    }
    else
    {   
        remove_finished_node_from_queue(target_node);
        free(target_node);
        puts("Done with this job");
    }

    return 0;
};

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t *mutexattr)
{
    //- initialize data structures for this mutex
    mutexQueue = (struct MutexQueue *)malloc(sizeof(struct MutexQueue));
    mutexQueue->front = NULL;
    mutexQueue->rear = NULL;
    // YOUR CODE HERE
    return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex)
{
    // - use the built-in test-and-set atomic function to test the mutex
    while(__sync_lock_test_and_set(&mutex->is_locked, 1)){
        enqueue_mutex(myQueue->running_thread);
        swapcontext(&myQueue->running_thread->myTCB->cctx, &sched_cctx);
    }
    // - if the mutex is acquired successfully, enter the critical section
    // - if acquiring mutex fails, push current thread into block list and
    // context switch to the scheduler thread

    // YOUR CODE HERE
    return 0;
};

/* release the mutex lock */
int worker_mutex_unlock(worker_mutex_t *mutex)
{
    // - release mutex and make it available again.
    // - put threads in block list to run queue
    // so that they could compete for mutex later.
    __sync_lock_release(&mutex->is_locked);
    struct QueueNode *nextMutexJob = dequeue_mutex();
    if(nextMutexJob == NULL){
        return 0;
    } else{
        enqueue_psjf(nextMutexJob);
    }
    // YOUR CODE HERE
    return 0;
};

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex)
{
    // - de-allocate dynamic memory created in worker_mutex_init
    free(mutexQueue);
    return 0;
};

// DO NOT MODIFY THIS FUNCTION
/* Function to print global statistics. Do not modify this function.*/
void print_app_stats(void)
{

    fprintf(stderr, "Total context switches %ld \n", tot_cntx_switches);
    fprintf(stderr, "Average turnaround time %lf \n", avg_turn_time);
    fprintf(stderr, "Average response time  %lf \n", avg_resp_time);
}

// Feel free to add any other functions you need

// YOUR CODE HERE
