#include "npu.h"


/*
 * scheduler_init - Address: 0xcb3c
 * 
 * Scheduler state structure initialization function.
 */
void scheduler_init() {
    /* Sets the priority group indices to 0 */
    g_scheduler_state.prio_grp0 = 0;
    for (int i = 0; i < 4; i++)
        g_scheduler_state.prio_grp1[i] = 0;
    for (int i = 0; i < 4; i++)
        for (int j = 0; i < 8; i++)
            g_scheduler_state.prio_grp2[i][j] = 0;

    /* Sets the currently scheduled task to NULL */
    g_current_task = 0;
    
    /*
     * Sets the number of tasks
     * (and other unknown values, but these are not too important afaict)
     */
    g_scheduler_state.nb_tasks = 0;
    g_scheduler_state.count_sched_slices = 1;
    g_scheduler_state._unk_840 = 0;

    /* Sets the scheduler state as stopped */
    g_scheduler_state.scheduler_stopped = 1;
    g_scheduler_state.forbid_scheduling = 1;

    /* Initializes delayed, tasks and ready lists */
    list_init(&g_scheduler_state.delayed_list);
    list_init(&g_scheduler_state.tasks_list);
    for (int prio = 0; prio < TASK_MAX_PRIORITY; prio++)
        list_init(&g_scheduler_state.ready_list[prio]);
}


/*
 * schedule_start - Address: 0xd118
 * 
 * Starts the scheduler and schedule the first task, which is the task
 * with the highest priority in the ready list.
 */
int schedule_start()
{
    /* Disables interrupts */
    __disable_irq();

    /* Retrieves the highest priority to schedule next */
    u32 prio_grp0_idx = g_scheduler_state.prio_grp0;
    u32 prio_grp0_val = g_priority_table[prio_grp0_idx];
    u32 prio_grp1_idx = g_scheduler_state.prio_grp1[prio_grp0_val];
    u32 prio_grp1_val = g_priority_table[prio_grp1_idx];
    u32 prio_grp2_idx = g_scheduler_state.prio_grp2[prio_grp0_val][prio_grp1_val];
    u32 prio_grp2_val = g_priority_table[prio_grp2_idx];
    u32 priority = \
        (prio_grp0_val << 6) | \
        (prio_grp1_val << 3) | \
        prio_grp0_val;

    /* Gets the task with the highest priority from the ready list */
    struct list_head *ready_list = &g_scheduler_state.ready_list[priority];
    g_current_task = container_of(
        ready_list->next, struct task, ready_list_entry);

    /* Sanity checks on the task */
    if (g_current_task->magic == TASK_MAGIC) {
        g_task_to_schedule = g_current_task;

        /* Sets the current task state as running */
        g_current_task->state |= TASK_RUNNING;

        /* Sets the scheduler state as started */
        g_scheduler_state.scheduler_stopped = 0;
        g_scheduler_state.forbid_scheduling = 0;

        /*
         * Loads the stack pointer from the task structure and retrieves the
         * values of the registers from it.
         * The last two values loaded (into r1 and r0) are the CPSR to use and
         * a pointer to the function `run_task`.
         * `run_task` will call the task handler and start the scheduling
         * process.
         */
        asm volatile(
            "mov sp, %[stk_ptr]\n"
            "pop {r0-r12, lr}\n"
            "pop {r0-r1}\n"
            "msr cpsr, r1\n"
            "mov pc, r0\n"
        );
    } else {
        return -42;
    }
}


/*
 * schedule - Address: 0x2d0
 * 
 * Function called when we want to perform an explicit schedule of a task.
 */
void __attribute__((naked)) schedule()
{
    asm volatile(
        "mrs r0, cpsr\n"
        "push {r0}\n"
        "push {lr}\n"
        "push {r0-r3, r12, lr}\n"
    );

    /* Finds the next task to schedule */
    schedule_task();

    /*
     * If the task to schedule is different from the one currently running
     * then set the new task as the current task
     */
    if (g_task_to_schedule != g_current_task) {
        /* Saves the registers on the stack */
        asm volatile("push {r4-r11}\n");
        /* Saves the current stack pointer */
        g_current_task->stack_ptr = get_stack_pointer();
        /* Sets the new current task */
        g_current_task = g_task_to_schedule;
        /* Changes the stack pointer to the one of the new task */
        set_stack_pointer(g_current_task->stack_ptr);
        
        asm volatile(
            "ldr r0, [sp, $0x3c]\n"  // CPSR stored when `g_task_to_schedule`
                                     // called `schedule`
            "msr spsr, r0\n"
            "ldr r0, [sp, $0x38]\n"  // lr stored when `g_task_to_schedule`
                                     // called `schedule`
            "str r0, [sp, $0x3c]\n"  // This value will be popped into `pc``
                                     // to continue the execution of the task
                                     // after its call to `schedule`
        );

        /*
         * Restores the registers and branch to the `lr` value stored before
         * the call to `schedule`
         */
        asm volatile(
            "pop {r4-r11}\n"
            "pop {r0-r3, r12, lr}\n"
            "add sp, sp, $4\n"
            "pop {pc}^\n"
        );
    }

    /*
     * If tasks are the same, just pop back the register and return from the
     * function
     */
    asm volatile(
        "pop {r0-r3, r12, lr}\n"
        "add sp, sp, $8\n"
        "bx lr\n"
    );
}


/*
 * schedule_task - Address: 0xcc34
 * 
 * Looks for the next task to schedule from the ready list based on its
 * priority.
 */
void schedule_task() {
    /* Checks that the scheduler is allowed to run */
    u32 forbid_scheduling = g_scheduler_state.forbid_scheduling;
    if (forbid_scheduling)
        return;

    u32 state = g_current_task->state;
    u32 priority = g_current_task->priority;
    u32 do_schedule = 0;

    /* Checks that the task is not in an invalid state */
    if (!(state & (TASK_SUSPENDED | TASK_READY | TASK_SLEEPING | TASK_PENDING)))
        abort();

    /*
     * If the current task is running, take it off the ready list and add
     * it back to the end of it.
     */
    if (state == (TASK_READY | TASK_RUNNING) && \
            g_current_task->remaining_sched_slices <= 0) {
        list_del(&g_current_task->ready_list_entry);
        g_current_task->remaining_sched_slices = g_current_task->max_sched_slices;
        struct list_head *ready_list = &g_scheduler_state.ready_list[priority];
        /*
         * If the ready list for the current priority is empty when we remove
         * the task, do the scheduling even if the priority of the next one is
         * lower.
         */
        if (is_list_empty(ready_list))
            do_schedule = 1;
        list_add(&g_current_task->ready_list_entry, ready_list);
    } else {
        /* If no task is currently running, schedules a new one */
        do_schedule = 1;
    }

    /* Finds the priority of the next task to schedule */
    u32 prio_grp0_idx = g_scheduler_state.prio_grp0;
    u32 prio_grp0_val = g_priority_table[prio_grp0_idx];
    u32 prio_grp1_idx = g_scheduler_state.prio_grp1[prio_grp0_val];
    u32 prio_grp1_val = g_priority_table[prio_grp1_idx];
    u32 prio_grp2_idx = g_scheduler_state.prio_grp2[prio_grp0_val][prio_grp1_val];
    u32 prio_grp2_val = g_priority_table[prio_grp2_idx];

    u32 next_priority = \
        (prio_grp0_val << 6) | \
        (prio_grp1_val << 3) | \
        prio_grp2_val;

    /*
     * Only schedule if the priority value is lower than the current one
     * (priority values are the inverse of the actual priority, the lower the
     * value, the higher the priority).
     */
    if (next_priority < priority)
        do_schedule = 1;

    /* If we have to schedule a new task */
    if (do_schedule) {
        /*
         * Finds the next task in the ready list associated to the priority
         * we computed above.
         */
        struct list_head *ready_list = &g_scheduler_state.ready_list[next_priority];
        /* Aborts if the list is empty, because it should not happen. */
        if (is_list_empty(ready_list))
            abort();
        /*
         * Retrieves the beginning of the task structure base on its
         * `ready_list_entry` pointer */
        struct task *task = container_of(
            ready_list->next, struct task, ready_list_entry);
        /* Sets the task we got as the next one to schedule */
        g_task_to_schedule = task;
        /* Stops the current task */
        g_current_task->state &= ~(TASK_RUNNING);
        /* Marks the next task as running */
        g_task_to_schedule->state |= TASK_RUNNING;
    }
}


/*
 * schedule_tick - Address: 0xc324
 * 
 * Updates scheduling info for the current task after one scheduler tick.
 * Also wakes up tasks in the delayed list if needed.
 */
void schedule_tick() {
    if (!g_current_task)
        abort();

    /* Updating the scheduling slices */
    g_current_task->total_sched_slices++;
    if (g_scheduler_state.count_sched_slices == 1)
        g_current_task->remaining_sched_slices--;

    /* Returns if there are no sleeping tasks */
    if (is_list_empty(&g_scheduler_state.delayed_list))
        return;

    struct task *delayed_task = container_of(&g_scheduler_state.delayed_list,
        struct task, delayed_list_entry);

    /* Sanity checks on the first sleeping task */
    if (delayed_task->magic != TASK_MAGIC)
        abort();

    /* Decrements the delay of the first task */
    delayed_task->delay--;

    /* Iterates over tasks in the delayed list until their delay is above zero */
    while (delayed_task->delay <= 0) {

        /* 
         * Setting this variable will allow the scheduling of a new task when
         * the execution goes back to the irq handler.
         */
        g_do_schedule = 1;

        /* Sanity checks on the delayed task */
        if (!delayed_task \
                || delayed_task->magic != TASK_MAGIC \
                || !(delayed_task->state & TASK_SLEEPING))
            abort();
        
        /* Updates the delay of the next task (delay could be negative?) */
        struct task* next_delayed_task = delayed_task->delayed_list_entry.next;
        if (next_delayed_task != &g_scheduler_state.delayed_list)
            next_delayed_task->delay += delayed_task->delay;

        /* Removes the task from the delayed list */
        list_del(&delayed_task->delayed_list_entry);

        /* Adds the task we woke up to the ready list */
        if (__add_to_ready_list(delayed_task))
            abort();

        /* Returns if there are no sleeping tasks */
        if (is_list_empty(&g_scheduler_state.delayed_list))
            return;
        
        /* Sets the next task as the current one and continues the loop */
        delayed_task = next_delayed_task;
        if (delayed_task->magic != TASK_MAGIC)
            abort();
    }
}


/*
 * __add_to_ready_list - Address: 0xc324
 * 
 * Adds a task to a ready list when it's ready to run.
 */
int __add_to_ready_list(struct task *task) {
    /* Sanity checks on the task */
    if (!task || task->magic != TASK_MAGIC)
        abort();
    
    /* Retrieves the ready list associated to the task's priority */
    u32 priority = task->priority;
    struct list_head *ready_list = &g_scheduler_state.ready_list[priority];

    /* If the ready list is empty, add the priority in the priority groups */
    if (is_list_empty(ready_list)) {
        /* Computes the priority group values based on the task's priority */
        u8 grp0_val = priority >> 6;
        u8 grp1_val = (priority >> 3) & 7;
        u8 grp2_val = priority & 7;
        
        /* Adds the current task's priority to the priority group values */
        g_scheduler_state.prio_grp0 |= 1 << grp0_val;
        g_scheduler_state.prio_grp1[grp0_val] |= 1 << grp1_val;
        g_scheduler_state.prio_grp2[grp0_val][grp1_val] |= 1 << grp2_val;
    }

    /* Initializes the number of scheduling slices */
    task->remaining_sched_slices = task->max_sched_slices;
    /* Adds the task to the correponding ready list */
    list_add(&task->ready_list_entry, ready_list);
    /* Sets the task's state as ready */
    task->state = TASK_READY;

    return 0;
}


/*
 * __del_from_ready_list - Address: 0xbf30
 * 
 * Removes a task from its ready list.
 */
int __del_from_ready_list(struct task *task) {

    if (!task || task->magic != TASK_MAGIC)
        abort();
    
    if (!(task->state & TASK_READY))
        return -42;
    
    u32 priority = task->priority;
    struct list_head *ready_list = &g_scheduler_state.ready_list[priority];

    /* If the ready list is empty, return an error */
    if (is_list_empty(ready_list))
        return -42;

    /* It there is one item in the ready list */
    if (ready_list->next->next == ready_list) {
        /* Computes the priority group values based on the current priority */
        u8 grp0_val = priority >> 6;
        u8 grp1_val = (priority >> 3) & 7;
        u8 grp2_val = priority & 7;
        u8 *prio_grp0 = &g_scheduler_state.prio_grp0;
        u8 *prio_grp1 = &g_scheduler_state.prio_grp1[grp0_val];
        u8 *prio_grp2 = &g_scheduler_state.prio_grp2[grp0_val][grp1_val];

        /* Removes the current priority from the priority group values */
        u8 prio_tbl_val = *prio_grp2 & ~(1 << grp2_val);
        *prio_grp2 = prio_tbl_val;
        if (!prio_tbl_val) {
            u8 grp1_val = *prio_grp1 & ~(1 << grp1_val);
            *prio_grp1 = grp1_val;
            if (!grp1_val)
                *prio_grp0 = *prio_grp0 & ~(1 << grp0_val);
        }
    }

    /* Removes the task from the list */
    list_del(&task->ready_list_entry);
    /* Updates the task state */
    task->state |= TASK_NOT_READY;

    return 0;
}


/*
 * __add_to_pending_list - Address: 0xc760
 * 
 * Adds `task` to the pending list `wq`.
 */
int __add_to_pending_list(struct task *task, struct workqueue *wq) {
    if (!task || task->magic != TASK_MAGIC)
        abort();

    if(!(task->state & TASK_PENDING) || !(task->state & TASK_NOT_READY))
        return -42;

    u32 priority = TASK_MAGIC;
    u32 service = wq->service;
    struct list_head* wq_list = &wq->head;

    /* The operation performed depends on the `service` type of the workqueue */
    switch (service) {
        case 0:
            /* Simply adds the task to the workqueue list */
            list_add(&task->pending_list_entry, wq_list);
            break;
        case 1:
            if (!is_list_empty(wq_list)) {
                /*
                 * If the workqueue/pending list is not empty, then we retrieve
                 * the priority of the task since it's used to sort the tasks in
                 * the pending list.
                 */
                priority = task->priority;
            } else {
                /*
                 * Otherwise we just add the task to the list, since it doesn't
                 * need any sorting.
                 */
                list_add(&task->pending_list_entry, wq_list);
                break; 
            }

            struct task *neighbor_task = 0;
            struct list_head* curr_list_entry = 0;

            /* Gets the first element in the workqueue */
            struct task *curr_task = container_of(
                &wq_list->next, struct task, pending_list_entry);
            /* Gets the second element in the workqueue */
            struct task *next_task = container_of(
                &wq_list->next->next, struct task, pending_list_entry);
            
            /*
             * This loop tries to find a "neighbor" task, which is the first task
             * found in the list, while iterating, that has a priority greater
             * or equal than `priority`.
             * The task we want to add to the pending list will be added right
             * before the "neighbor" one.
             */
            do {
                /*
                 * The loop ends if the priority of the task we are currently
                 * iterating on is bigger than the one we want to add.
                 */
                if (priority < curr_task->priority)
                    break;
                neighbor_task = curr_task;
                curr_task = next_task;
                next_task = container_of(
                    &next_task->pending_list_entry.next, struct task,
                    pending_list_entry);
                curr_list_entry = &curr_task->pending_list_entry;
                /* The loop ends if we're back at the beginning of the list */
            } while (curr_list_entry != wq_list);

            if (neighbor_task) {
                /* The task is added before the neighbor task */
                list_add(&task->pending_list_entry,
                    neighbor_task->pending_list_entry);
            else
                /* The task is added at the end of the pending list */
                list_add(&task->pending_list_entry, wq_list);
            break;
        default:
            return -22;
    }

    /* Sets the task state as pending */
    task->state = TASK_PENDING;
    /* References the workqueue the task was added to */
    task->wait_queue = wq;

    return 0;
}


/*
 * __del_from_pending_list - Address: 0xbdb4
 * 
 * Removes `task` from the pending list it's currently attached to.
 */
int __del_from_pending_list(struct task *task) {

    if (!task || task->magic != TASK_MAGIC || !task->wait_queue)
        abort();

    /* Checks that the task is actually pending */
    if (!(task->state & TASK_PENDING))
        return -42;
    
    /* Checks that the pending list is not empty */
    if (is_list_empty(&task->wait_queue->head))
        return -42;

    /* Removes the task from the pending list */
    list_del(&task->pending_list_entry);
    task->wait_queue = 0;
    task->state |= TASK_NOT_READY;

    return 0;
}


/*
 * __add_to_delayed_list - Address: 0xc4e8
 * 
 * Adds a task to the delayed list for `delay` seconds.
 */
int __add_to_delayed_list(u32 delay) {
    u32 ret = 0;
    struct task *task = g_current_task;

    /* Checks that the scheduler is allowed to run */
    u32 forbid_scheduling = g_scheduler_state.forbid_scheduling;
    if (forbid_scheduling)
        return ret;

    ret = __del_from_ready_list(g_current_task);
    if (ret)
        return ret;

    if (!task || task->magic != TASK_MAGIC || delay <= 0)
        abort();

    if (!(task->state & (TASK_READY | TASK_NOT_READY)))
        return -42;
    
    struct task *neighbor_task = 0;
    struct list_head* curr_list_entry = 0;
    struct list_head* delayed_list = &g_scheduler_state.delayed_list;

    /* Gets the first element in the delayed list */
    struct task *curr_task = container_of(
        delayed_list, struct task, delayed_list_entry);
    /* Gets the second element in the delayed list */
    struct task *next_task = container_of(
        delayed_list, struct task, delayed_list_entry);
    
    /*
    * This loop tries to find a "neighbor" task, which is the first task
    * found in the list, while iterating, that has a delay greater
    * or equal than `delay`.
    * The task we want to add to the delayed list will be added right
    * before the "neighbor" one.
    */
    do {
        /*
        * The loop ends if the delay of the task we are currently
        * iterating on is bigger than the one we want to add.
        */
        if (delay < curr_task->delay)
            break;
        /*
         * Subtracts the delay of the task, since tasks are stored with a delay
         * relative to each others.
         */
        delay -= curr_task->delay;
        neighbor_task = curr_task;
        curr_task = next_task;
        next_task = container_of(
            &next_task->delayed_list_entry.next, struct task,
            delayed_list_entry);
        curr_list_entry = &curr_task->delayed_list_entry;
        /* The loop ends if we're back at the beginning of the list */
    } while (curr_list_entry != delayed_list);

    if (neighbor_task) {
        /* The task is added before the neighbor task */
        list_add(&task->delayed_list_entry, neighbor_task->delayed_list_entry);
    else
        /* The task is added at the end of the delayed list */
        list_add(&task->delayed_list_entry, delayed_list);

    /*
     * Updates the delay of the next task to keep all delays in the list
     * relative to each other
     */
    if (&curr_task->delayed_list_entry != &g_scheduler_state.delayed_list)
        curr_task->delay -= delay;

    /* Updates task info */
    task->state = TASK_SLEEPING;
    task->delay = delay;

    /* Schedules another task */
    schedule();

    return 0;
}

/*
 * __del_from_delayed_list - Address: 0xd590
 * 
 * Removes a sleeping task from the delayed list.
 */
unsigned int __del_from_delayed_list(struct task *task) {
    /* Sanity checks*/
    if (!task || task->magic != TASK_MAGIC)
        abort();

    /* Checks that the task is effectively sleeping */
    if (!(task->state & TASK_SLEEPING))
        return -42;

    /* If the task is not the last in the list, update the delay of the next one */
    struct task *next_task = container_of(
        task->delayed_list_entry.next, struct task, delayed_list_entry);
    if (&next_task->delayed_list_entry != &g_scheduler_state.delayed_list)
        next_task->delay = next_task->delay + task->delay;

    /* Deletes the task from the delayed list */
    list_del(task->delayed_list_entry);
    /* Resets the delay to 0 */
    task->delay = 0;
    /* Updates the task's state */
    task->state |= TASK_NOT_READY;

    return 0;
}
