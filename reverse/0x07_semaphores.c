#include "npu.h"


/*
 * semaphores_init - Address: 0xbc78
 * 
 * Semaphores state structure initialization function.
 */
void semaphores_init() {
    g_semaphores_state.nb_semaphores = 0;
    list_init(&g_semaphores_state.semaphore_list);
}


/*
 * __create_semaphore - Address: 0xb55c
 * 
 * Creates a semaphore and adds it to the global list in `g_semaphores_state`
 */
int __create_semaphore(struct semaphore *sem, int name, int count) {
    u32 ret = 0;
    
    if (!sem || sem->magic == SEM_MAGIC || !name)
        abort();

    if (count < 0)
        return -41;
    
    ret = init_wqueue(&sem->wq, 1, sem);
    if (ret)
        return ret;
    
    list_add(sem, &g_semaphores_state.semaphore_list);
    g_semaphores_state.nb_semaphores++;

    sem->count = count;
    sem->name = name;
    sem->magic = SEM_MAGIC;

    return ret;
}


/*
 * __delete_semaphore - Address: 0xb6cc
 * 
 * Deletes a semaphore and removes it from the global list in
 * `g_semaphores_state`
 */
int __delete_semaphore(struct semaphore *sem) {
    u32 ret = 0;

    if (!sem || sem->magic != SEM_MAGIC)
        abort();

    ret = cleanup_wqueue(&sem->wq);
    if (ret)
        return ret;

    ret = deinit_wqueue(&sem->wq);
    if (ret)
        return ret;

    list_del(&sem->entry);
    g_semaphores_state.nb_semaphores--;

    sem->count = 0;
    sem->magic = 0;

    return ret;
}


/*
 * __down - Address: 0xb900
 * 
 * Decreases a semaphore by one and blocks the current task if the count is 0.
 */
int __down(struct semaphore *sem) {
    u32 ret = 0;

    if (!sem || sem->magic != SEM_MAGIC)
        abort();

    if (sem->count > 0) {
        sem->count--;
        return ret;
    }
    
    ret = __del_from_ready_list(g_current_task);
    if (ret)
        return ret;

    ret = __add_to_pending_list(g_current_task, &sem->wq);
    if (ret)
        return ret;
    
    schedule();

    return ret;
}


/*
 * __up - Address: 0xb81c
 * 
 * Increases the semaphore by one if the semaphore is not blocking, otherwise
 * wakes up the waiting process with `__up_sema_no_schedule`.
 */
int __fastcall __up(struct semaphore *sem) {
    u32 ret = 0;

    if (!sem || sem->magic != SEM_MAGIC)
        abort();

    if (is_list_empy(&sem->wq.head))
        sem->count++;
    else
        ret = __up_sema_no_schedule(sem);

    return ret;
}


/*
 * __up_sema_no_schedule - Address: 0xbc90
 * 
 * Wakes up the process waiting on a semaphore.
 */
int __up_sema_no_schedule(struct semaphore *sem) {
    u32 ret = 0;
    struct task *task = container_of(&sem->wq.head.next, struct task,
        pending_list_entry);

    if (task->magic != TASK_MAGIC)
        abort();
    
    ret = __del_from_pending_list(task);
    if (ret)
        return ret;
    
    ret = __add_to_ready_list(task);
    if (ret)
        return ret;
}
