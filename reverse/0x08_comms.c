#include "npu.h"


/*
 * comm_channels_init - Address: 0x62e0
 * 
 * Initializes interrupts, handlers and events related to communication 
 * channels between the AP and the NPU.
 */
void comm_channels_init() {
    /*
     * Initializes events and interrupts for the reception of messages by the
     * high and low priority mailboxes
     */
    mailbox_init(0x401C0000);

    /*
     * Initializes events and interrupts for the reception of messages by
     * CMDQ (Command Queue?)
     */
    cmdq_init();

    /* Operations on an unknown mapped device */
    comm_init_unknown_0();

    /* Initialization of an unknown component called EV */
    ev_init(0x40110000);

    /*
     * Initializes timer related settings and, in particular, sets the
     * IRQ handler for timer ticks.
     */
    timer_handler_init(1);

    /* Operations on an unknown mapped device */
    comm_init_unknown_1(0x4000C000);

    /* Cleans up and invalidates caches, TLB and branch predictor */
    tlb_bp_clean_caches();
}


/*
 * mailbox_init - Address: 0x19e0
 * 
 * Initializes events used by low and high priority mailboxes.
 * Maps the IRQ triggered when receiving a message in a mailbox to their
 * handlers.
 */
void mailbox_init(u32 mapped_dev_addr) {
    /* [...] */

    g_mailbox_low_prio_event = __alloc_event(0x46u, 1);
    if ( !g_mailbox_low_prio_event )
        abort();
    g_mailbox_high_prio_event = __alloc_event(0x48u, 1);
    if ( !g_mailbox_high_prio_event )
        abort();

    /* [...] */

    request_irq(0x70, mailbox_low_prio_msg_received, 0);
    request_irq(0x71, mailbox_high_prio_msg_received, 0);
    request_irq(0x160, mailbox_low_prio_msg_received, 0);
    request_irq(0x161, mailbox_high_prio_msg_received, 0);
}


/*
 * mailbox_low_prio_msg_received - Address: 0x19b0
 * 
 * Handler called when a message is received in the low prio mailbox.
 * Sets the associated event to signify to the task that messages arrived.
 */
int mailbox_low_prio_msg_received() {
    /* [...] */
    return __set_event(g_mailbox_low_prio_event);
}


/*
 * mailbox_high_prio_msg_received - Address: 0x19c8
 * 
 * Handler called when a message is received in the high prio mailbox.
 * Sets the associated event to signify to the task that messages arrived.
 */
int mailbox_high_prio_msg_received() {
    /* [...] */
    return __set_event(g_mailbox_high_prio_event);
}


/*
 * mailbox_init - Address: 0x19e0
 * 
 * Initializes events and interrupts used by CMDQ.
 */
void cmdq_init() {
    for (int i = 0; i < 2; i++) {
        struct cmdq_info *cmdq_info = &g_cmdq_info[i];
        cmdq_info->_unk_34 = 0;
        request_irq(cmdq_info->done_handler_intr_0, cmdq_done_handler, i);
        request_irq(cmdq_info->process_handler_intr_0, cmdq_process_handler, i);
        request_irq(cmdq_info->err_handler_intr_0, cmdq_err_handler, i);
        request_irq(cmdq_info->done_handler_intr_1, cmdq_done_handler, i);
        request_irq(cmdq_info->process_handler_intr_1, cmdq_process_handler, i);
        request_irq(cmdq_info->err_handler_intr_1, cmdq_err_handler, i);
        cmdq_info->event1 = __alloc_event(cmdq_info->event_id_0, 1);
        if (!cmdq_info->event1)
            abort();
        cmdq_info->event0 = _alloc_event(cmdq_info->event_id_1, 1);
        if (!cmdq_info->event0)
            abort();
    }
}

