#include "kernel/types.h"
#include "user/user.h"
#include "user/list.h"
#include "user/threads.h"
#include "user/threads_sched.h"
#include <limits.h>
#define NULL 0
#define true 1
#define false 0
#define stdin 0
#define stdout 1
#define stderr 2
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define P_RR_MAX_PRIORITY 4

// #define THREAD_SCHEDULER_DEFAULT
// #define THREAD_SCHEDULER_HRRN
// #define THREAD_SCHEDULER_PRIORITY_RR
// #define THREAD_SCHEDULER_DM
// #define THREAD_SCHEDULER_EDF_CBS

#define min(a, b) (((a) < (b))? (a): (b))

typedef int bool;

#ifdef THREAD_SCHEDULER_PRIORITY_RR
struct priority_queue_entry {
    struct thread *thread;
    struct list_head thread_list;
};
#endif

/* Iterators */
struct thread *th = NULL;
struct release_queue_entry *entry = NULL;

/* Args (global) */
int current_time = 0;
int time_quantum = 0;
struct list_head *run_queue = NULL;
struct list_head *release_queue = NULL;

/* Helper functions */
static inline void ERR_EXIT(const char *msg) {
    fprintf(stderr, "%s\n", msg);
    exit(EXIT_FAILURE);
}

void set_global_args(struct threads_sched_args args) {
    current_time = args.current_time;
    time_quantum = args.time_quantum;
    run_queue = args.run_queue;
    release_queue = args.release_queue;
}

int get_waiting_time(struct thread *t, int current_time) {
    return current_time - t->arrival_time;
}

int get_sleeping_time() {
    if (list_empty(release_queue))
        ERR_EXIT("Release queue is empty\n");
    int min_release_time = INT_MAX;
    list_for_each_entry(entry, release_queue, thread_list)
        min_release_time = min(min_release_time, entry->release_time);
    return min_release_time - current_time;
}
 
/* Scheduling algorithm */

// Default
#ifdef THREAD_SCHEDULER_DEFAULT
struct threads_sched_result schedule_default(struct threads_sched_args args) {
    set_global_args(args);
    struct thread *thread_with_smallest_id = NULL;
    list_for_each_entry(th, run_queue, thread_list) {
        if (thread_with_smallest_id == NULL || th->ID < thread_with_smallest_id->ID)
            thread_with_smallest_id = th;
    }

    struct threads_sched_result r;
    if (thread_with_smallest_id != NULL) {
        r.scheduled_thread_list_member = &thread_with_smallest_id->thread_list;
        r.allocated_time = thread_with_smallest_id->remaining_time;
    }
    else {
        r.scheduled_thread_list_member = run_queue;
        r.allocated_time = 1;
    }
    return r;
}
#endif

/* MP3 Part 1 - Non-Real-Time Scheduling */

// HRRN
#ifdef THREAD_SCHEDULER_HRRN
static int __hrrn_thread_cmp(struct thread *a, struct thread *b) {
    if (a == NULL || b == NULL)
        ERR_EXIT("[HRRN] Compare NULL threads\n");

    int a_priority = (get_waiting_time(a, current_time) + a->processing_time) * b->processing_time;
    int b_priority = (get_waiting_time(b, current_time) + b->processing_time) * a->processing_time;
    if (a_priority < b_priority || (a_priority == b_priority && a->ID > b->ID))
        return -1;
    else if (a_priority == b_priority && a->ID == b->ID)
        return 0;
    else // a_priority > b_priority || (a_priority == b_priority && a->ID < b->ID)
        return 1;
}

struct threads_sched_result schedule_hrrn(struct threads_sched_args args) {
    set_global_args(args);
    struct thread *thread_with_hrr = NULL;
    list_for_each_entry(th, run_queue, thread_list) {
        if (thread_with_hrr == NULL || __hrrn_thread_cmp(th, thread_with_hrr) == 1)
            thread_with_hrr = th;
    }

    struct threads_sched_result r;
    if (thread_with_hrr != NULL) {
        r.scheduled_thread_list_member = &thread_with_hrr->thread_list;
        r.allocated_time = thread_with_hrr->remaining_time;
    }
    else {
        r.scheduled_thread_list_member = run_queue;
        r.allocated_time = get_sleeping_time();
    }
    return r;
}
#endif

// Priority Round-Robin(P-RR)
#ifdef THREAD_SCHEDULER_PRIORITY_RR
void __rr_priority_queue_add(struct list_head *priority_queue, struct thread *t) {
    struct priority_queue_entry *new_entry = (struct priority_queue_entry *)malloc(sizeof(struct priority_queue_entry));
    new_entry->thread = t;
    list_add_tail(&new_entry->thread_list, priority_queue);
}

struct threads_sched_result schedule_priority_rr(struct threads_sched_args args) {
    set_global_args(args);
    struct list_head priority_queue[P_RR_MAX_PRIORITY + 1];
    for (int i = 0; i <= P_RR_MAX_PRIORITY; i++)
        INIT_LIST_HEAD(&priority_queue[i]);
    
    list_for_each_entry(th, run_queue, thread_list) {
        if (th->priority < 0 || th->priority > P_RR_MAX_PRIORITY)
            ERR_EXIT("[P-RR] Thread priority is out of range\n");
        __rr_priority_queue_add(&priority_queue[th->priority], th);
    }

    struct threads_sched_result r;
    for (int i = 0; i <= P_RR_MAX_PRIORITY; i++) {
        if (list_empty(&priority_queue[i]))
            continue;
        struct priority_queue_entry *entry = list_entry(priority_queue[i].next, struct priority_queue_entry, thread_list);
        list_del(&entry->thread_list);
        r.scheduled_thread_list_member = &entry->thread->thread_list;
        if (list_empty(&priority_queue[i]) || entry->thread->remaining_time <= time_quantum) {
            r.allocated_time = entry->thread->remaining_time;
            free(entry);
        }
        else {
            r.allocated_time = time_quantum;
            list_add_tail(&entry->thread_list, &priority_queue[i]);
        }
        return r;
    }

    /* No thread in the run_queue */
    r.scheduled_thread_list_member = run_queue;
    r.allocated_time = 1;
    return r;
}
#endif

/* MP3 Part 2 - Real-Time Scheduling*/

#if defined(THREAD_SCHEDULER_EDF_CBS) || defined(THREAD_SCHEDULER_DM)
static struct thread *__check_deadline_miss() {
    struct thread *thread_missing_deadline = NULL;
    list_for_each_entry(th, run_queue, thread_list) {
        if (th->current_deadline <= current_time && (thread_missing_deadline == NULL || th->ID < thread_missing_deadline->ID))
                thread_missing_deadline = th;
    }
    return thread_missing_deadline;
}
#endif

// Deadline-Monotonic Scheduling
#ifdef THREAD_SCHEDULER_DM
static int __dm_thread_cmp(struct thread *a, struct thread *b) {
    if (a == NULL || b == NULL)
        ERR_EXIT("[DM] Compare NULL threads\n");
    if (a->period > b->period || (a->period == b->period && a->ID > b->ID))
        return -1;
    else if (a->period == b->period && a->ID == b->ID)
        return 0;
    else // a->period < b->period || (a->period == b->period && a->ID < b->ID)
        return 1;
}

int __dm_compute_time_to_be_allocated(struct thread *t) {
    int time_to_be_allocated = min(t->remaining_time, t->current_deadline - current_time);
    list_for_each_entry(entry, release_queue, thread_list) {
        if (__dm_thread_cmp(entry->thrd, t) == 1)
            time_to_be_allocated = min(time_to_be_allocated, entry->release_time - current_time);
    }
    return time_to_be_allocated;
}

struct threads_sched_result schedule_dm(struct threads_sched_args args) {
    set_global_args(args);
    struct threads_sched_result r;
    struct thread *thread_missing_deadline = __check_deadline_miss();
    if (thread_missing_deadline != NULL) { // first check if there is any thread has missed its current deadline
        list_del(&thread_missing_deadline->thread_list);
        r.scheduled_thread_list_member = &thread_missing_deadline->thread_list;
        r.allocated_time = 0;
    }
    else {
        struct thread *thread_with_shortest_deadline = NULL;
        list_for_each_entry(th, run_queue, thread_list) {
            if (thread_with_shortest_deadline == NULL || __dm_thread_cmp(th, thread_with_shortest_deadline) == 1)
                thread_with_shortest_deadline = th;
        }

        if (thread_with_shortest_deadline != NULL) {
            r.scheduled_thread_list_member = &thread_with_shortest_deadline->thread_list;
            r.allocated_time = __dm_compute_time_to_be_allocated(thread_with_shortest_deadline);
        }
        else { // handle the case where run queue is empty
            r.scheduled_thread_list_member = run_queue;
            r.allocated_time = get_sleeping_time();
            if (r.allocated_time < 0)
                ERR_EXIT("[DM] Negative sleeping time\n");
        }
    }
    return r;
}
#endif

// EDF with CBS comparation
#ifdef THREAD_SCHEDULER_EDF_CBS
static int __edf_thread_cmp(struct thread *a, struct thread *b) {
    if (a == NULL || b == NULL)
        ERR_EXIT("[EDF] Compare NULL threads\n");
    if (a->current_deadline > b->current_deadline || (a->current_deadline == b->current_deadline && a->ID > b->ID))
        return -1;
    else if (a->current_deadline == b->current_deadline && a->ID == b->ID)
        return 0;
    else // a->current_deadline < b->current_deadline || (a->current_deadline == b->current_deadline && a->ID < b->ID)
        return 1;
}

static bool __edf_violate_bandwidth(struct thread *t) {
    if (t == NULL)
        ERR_EXIT("[EDF] Check NULL thread\n");
    return t->cbs.remaining_budget * t->period > t->cbs.budget * (t->current_deadline - current_time);
}

int __edf_compute_time_to_be_allocated(struct thread *t) {
    int time_to_be_allocated = min(min(t->cbs.remaining_budget, t->remaining_time), t->current_deadline - current_time);
    list_for_each_entry(th, run_queue, thread_list) {
        if (th->cbs.is_throttled && __edf_thread_cmp(th, t) == 1)
            time_to_be_allocated = min(time_to_be_allocated, th->current_deadline - current_time);
    }
    list_for_each_entry(entry, release_queue, thread_list) {
        if (__edf_thread_cmp(entry->thrd, t) == 1)
            time_to_be_allocated = min(time_to_be_allocated, entry->release_time - current_time);
    }
    return time_to_be_allocated;
}

void __edf_select_thread_and_store_result(struct threads_sched_result *r) {
    struct thread *thread_with_ed = NULL;
    list_for_each_entry(th, run_queue, thread_list) {
        if (!th->cbs.is_throttled && (thread_with_ed == NULL || __edf_thread_cmp(th, thread_with_ed) == 1))
            thread_with_ed = th;
    }

    if (thread_with_ed != NULL) {
        if (!thread_with_ed->cbs.is_hard_rt && __edf_violate_bandwidth(thread_with_ed)) {
            thread_with_ed->current_deadline = current_time + thread_with_ed->period;
            thread_with_ed->cbs.remaining_budget = thread_with_ed->cbs.budget;
            __edf_select_thread_and_store_result(r);
        }
        else {
            r->scheduled_thread_list_member = &thread_with_ed->thread_list;
            r->allocated_time = __edf_compute_time_to_be_allocated(thread_with_ed);
            if (r->allocated_time < 0)
                ERR_EXIT("[EDF] Negative allocated time\n");
        }
    }
    else { // handle the case where run queue is empty
        r->scheduled_thread_list_member = run_queue;
        r->allocated_time = get_sleeping_time();
        if (r->allocated_time < 0)
            ERR_EXIT("[EDF] Negative sleeping time\n");
    }
}

//  EDF_CBS scheduler
struct threads_sched_result schedule_edf_cbs(struct threads_sched_args args) {
    set_global_args(args);
    struct threads_sched_result r;
    // notify the throttle task
    list_for_each_entry(th, run_queue, thread_list) {
        if (th->cbs.remaining_budget <= 0 && th->remaining_time > 0)
            th->cbs.is_throttled = true;
    }

    // first check if there is any thread has missed its current deadline
    struct thread *thread_missing_deadline = NULL;
    while ((thread_missing_deadline = __check_deadline_miss()) != NULL) {
        if (thread_missing_deadline->cbs.is_hard_rt) {
            list_del(&thread_missing_deadline->thread_list);
            r.scheduled_thread_list_member = &thread_missing_deadline->thread_list;
            r.allocated_time = 0;
            return r;
        }
        else {
            thread_missing_deadline->current_deadline += thread_missing_deadline->period;
            thread_missing_deadline->cbs.remaining_budget = thread_missing_deadline->cbs.budget;
            thread_missing_deadline->cbs.is_throttled = false;
        }
    }

    __edf_select_thread_and_store_result(&r);
    return r;
}
#endif