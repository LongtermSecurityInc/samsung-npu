#include "npu.h"


/*
 * events_init - Address: 0xe0dc
 * 
 * Events state structure initialization function.
 */
void events_init() {
    /* Initializes the total number of events to 0 */
    g_events_state.nb_events = 0;

    /* Initializes the list of events */
    list_init(&g_events_state.event_list);

    /* Initializes the waiting lists and the counts */
    for (int i = 0; i <= MAX_EVENT_IDS; ++i) {
        list_init(&g_events_state.event_waiting_lists[i]);
        g_events_state.standby_cnts[i] = 0;
        g_events_state.trigger_cnts[i] = 0;
    }

    /* Sets the magic value and resets the state for each event */
    for (int i = 0; i < MAX_EVENTS; i++) {
        g_events_state.events[i].magic = EVENT_MAGIC;
        g_events_state.events[i].state = 0;
    }
}


/*
 * __alloc_event - Address: 0xd618
 * 
 * Creates and returns a new event referenced by `id` in `g_events_state`.
 */
struct event *__alloc_event(u32 id, u32 rule) {
    struct event *event = 0;
    u32 available_event_id = 0;

    if (id >= MAX_EVENT_IDS || rule >= 3)
        return 0;

    /* Finds an empty event structure in `g_events_state` */
    while (g_events_state.events[available_event_id].state & 1) {
        if (++available_event_id >= MAX_EVENTS)
            abort();
    }
    event = &g_events_state.events[available_event_id];
    if ( event->magic != EVENT_MAGIC )
        abort();

    u32 standby_cnt = g_events_state.standby_cnts[id];
    if (standby_cnt != 0 && standby_cnt != rule)
        return 0;

    /* Initializes the event's workqueue */
    int ret = init_wqueue(event->wq, 0, event);
    if (ret)
        return 0;

    /* Updates `g_events_state` with the new event */
    g_events_state.nb_events++;
    g_events_state.standby_cnts[id]++;
    g_events_state.event_flags[id] = rule;
    list_add(&event->event_list_entry, &g_events_state.event_list);

    /* Sets the id and state of the event */
    event->id = id;
    event->state = event->state & ~EVENT_WAITING | EVENT_READY;

    return event;
}


/*
 * __free_event - Address: 0xd8c4
 * 
 * Creates and returns a new event referenced by `id` in `g_events_state`.
 */
int __free_event(struct event *event) {
    int ret = 0;

    if (!event || event->magic != EVENT_MAGIC)
        abort();

    if (!g_events_state.standby_cnts[event->id])
        return -22;

    ret = cleanup_wqueue(event->wq);
    if (ret)
        return ret;

    ret = deinit_wqueue(event->wq);
    if (ret)
        return ret;

    /* Removes the event from `g_events_state` */
    g_events_state.nb_events--;
    g_events_state.standby_cnts[event->id]--;
    list_del(&event->event_list_entry);

    /* Resets the id and state of the event */
    event->id = 0;
    event->state = event->state & ~EVENT_READY;

    return ret;
}


/*
 * __set_event_no_schedule - Address: 0xda60
 * 
 * This function goes through the events in the waiting list associated to a
 * given event and wakes up all tasks waiting on it.
 */
int __set_event_no_schedule(struct event* event) {
    if (!event || event->magic != EVENT_MAGIC || event->id > MAX_EVENT_IDS)
        abort();

    u32 ret = 0;
    u32 id = event->id;
    struct list_head* waiting_list = &g_events_state.event_waiting_lists[id];

    switch (g_events_state.event_flags[id]) {
        case 0:
            /* Gets the first element of the waiting list */
            struct event* curr_event = container_of(
                waiting_list->next, struct event, waiting_list_entry);

            /* Gets the second element of the waiting list */
            struct event* next_event = container_of(
                waiting_list->next->next, struct event, waiting_list_entry);

            /* Checks that the waiting list is not empty */
            if (!is_list_empty(waiting_list)) {
                /*
                 * This loop goes through all events in the list and wake them
                 * up. It stops once the current element is the list head.
                 */
                for (;;) {
                    /* Tries to wake up the current event */
                    ret = wakeup_wqueue(&curr_event->wq);
                    if (ret)
                        break;
                    /*
                     * If the current event was woken up, remove it from the
                     * waiting list.
                     */
                    list_del(&curr_event->waiting_list_entry);
                    /* Resets the waiting state */
                    event->state = event->state & ~EVENT_WAITING;
                    /* 
                     * Sets the next event as the current one to continue
                     * the process.
                     */
                    curr_event = next_event;
                    /* Next element while iterating through the list */
                    next_event = container_of(
                        &next_event->waiting_list_entry.next,
                        struct event, waiting_list_entry);
                    /* Stops the iteration once we're back to the list head */
                    if (&curr_event->waiting_list_entry == waiting_list)
                        return ret;
                }
            }
            break;
        case 1:
            /* Gets the first element of the waiting list */
            struct event* curr_event = container_of(
                waiting_list, struct event, waiting_list_entry);

            /* Gets the second element of the waiting list */
            struct event* next_event = container_of(
                waiting_list->next, struct event, waiting_list_entry);

            /* Checks that the waiting list is not empty */
            if (!is_list_empty(waiting_list)) {
                /*
                 * This loop goes through all events in the list and wake them
                 * up. It stops once the current element is the list head.
                 */
                for (;;) {
                    /* Tries to wake up the current event */
                    ret = wakeup_wqueue(&curr_event->wq);
                    if (ret)
                        break;
                    /*
                     * If the current event was woken up, remove it from the
                     * waiting list.
                     */
                    list_del(&curr_event->waiting_list_entry);
                    /* Resets the waiting state */
                    event->state = event->state & ~EVENT_WAITING;
                    /* 
                     * Sets the next event as the current one to continue
                     * the process.
                     */
                    next_event = next_event;
                    /* Next element while iterating through the list */
                    next_event = container_of(
                        &next_event->waiting_list_entry.next,
                        struct event, waiting_list_entry);
                    /* Stops the iteration once we're back to the list head */
                    if (&curr_event->waiting_list_entry == waiting_list)
                        return ret;
                }
            } else {
                /* Increases the trigger count */
                g_events_state.trigger_cnts[id]++;
            }
            break;
        case 2:
            /* Gets the first element of the waiting list */
            struct event* curr_event = container_of(
                waiting_list, struct event, waiting_list_entry);
            
            /* Checks that the waiting list is not empty */
            if (!is_list_empty(waiting_list)) {
                /* Tries to wake up the current event */
                ret = wakeup_wqueue(&curr_event->wq);
                if (ret)
                    return ret;
                /*
                * If the current event was woken up, remove it from the
                * waiting list.
                */
                list_del(&curr_event->waiting_list_entry);
                /* Resets the waiting state */
                event->state = event->state & ~EVENT_WAITING;
            } else {
                /* Increases the trigger count */
                g_events_state.trigger_cnts[id]++;
            }
            break;
        default:
            return -22;
    }

    return 0;
}


/*
 * __wait_event - Address: 0xdd38
 * 
 * This function queues an event into its associated waiting list and pauses
 * the current tasks, waiting for it to be woken back up.
 */
int __wait_event(struct event *event) {
    u32 ret = 0;
    u32 id = event->id;

    if (!event || event->magic != EVENT_MAGIC)
        abort();
    
    /* Decreases the trigger count */
    if (g_events_state.trigger_cnts[id]) {
        g_events_state.trigger_cnts[id]--;
        return 0;
    }

    /* Removes the current task from the ready list... */
    ret = __del_from_ready_list(g_current_task);
    if (ret)
        return ret;
    /* ... and adds it to the pending list of this event */
    ret = __add_to_pending_list(g_current_task, &event->wq);
    if (ret)
        return ret;

    /* Checks that the event is not EVENT_WAITING */
    u32 state = (event->state >> 1) & 1;
    if (state)
        return ret;

    /* Adds the event to the corresponding waiting list */
    list_add(&event->waiting_list_entry,
        &g_events_state.event_waiting_lists[id]);
    event->state = event->state | EVENT_WAITING;
    
    /* Schedlues a new task */
    schedule();

    return ret;
}

