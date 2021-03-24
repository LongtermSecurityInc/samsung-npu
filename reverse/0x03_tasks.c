#include "npu.h"


/*
 * create_task - Address: 0xd1cc
 * 
 * Creates and initializes a new task.
 */
int create_task(struct task *task, void (*handler)(), u32 priority, char *name, 
        u32 max_slices, u32 stack_addr, u32 stack_size, void *args) {
    if (!task || task->magic == TASK_MAGIC)
        abort();

    if (priority >= TASK_MAX_PRIORITY || stack_addr & 3 || stack_size & 3)
        return -22;

    /* Initializes task info */
    task->priority = priority;
    task->handler = handler;
    task->name = name;
    task->remaining_sched_slices = max_slices;
    task->args = args;
    task->max_sched_slices = max_slices;
    task->total_sched_slices = 0;
    bzero(task->_unk_60, 0x60u);
    task->wait_queue = 0;

    /* Initializes the stack */
    task->stack_start = stack_addr + stack_size;
    task->stack_end = stack_addr;
    task->stack_size = stack_size;
    task->stack_ptr = init_task_stack(
        task->stack_start, task->stack_end, run_task);

    /* If the stack is too small, returns an error */
    if (task->stack_ptr < task->stack_end)
        return -22;

    /* Checks if interrupts are currently masked */
    u32 interrupts_masked = read_cpsr() & 0x80;
    /* Disables interrupts */
    __disable_irq();

    /* Adds the task to the global tasks list */
    list_add(&task->tasks_list_entry, &g_scheduler_state.tasks_list);
    /* Increments the total number of tasks */
    g_scheduler_state.nb_tasks++;
    /* Sets the task as suspended */
    task->state = TASK_SUSPENDED;
    /* Sets the task magic value */
    task->magic = TASK_MAGIC;

    /* Re-enables interrupts if they were disabled */
    if (!interrupts_masked)
        __enable_irq();

    return 0;
}


/*
 * run_task - Address: 0xc978
 * 
 * Calls the handler of the current task and suspends it when it's done working.
 */
void run_task() {
    for (;;) {
        /* Calls the task's handler */
        g_current_task->handler(g_current_task->args);

        /* Skips the slow path if we are in IRQ mode */
        if (read_cpsr() & 0x1F == 0x12)
            continue;

        /* Checks if interrupts are currently masked */
        u32 interrupts_masked = read_cpsr() & 0x80;
        /* Disables interrupts */
        __disable_irq();
        /* Suspends the current task until we resume it explicitely */
        __suspend_task(g_current_task);
        /* Re-enables interrupts if they were disabled */
        if (!interrupts_masked)
            __enable_irq();
    }
}


/*
 * __suspend_task - Address: 0xc088
 * 
 * Suspends a tasks.
 */
int __suspend_task(struct task *task) {
    if (!task || task != TASK_MAGIC)
        abort();

    u32 ret = 0;
    u32 state = task->state;

    /* Checks that the task is in a compatible state */
    if (!(state & (TASK_READY | TASK_SLEEPING | TASK_PENDING)))
        return -42;
    
    switch (state) {
        case TASK_READY:
        case TASK_READY | TASK_RUNNING:
            /* If the task is ready, remove it from the ready list */
            ret = __del_from_ready_list(task);
            if (ret)
                return ret;
            break;
        case TASK_SLEEPING:
            /* If the task is sleeping, remove it from the delayed list */
            ret = __del_from_delayed_list(task);
            if (ret)
                return ret;
            break;
        case TASK_PENDING:
            /* If the task is pending, first retrieve the associated wq */
            struct workqueue* wait_queue = task->wait_queue;
            if (!wait_queue)
                return ret;
            void* obj = wait_queue->obj;
            /* If the workqueue object is a semaphore, increment the count */
            if (((struct semaphore*)obj)->magic == SEM_MAGIC)
                ((struct semaphore*)obj)->count++;
            /* Removes the task from the pending list */
            ret = __del_from_pending_list(task);
            if (ret)
                return ret;
            break;
        default:
            abort()
    }

    /* Sets the task state as suspended */
    task->state = TASK_SUSPENDED;
    /* Explicit schedule */
    schedule();

    return ret;
}


/*
 * __resume_task - Address: 0xc404
 * 
 * Resumes a task that was suspended.
 */
int __resume_task(struct task *task) {
    if (!task || task->magic != TASK_MAGIC)
        abort();

    if (!(task->state & TASK_SUSPENDED))
        return -42;

    __add_to_ready_list(task);
    schedule();

    return 0;
}
