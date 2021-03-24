#include "npu.h"


#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})


/* ------------------------------------------------------------------------- */
/* Lists */

int is_list_empty(struct list_head *head) {
    return head->next == head;
}

void list_init(struct list_head *entry) {
    entry->next = entry->prev;
    entry->prev = entry->prev;
}

void list_add(struct list_head *new, struct list_head *head) {
    struct list_head* prev = head->prev;
    head->prev = new;
    prev->next = new;
    new->next = head;
    new->prev = prev;
}

void list_del(struct list_head *entry) {
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
    entry->next = 0xdeadcafe;
    entry->prev = 0xcafedead;
}


/* ------------------------------------------------------------------------- */
/* Workqueues */

/*
 * init_wqueue - Address: 0xe238
 * 
 * Initializes a workqueue.
 */
int init_wqueue(struct workqueue *wq, u32 _unused, void* obj) {
    struct list_head *v3; // r0

    if (!wq || !obj)
        abort();

    wq->service = 0;
    wq->obj = obj;
    wq->task = 0;
    list_init(&wq->head);

    return 0;
}

/*
 * deinit_wqueue - Address: 0xe2ac
 * 
 * Deinitializes a workqueue.
 */
int deinit_wqueue(struct workqueue *wq) {
    if (!wq)
        abort();

    if (wq->task || !is_list_empty(&wq->head))
        return -42;

    wq->obj = 0;
    return 0;
}


/*
 * wakeup_wqueue - Address: 0xe36c
 * 
 * Iterates through all task entries in a waiting workqueue, removes them from
 * their pending list and adds them to the ready list.
 */
int wakeup_wqueue(struct workqueue *wq) {
    u32 ret = 0;

    /* Sanity checks */
    if (!wq)
        abort();

    /* Returns immediately if the list is empty */
    if (is_list_empty(&wq->head))
        return 0;

    /* Gets the first task of the pending list */
    struct task* curr_task = container_of(
        &wq->head, struct task, pending_list_entry);

    /* Gets the second task of the pending list */
    struct task* next_task = container_of(
        &wq->head->next, struct task, pending_list_entry);

    /* Iterates as long as we haven't gone through all pending list entries */
    while (&curr_task->pending_list_entry != &wq->head) {
        /* Removes a task from the pending list... */
        ret = __del_from_pending_list(curr_task);
        if (ret)
            return ret;
        /* ... and adds it to the ready list */
        ret = __add_to_ready_list(curr_task);
        if (ret)
            return ret;

        /* Retrieves the next task */
        curr_task = next_task;
        next_task = container_of(&next_task->pending_list_entry.next,
            struct task, pending_list_entry);
    }

    return 0;
}



/*
 * cleanup_wqueue - Address: 0xe478
 *
 * Similar to `wakeup_wqueue`, but sets `_unk_18` before adding the task
 * in the ready list
 */
int cleanup_wqueue(struct workqueue *wq) {
    u32 ret = 0;

    /* Sanity checks */
    if (!wq)
        abort();

    /* Returns immediately if the list is empty */
    if (is_list_empty(&wq->head))
        return 0;

    /* Gets the first task of the pending list */
    struct task* curr_task = container_of(
        &wq->head, struct task, pending_list_entry);

    /* Gets the second task of the pending list */
    struct task* next_task = container_of(
        &wq->head->next, struct task, pending_list_entry);

    /* Iterates as long as we haven't gone through all pending list entries */
    while (&curr_task->pending_list_entry != &wq->head) {
        /* Removes a task from the pending list */
        ret = __del_from_pending_list(curr_task);
        if (ret)
            return ret;
        /* Unused? */
        curr_task->_unk_18 |= 4;
        /* Adds the task to the ready list */
        ret = __add_to_ready_list(curr_task);
        if (ret)
            return ret;

        /* Retrieves the next task */
        curr_task = next_task;
        next_task = container_of(&next_task->pending_list_entry.next,
            struct task, pending_list_entry);
    }

    return 0;
}
