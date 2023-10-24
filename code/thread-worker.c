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

// Function to check if the queue is empty
int isEmpty()
{
    return (myQueue->front == NULL);
}

// Function to add an element to the rear of the queue
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
            }
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

// Function to add an element to the rear of the queue
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
struct QueueNode *dequeue()
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
    enqueue_psjf(myQueue->running_thread);
    swapcontext(&myQueue->running_thread->myTCB->cctx, &sched_cctx);
}
/*TIMER FUNCTIONS END HERE*/

/*SCHEDULER FUNCTIONS START HERE*/

/* Pre-emptive Shortest Job First (POLICY_PSJF) scheduling algorithm */
static void sched_psjf()
{
    // process having the smallest executing time is chosen for next execution
    while (!isEmpty(myQueue))
    {
        struct QueueNode *shortestJob = myQueue->running_thread = dequeue();
        setitimer(ITIMER_PROF, &timer, NULL);
        setcontext(&shortestJob->myTCB->cctx);
    }
}

/* Preemptive MLFQ scheduling algorithm */
static void sched_mlfq()
{
    puts("To be implemented");
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
    // myQueue->running_thread->myTCB->thread_status = 0;
    // - save context of this thread to its thread control block
    // - switch from thread context to scheduler context

    // YOUR CODE HERE

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
        // - wait for a specific thread to terminate
    }
    else
    {
        // - de-allocate any dynamic memory created by the joining thread
    }

    // YOUR CODE HERE
    return 0;
};

/* initialize the mutex lock */
int worker_mutex_init(worker_mutex_t *mutex, const pthread_mutexattr_t *mutexattr)
{
    //- initialize data structures for this mutex

    // YOUR CODE HERE
    return 0;
};

/* aquire the mutex lock */
int worker_mutex_lock(worker_mutex_t *mutex)
{

    // - use the built-in test-and-set atomic function to test the mutex
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

    // YOUR CODE HERE
    return 0;
};

/* destroy the mutex */
int worker_mutex_destroy(worker_mutex_t *mutex)
{
    // - de-allocate dynamic memory created in worker_mutex_init

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
