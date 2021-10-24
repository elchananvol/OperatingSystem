#include "uthreads.h"
#include <csetjmp>
#include <csignal>
#include <sys/time.h>
#include <vector>
#include <stdlib.h>
#include <queue>
#include <setjmp.h>
#include <signal.h>
#include <iostream>
#include <set>

#define FAILURE -1
#define SUCCESS 0
using namespace std;

class Thread {
public:
    int id;
    sigjmp_buf env;
    char *stack_ptr;
    void (*func)(void);
    int quantum_num;
    bool is_blocked;
    bool is_mutex_locked;

};

sigset_t time_block;
struct itimerval quantum_timer;
int curr_running;
int mutex_possessor;
int first_available_place;
int active_threads_num;
bool is_first;
int quantum_counter;
priority_queue<int, vector<int>, greater<int> > free_queue;
queue<Thread **> ready_list;
queue<Thread **> mutex_list;
struct sigaction time_alrm;
set<int> mutex_seekers;

Thread *threads_array[MAX_THREAD_NUM] = {nullptr};
Thread main_thread;

#ifdef __x86_64__
/* code for 64 bit Intel arch */

typedef unsigned long address_t;
#define JB_SP 6
#define JB_PC 7

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
/*
 * Description: This function translates an
 * address for it to be readable for the code
*/
address_t translate_address(address_t addr) {
    address_t ret;
    asm volatile("xor    %%fs:0x30,%0\n"
                 "rol    $0x11,%0\n"
    : "=g" (ret)
    : "0" (addr));
    return ret;
}

#else
/* code for 32 bit Intel arch */

typedef unsigned int address_t;
#define JB_SP 4
#define JB_PC 5

/* A translation is required when using an address of a variable.
   Use this as a black box in your code. */
/*
 * Description: This function translates an
 * address for it to be readable for the code
*/
address_t translate_address(address_t addr)
{
    address_t ret;
    asm volatile("xor    %%gs:0x18,%0\n"
        "rol    $0x9,%0\n"
                 : "=g" (ret)
                 : "0" (addr));
    return ret;
}

#endif
/*
 * Description: This function removes a given thread from the ready queue
*/
void RemoveFromQueue(int tid) {
    int length = ready_list.size();
    for (int i = 0; i < length; i++) {
        if (((*(ready_list.front()))->id) != tid) {
            ready_list.push(ready_list.front());
        }
        ready_list.pop();
    }
}

/*
 * Description: This function tries to obtain the mutex if it tried already and failed
*/
void SeekMutex(int tid){
    sigprocmask(SIG_BLOCK, &time_block, nullptr);
    if (mutex_seekers.find(tid) != mutex_seekers.end()){
        mutex_seekers.erase(tid);
        uthread_mutex_lock();
    }
    sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
}

/*
 * Description: This function is the normal scheduler obeying the Robin algorithm
*/
void timer_handler(int signum) {
    sigprocmask(SIG_BLOCK, &time_block, nullptr);
    if (sigsetjmp(threads_array[curr_running]->env, 1)) {
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        return;
    }
    ready_list.pop();
    ready_list.push(&(threads_array[curr_running]));
    int new_thread_id = (*(ready_list.front()))->id;
    curr_running = new_thread_id;
    threads_array[curr_running]->quantum_num += 1;
    quantum_counter += 1;
    if (setitimer(ITIMER_VIRTUAL, &quantum_timer, nullptr) == FAILURE){
        cerr << "system error: Failed to start timer" << endl;
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        exit(1);
    }
    sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
    SeekMutex(new_thread_id);
    siglongjmp(threads_array[new_thread_id]->env, 1);
}

/*
 * Description: This function is the scheduler obeying the Robin algorithm
 * In case it happened that a thread terminated itself
*/
void timer_handler_after_selfterminate() {
    sigprocmask(SIG_BLOCK, &time_block, nullptr);
    int new_thread_id = (*(ready_list.front()))->id;
    curr_running = new_thread_id;
    threads_array[curr_running]->quantum_num += 1;
    quantum_counter += 1;
    if (setitimer(ITIMER_VIRTUAL, &quantum_timer, nullptr) == FAILURE){
        cerr << "system error: Failed to start timer" << endl;
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        exit(1);
    }
    sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
    SeekMutex(new_thread_id);
    siglongjmp(threads_array[new_thread_id]->env, 1);
}

/*
 * Description: This function is the scheduler obeying the Robin algorithm
 * In case it happened that a thread blocked itself
*/
void timer_handler_after_selfblock() {
    sigprocmask(SIG_BLOCK, &time_block, nullptr);
    if (sigsetjmp(threads_array[curr_running]->env, 1)) {
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        return;
    }
    int new_thread_id = (*(ready_list.front()))->id;
    curr_running = new_thread_id;
    threads_array[curr_running]->quantum_num += 1;
    quantum_counter += 1;
    if (setitimer(ITIMER_VIRTUAL, &quantum_timer, nullptr) == FAILURE){
        cerr << "system error: Failed to start timer" << endl;
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        exit(1);
    }
    sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
    SeekMutex(new_thread_id);
    siglongjmp(threads_array[new_thread_id]->env, 1);
}

/*
 * Description: This function initializes the thread library.
 * You may assume that this function is called before any other thread library
 * function, and that it is called exactly once. The input to the function is
 * the length of a quantum in micro-seconds. It is an error to call this
 * function with non-positive quantum_usecs.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_init(int quantum_usecs) {
    if (sigemptyset(&time_block) == FAILURE){
        cerr << "system error: Failed to allocate empty set" << endl;
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        exit(1);
    }
    if (sigaddset(&time_block, SIGVTALRM) == FAILURE){
        cerr << "system error: Failed to add a signal to a set" << endl;
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        exit(1);
    }
    sigprocmask(SIG_BLOCK, &time_block, nullptr);
    if (quantum_usecs <= 0) {
        cerr << "thread library error: Negative quantum number cannot be initialized" << endl;
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        return FAILURE;
    }
    main_thread.stack_ptr = (char *) malloc(STACK_SIZE);
    if (!main_thread.stack_ptr) {
        cerr << "system error: Failed to allocate stack memory" << endl;
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        exit(1);
    }
    if (sigaddset(&time_alrm.sa_mask, SIGVTALRM) == FAILURE){
        cerr << "system error: Failed to add a signal to a set" << endl;
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        exit(1);
    }
    time_alrm.sa_handler = &timer_handler;

    main_thread.id = 0;
    main_thread.is_blocked = false;
    main_thread.func = nullptr;
    main_thread.quantum_num = 1;
    quantum_timer.it_value.tv_usec = quantum_usecs;
    quantum_timer.it_value.tv_sec = 0;
    quantum_timer.it_interval.tv_usec = quantum_usecs;
    quantum_timer.it_interval.tv_sec = 0;
    first_available_place = 1;
    active_threads_num = 1;
    quantum_counter = 1;
    curr_running = 0;
    mutex_possessor = -1;
    main_thread.is_mutex_locked = false;
    if (sigemptyset(&main_thread.env->__saved_mask) == FAILURE){
        cerr << "system error: Failed to allocate empty set" << endl;
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        exit(1);
    }
    threads_array[0] = &main_thread;
    ready_list.push(&threads_array[0]);
    is_first = true;
    sigaction(SIGVTALRM, &time_alrm, nullptr);
    if (setitimer(ITIMER_VIRTUAL, &quantum_timer, nullptr) == FAILURE){
        cerr << "system error: Failed to start timer" << endl;
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        exit(1);
    }
    sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
    return SUCCESS;
}

/*
 * Description: This function creates a new thread, whose entry point is the
 * function f with the signature void f(void). The thread is added to the end
 * of the READY threads list. The uthread_spawn function should fail if it
 * would cause the number of concurrent threads to exceed the limit
 * (MAX_THREAD_NUM). Each thread should be allocated with a stack of size
 * STACK_SIZE bytes.
 * Return value: On success, return the ID of the created thread.
 * On failure, return -1.
*/
int uthread_spawn(void (*f)(void)) {
    sigprocmask(SIG_BLOCK, &time_block, nullptr);
    if ((!f)) {
        cerr << "thread library error: Cannot provide NULL argument as a function for a thread." << endl;
        fflush(stderr);
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        return FAILURE;
    }

    if ((active_threads_num >= MAX_THREAD_NUM)) {
        cerr << "thread library error: Too many threads are currently running, Thus cannot allocate further thread."
             << endl;
        fflush(stderr);
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        return FAILURE;
    }


    Thread *curr_thread = (Thread *) malloc(sizeof(Thread));
    if (!curr_thread) {
        cerr << "system error: Failed to allocate the Thread" << endl;
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        exit(1);
    }


    curr_thread->stack_ptr = (char *) malloc(STACK_SIZE * sizeof(char));
    if (!main_thread.stack_ptr) {
        cerr << "system error: Failed to allocate stack memory" << endl;
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        exit(1);
    }


    if (free_queue.empty()) {
        curr_thread->id = first_available_place;
        first_available_place++;
    } else {
        curr_thread->id = free_queue.top();
        free_queue.pop();
    }

    curr_thread->func = f;
    curr_thread->quantum_num = 0;
    threads_array[curr_thread->id] = curr_thread;
    active_threads_num++;
    address_t sp, pc;
    sp = (address_t) curr_thread->stack_ptr + STACK_SIZE - sizeof(address_t);
    pc = (address_t) f;
    sigsetjmp(curr_thread->env, 1);
    (curr_thread->env->__jmpbuf)[JB_SP] = translate_address(sp);
    (curr_thread->env->__jmpbuf)[JB_PC] = translate_address(pc);
    curr_thread->is_blocked = false;
    curr_thread->is_mutex_locked = false;
    ready_list.push(&threads_array[curr_thread->id]);
    if (sigemptyset(&curr_thread->env->__saved_mask) == FAILURE){
        cerr << "system error: Failed to allocate empty set" << endl;
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        exit(1);
    };
    if (is_first) {
        is_first = false;
    }
    sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
    return curr_thread->id;
}


/*
 * Description: This function terminates the thread with ID tid and deletes
 * it from all relevant control structures. All the resources allocated by
 * the library for this thread should be released. If no thread with ID tid
 * exists it is considered an error. Terminating the main thread
 * (tid == 0) will result in the termination of the entire process using
 * exit(0) [after releasing the assigned library memory].
 * Return value: The function returns 0 if the thread was successfully
 * terminated and -1 otherwise. If a thread terminates itself or the main
 * thread is terminated, the function does not return.
*/
int uthread_terminate(int tid) {
    sigprocmask(SIG_BLOCK, &time_block, nullptr);
    if ((tid == 0) || (tid >= MAX_THREAD_NUM) || (tid < 0) || (!threads_array[tid])) {
        for (int i = 1; i < MAX_THREAD_NUM; i++) {
            if (threads_array[i]) {
                free(threads_array[i]->stack_ptr);
                free(threads_array[i]);
                free_queue.push(tid);
                active_threads_num--;
            }
        }
        free(main_thread.stack_ptr);
        if ((tid >= MAX_THREAD_NUM) || (tid < 0) || (!threads_array[tid])) {
            cerr << "thread library error: Provided an illegal argument of thread id(Negative/Too large/Inexistant) !"
                 << endl;
            fflush(stderr);
            sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
            return FAILURE;
        }
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        exit(0);
    }
    RemoveFromQueue(tid);
    Thread *curr_thread = threads_array[tid];
    if (curr_thread->is_mutex_locked){
        mutex_possessor = -1;
        if (!mutex_list.empty()) {
            int mutex_seeker = (*mutex_list.front())->id;
            mutex_list.pop();
            mutex_seekers.insert(mutex_seeker);
            if (!threads_array[mutex_seeker]->is_blocked) {
                ready_list.push(&threads_array[mutex_seeker]);
            }
        }
    }
    free(curr_thread->stack_ptr);
    free(curr_thread);
    if (tid == (first_available_place - 1)) {
        first_available_place--;
    } else {
        free_queue.push(tid);
    }
    active_threads_num--;
    threads_array[tid] = nullptr;
    if (curr_running == tid){
        timer_handler_after_selfterminate();
    }
    sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
    return SUCCESS;
}


/*
 * Description: This function blocks the thread with ID tid. The thread may
 * be resumed later using uthread_resume. If no thread with ID tid exists it
 * is considered as an error. In addition, it is an error to try blocking the
 * main thread (tid == 0). If a thread blocks itself, a scheduling decision
 * should be made. Blocking a thread in BLOCKED state has no
 * effect and is not considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_block(int tid) {
    sigprocmask(SIG_BLOCK, &time_block, nullptr);
    if ((tid <= 0) || (tid >= MAX_THREAD_NUM) || (!threads_array[tid])) {
        cerr << "thread library error: Provided an illegal argument of thread id (Negative/Too large/Inexistant)!"
             << endl;
        fflush(stderr);
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        return FAILURE;
    }
    if (threads_array[tid]->is_blocked) {
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        return SUCCESS;
    }
    threads_array[tid]->is_blocked = true;
    RemoveFromQueue(tid);
    if (tid == curr_running) {
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        timer_handler_after_selfblock();
        return SUCCESS;
    }
    sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
    return SUCCESS;
}


/*
 * Description: This function resumes a blocked thread with ID tid and moves
 * it to the READY state if it's not synced. Resuming a thread in a RUNNING or READY state
 * has no effect and is not considered as an error. If no thread with
 * ID tid exists it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_resume(int tid) {
    sigprocmask(SIG_BLOCK, &time_block, nullptr);
    if ((tid < 0) || (tid >= MAX_THREAD_NUM) || (!threads_array[tid])) {
        cerr << "thread library error: Provided an illegal argument of thread id(Negative/Too large/Inexistant) !"
             << endl;
        fflush(stderr);
        sigprocmask(SIG_UNBLOCK, &time_block, NULL);
        return FAILURE;
    }
    if (!threads_array[tid]->is_blocked) {
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        return SUCCESS;
    }
    threads_array[tid]->is_blocked = false;
    ready_list.push(&threads_array[tid]);
    sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
    return SUCCESS;
}


/*
 * Description: This function tries to acquire a mutex.
 * If the mutex is unlocked, it locks it and returns.
 * If the mutex is already locked by different thread, the thread moves to BLOCK state.
 * In the future when this thread will be back to RUNNING state,
 * it will try again to acquire the mutex.
 * If the mutex is already locked by this thread, it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_mutex_lock() {
    sigprocmask(SIG_BLOCK, &time_block, nullptr);
    if (threads_array[curr_running]->is_mutex_locked) {
        cerr << "thread library error: Mutex is already Locked!" << endl;
        fflush(stderr);
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        return FAILURE;
    }
    if ((mutex_possessor == -1) && (mutex_list.empty())) {
        mutex_possessor = curr_running;
        threads_array[curr_running]->is_mutex_locked = true;
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);

        return SUCCESS;
    } else if (mutex_possessor != -1) {
        int tid = curr_running;
        threads_array[tid]->is_mutex_locked = true;
        mutex_list.push(&threads_array[tid]);
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
        RemoveFromQueue(tid);
        timer_handler_after_selfblock();
        sigprocmask(SIG_BLOCK, &time_block, nullptr);
    }
    sigprocmask(SIG_UNBLOCK, &time_block, nullptr);
    return SUCCESS;
}


/*
 * Description: This function releases a mutex.
 * If there are blocked threads waiting for this mutex,
 * one of them (no matter which one) moves to READY state.
 * If the mutex is already unlocked, it is considered an error.
 * Return value: On success, return 0. On failure, return -1.
*/
int uthread_mutex_unlock() {
    sigprocmask(SIG_BLOCK, &time_block, nullptr);

    if (!(threads_array[curr_running]->is_mutex_locked)) {
        cerr << "thread library error: This mutex is not Locked thus cannot be unlocked!" << endl;
        fflush(stderr);
        sigprocmask(SIG_UNBLOCK, &time_block, nullptr);

        return FAILURE;
    }
    mutex_possessor = -1;
    if (!mutex_list.empty()) {
        int mutex_seeker = (*mutex_list.front())->id;
        mutex_list.pop();
        mutex_seekers.insert(mutex_seeker);
        if (!threads_array[mutex_seeker]->is_blocked) {
            ready_list.push(&threads_array[mutex_seeker]);
        }
    }
    threads_array[curr_running]->is_mutex_locked = false;
    sigprocmask(SIG_UNBLOCK, &time_block, nullptr);

    return SUCCESS;
}


/*
 * Description: This function returns the thread ID of the calling thread.
 * Return value: The ID of the calling thread.
*/
int uthread_get_tid() {
    return curr_running;
}


/*
 * Description: This function returns the total number of quantums since
 * the library was initialized, including the current quantum.
 * Right after the call to uthread_init, the value should be 1.
 * Each time a new quantum starts, regardless of the reason, this number
 * should be increased by 1.
 * Return value: The total number of quantums.
*/
int uthread_get_total_quantums() {
    return quantum_counter;
}


/*
 * Description: This function returns the number of quantums the thread with
 * ID tid was in RUNNING state. On the first time a thread runs, the function
 * should return 1. Every additional quantum that the thread starts should
 * increase this value by 1 (so if the thread with ID tid is in RUNNING state
 * when this function is called, include also the current quantum). If no
 * thread with ID tid exists it is considered an error.
 * Return value: On success, return the number of quantums of the thread with ID tid.
 * 			     On failure, return -1.
*/
int uthread_get_quantums(int tid) {
    sigprocmask(SIG_BLOCK, &time_block, nullptr);

    if ((tid < 0) || (tid >= MAX_THREAD_NUM) || (!threads_array[tid])) {
        cerr << "thread library error: Provided an illegal argument of thread id(Negative/Too large/Inexistant) !"
             << endl;
        fflush(stderr);
        return FAILURE;
    }
    sigprocmask(SIG_UNBLOCK, &time_block, nullptr);

    return threads_array[tid]->quantum_num;
}


