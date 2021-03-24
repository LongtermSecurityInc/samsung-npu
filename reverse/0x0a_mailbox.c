#include "npu.h"


/* ------------------------------------------------------------------------- */
/* Mailbox initialization functions */

/*
 * mailbox_controller_init - Address: 0x104f0
 * 
 * Initialized the controllers for the following mailboxes:
 *  - low priority
 *  - high priority
 *  - response
 *  - report
 */
u32 mailbox_controller_init() {
    u32 ret = 0;

    /* Initializes the NCP handlers used for neural computation */
    init_ncp_handlers(&g_ncp_handler_state);

    g_mailbox_hdr_ptr = &g_mailbox_hdr;
    g_mailbox_hdr.debug_code = 0x14;

    /*
     * Sets the first signature used by the host to know that the firmware is
     * up and running
     */
    g_mailbox_hdr.signature1 = 0xC0FFEE0;
    /* Waits for the host to acknowledge that the firmware is running */
    while ( g_mailbox_hdr.signature2 != 0xC0DEC0DE )
        sleep(1);

    g_debug_time = g_mailbox_hdr.debug_time;

    /* Checks on the fields of the mailbox header struct */
    ret = mbx_ipc_chk_hdr(&g_mailbox_hdr);
    if (ret)
        return ret;

    /* Initializes the low priority mailbox */
    ret = mbx_dnward_init(&g_mailbox_h2fctrl_lpriority, 0,
        &g_mailbox_hdr.h2fctrl[0].sgmt_ofs, 0x80000);
    if (ret)
        return ret;

    /* Initializes the high priority mailbox */
    ret = mbx_dnward_init(&g_mailbox_h2fctrl_hpriority, 2,
        &g_mailbox_hdr.h2fctrl[1].sgmt_ofs, 0x80000);
    if (ret)
        return ret;

    /* Initializes the response mailbox */
    ret = mbx_upward_init(&g_mailbox_f2hctrl_response, 3,
        &g_mailbox_hdr.f2hctrl[0].sgmt_ofs, 0x80000);
    if (ret)
        return ret;

    /* Initializes the report mailbox */
    ret = mbx_report_init(&g_mailbox_f2hctrl_report, 4,
        &g_mailbox_hdr.f2hctrl[1].sgmt_ofs, 0x80000);
    if (ret)
        return ret;

    /* Sets the mailbox as ready */
    g_mailbox_ready |= 1u;
    if ( g_mailbox_hdr.version != 0x80007 )
        abort();

    return ret;
}


/*
 * init_ncp_handlers - Address: 0xfebc
 * 
 * Initializes message object and ncp handlers.
 */
void init_ncp_handlers(struct ncp_handler_state_t *ncp_handler_state) {
    ncp_handler_state->_unk_0x364 = 4;
    
    /* Resets messages state */
    for (int i = 0; i < NB_MESSAGES; i++) {
        ncp_handler_state->messages[i].state = RESPONSE_FREE;
    }
    
    /* Initializes the handlers for the request commands */
    ncp_handler_state->handlers[0] = ncp_manager_load;
    ncp_handler_state->handlers[1] = ncp_manager_unload;
    ncp_handler_state->handlers[2] = ncp_manager_process;
    ncp_handler_state->handlers[3] = profile_control;
    ncp_handler_state->handlers[4] = ncp_manager_purge;
    ncp_handler_state->handlers[5] = ncp_manager_powerdown;
    ncp_handler_state->handlers[7] = ncp_manager_policy;
    ncp_handler_state->handlers[6] = ut_main_func;
    ncp_handler_state->handlers[8] = ncp_manager_end;
}


/*
 * mbx_ipc_chk_hdr - Address: 0xeb70
 * 
 * Checks that the fields `sgmt_len` in the control structures from the mailbox
 * header are all powers of 2.
 */
i32 mbx_ipc_chk_hdr(struct mailbox_hdr *mbox) {
    u32 count = 0;

    if (!(mbox->h2fctrl[0].sgmt_len & (mbox->h2fctrl[0].sgmt_len - 1)))
        count++;
    if (!(mbox->h2fctrl[1].sgmt_len & (mbox->h2fctrl[1].sgmt_len - 1)))
        count++;
    if (!(mbox->f2hctrl[0].sgmt_len & (mbox->f2hctrl[0].sgmt_len - 1)))
        count++;
    if (!(mbox->f2hctrl[1].sgmt_len & (mbox->f2hctrl[1].sgmt_len - 1)))
        count++;

    if (count >= 4)
        return 0;

    return -22;
}


/*
 * mbx_dnward_init - Address: 0xe5a0
 * 
 * Initializes a downward mailbox's event and structure fields
 */
i32 mbx_dnward_init(struct mailbox_dnward *mbox, u32 event_id_off,
        struct mailbox_ctrl *hdr_ptr, u32 start) {
    i32 ret = 0;

    /* Creates the event associated to the mailbox */
    mbox->event = alloc_event(event_id_off + 0x46, 1);
    if (mbox->event) {
        mbox->hctrl = hdr_ptr;
        mbox->event_id_off = event_id_off;
        mbox->start = start;
        mbox->fctrl = *hdr_ptr;
        return 0;
    }
    
    return -22;
}


/*
 * mbx_upward_init - Address: 0x104f0
 * 
 * Initialized the controllers for the following mailboxes:
 *  - low priority
 *  - high priority
 *  - response
 *  - report
 */
i32 mbx_upward_init(struct mailbox_upward *mbox, u32 event_id_off,
        struct mailbox_ctrl *hctrl, u32 start) {

    /* Creates the response event */
    mbox->event = alloc_event(event_id_off + 0x46, 1);
    if (!mbox->event)
        return -22;
    
    /* Initializes the available response list */
    mbox->available.type = RESPONSE_LIST_AVAILABLE;
    mbox->available.nb_entries = 0;
    mbox->available._unk_8 = 0;
    list_init(&mbox->available.list);

    /* Initializes the pending response list */
    mbox->pending.type = RESPONSE_LIST_PENDING;
    mbox->pending.nb_entries = 0;
    mbox->pending._unk_8 = 0;
    list_init(&mbox->pending.list);

    for (int i = 0; i < NB_MESSAGES; i++) {
        struct list_head *available_list = &mbox->available.list;
        struct list_head *chosen_rsp;
        struct response *response = &mbox->responses[i];
        response->id = i;
        response->priority = 0;

        if (mbox->available.type == RESPONSE_LIST_AVAILABLE) {
            struct response *curr_rsp = 0;
            /* Gets the first element in the available list */
            struct response *next_rsp = container_of(
                available_list->next,
                struct response, response_list_entry);
            /* Gets the second element in the available list */
            struct response *next_next_rsp = container_of(
                available_list->next->next,
                struct response, response_list_entry);

            chosen_rsp = &mbox->available.list;

            if (!is_list_empty(available_list)) {
                /*
                 * Iterating over the available list entries, until we're back
                 * to the first one. This operation is used to find an entry
                 * we want to queue our response next to.
                 */
                do {
                    /*
                     * `priority` should be used as some kind of attribute
                     * used to determine the order of the message in the list.
                     * However it doesn't seem like it's changed anywhere, its
                     * value is always set to 0.
                     */
                    if (next_rsp->priority)
                        break;
                    curr_rsp = next_rsp;
                    next_rsp = next_next_rsp;
                    next_next_rsp = container_of(
                        next_next_rsp->response_list_entry.next,
                        struct response, response_list_entry);
                } while (&next_rsp->response_list_entry != available_list);

                /* If an available response was found, gets its list entry ptr */
                if (curr_rsp)
                    chosen_rsp = &curr_rsp->response_list_entry;
            }
        }
    
        /* Checks if interrupts are currently masked */
        u32 interrupts_masked = read_cpsr() & 0x80;
        /* Disables interrupts */
        __disable_irq();

        if (chosen_rsp)
            /* If a response was found, queues next to this one */
            list_add(&response->response_list_entry, chosen_rsp);
        else
            /* Otherwise links directly to the available list */
            list_add(&response->response_list_entry, available_list);
        
        /* Increments the number of available entries */
        mbox->available.nb_entries++;

        /* Re-enables interrupts if they were disabled */
        if (!interrupts_masked)
            __enable_irq();
    }

    mbox->event_id_off = event_id_off;
    mbox->start = start;
    mbox->hctrl = hctrl;

    return 0;
}



/* ------------------------------------------------------------------------- */
/* Downward Mailboxes functions */


/*
 * TASK_mailbox_lowpriority - Address: 0x10024
 * 
 * Receives and handles messages from the low priority mailbox
 */
void TASK_mailbox_lowpriority() {
    u32 ret = 0;
    u32 sgmt_cursor = 0;
    struct message *message;
    
    /* Waits for the mailbox to be initialized */
    while (!(g_mailbox_ready & 1))
        sleep(1);

    for (;;) {
        /* Waits and retrieves a message from the low priority mailbox */
        ret = mbx_dnward_get(&g_mailbox_h2fctrl_lpriority, &message,
            &sgmt_cursor);
        if (ret)
            break;

        /* Handles the request and sends the response to the response mailbox */
        ret = mbx_msghub_req(&g_ncp_handler_state, &message, sgmt_cursor,
            &g_mailbox_h2fctrl_lpriority);
        if (ret)
            break;
    }

    abort();
}


/*
 * TASK_mailbox_highpriority - Address: 0x10174
 * 
 * Receives and handles messages from the high priority mailbox
 */
void TASK_mailbox_highpriority() {
    u32 ret = 0;
    u32 sgmt_cursor = 0;
    struct message *message;
    
    /* Waits for the mailbox to be initialized */
    while (!(g_mailbox_ready & 1))
        sleep(1);

    for (;;) {
        /* Waits and retrieves a message from the high priority mailbox */
        ret = mbx_dnward_get(&g_mailbox_h2fctrl_hpriority, &message,
            &sgmt_cursor);
        if (ret)
            break;

        /* Handles the request and sends the response to the response mailbox */
        ret = mbx_msghub_req(&g_ncp_handler_state, &message, sgmt_cursor,
            &g_mailbox_h2fctrl_hpriority);
        if (ret)
            break;
    }

    abort();
}


/*
 * mbx_dnward_get - Address: 0xe66c
 * 
 * Waits for and retrieves a new message from a downward mailbox
 */
i32 mbx_dnward_get(struct mailbox_dnward *mbox, struct message *message,
        u32 *sgmt_curstor) {
    
    i32 ret = 0;
    struct mailbox_ctrl* hctrl = mbox->hctrl;
    struct mailbox_ctrl* fctrl = &mbox->fctrl;

    for (;;) {
        u32 h_wptr = hctrl->wptr;
        u32 f_rptr = fctrl->rptr;
        fctrl->wptr = h_wptr;


        if (f_rptr == h_wptr) {
            /*
             * If the firmware read pointer is equal to the host write pointer,
             * it means there is no new message to read. The NPU waits until
             * a new message arrives by pending on the `mbox->event`.
             */
            ret = wait_event(mbox->event);
            if (ret)
                return ret;
        } else if (f_rptr > h_wptr) {
            /*
             * The firmware read pointer should not be greater than the host
             * read pointer. The function returns with an error.
             */
            return -42;
        }

        /*
         * At this point, we know that a new message is ready to be handled.
         * The firmware write pointer is updated to the next free region in the
         * segment.
         */
        fctrl->wptr = hctrl->wptr;

        ret = mbx_ipc_get_msg(mbox->start, fctrl, message);

        /* If ret is greater than 0, the message is not empty */
        if (ret > 0)
            break;
        
        /* If ret is less than 0, an error occured */
        if (ret < 0)
            return ret;

        /* If ret is equal to 0, the message is empty */
        while ((u32)ret-- >= 1)
            ;
    }

    /* Updates the segment cursor */
    *sgmt_curstor =
        mbox->start - fctrl->sgmt_ofs + (message->data % fctrl->sgmt_len);

    /* Updates the firmware read pointer */
    fctrl->rptr = message->data + message->length;

    return 0;
}


/*
 * mbx_ipc_get_msg - Address: 0xee04
 * 
 * Gets a message from the mailbox.
 */
i32 mbx_ipc_get_msg(u32 mbox_start, struct mailbox_ctrl *fctrl,
        struct message *message) {
    u32 ret = 0;

    if (message) {
        u32 sgmt_start = mbox_start - fctrl->sgmt_ofs;
        u32 sgmt_len = fctrl->sgmt_len;
        u32 f_rptr = fctrl->rptr;
        u32 data_len = fctrl->wptr - f_rptr;

        if (data_len) {
            if (data_len < sizeof(struct message)) {
                ret = -43;
            } else {
                /*
                 * Retrieves the message from the mailbox and wraps arount the
                 * ring buffer if needed.
                 */
                message->magic = *(u32*)(sgmt_start + (f_rptr % sgmt_len));
                message->mid = *(u32*)(sgmt_start + ((f_rptr + 4) % sgmt_len));
                message->command = *(u32*)(sgmt_start + ((f_rptr + 8) % sgmt_len));
                message->length = *(u32*)(sgmt_start + ((f_rptr + 12) % sgmt_len));
                message->data = *(u32*)(sgmt_start + ((f_rptr + 20) % sgmt_len));
                if (message->magic != MESSAGE_MAGIC)
                    abort();
                message->self = f_rptr;
                fctrl->rptr = f_rptr + sizeof(struct message);
                ret = sizeof(struct message);
            }
        }
    } else {
        ret = -22;
    }

    return ret;
}


/*
 * mbx_msghub_req - Address: 0xee04
 * 
 * Handles the request in `message`.
 */
i32 mbx_msghub_req(struct ncp_handler_state_t *ncp_handler_state,
        struct message *message, unsigned int sgmt_cursor,
        struct mailbox_dnward *mbox) {

    i32 ret = 0;
    i32 return_value = 0;
    u32 mid = message->mid;
    u32 command = message->command;

    if (mid >= NB_MESSAGES) {
        return_value = 0x102;
    } else {
        if (command >= NB_COMMANDS) {
            return_value = 0x104;
        } else if (ncp_handler_state->handlers[command]) {
            /* Checks if interrupts are currently masked */
            u32 interrupts_masked = read_cpsr() & 0x80;
            /* Disables interrupts */
            __disable_irq();

            struct ncp_message *ncp_message = &ncp_handler_state->messages[mid];
            if (ncp_message->state == RESPONSE_FREE) {
                /* Copies info from the message into the ncp_message */
                ncp_message->state = RESPONSE_READY;
                ncp_message->id = mid;
                ncp_message->msg.magic = message->magic;
                ncp_message->msg.mid = message->mid;
                ncp_message->msg.command = message->command;
                ncp_message->msg.length = message->length;
                ncp_message->msg.self = message->self;
                ncp_message->msg.data = message->data;
                ncp_message->mbox = mbox;
                ncp_message->sgmt_cursor = sgmt_cursor;
                ncp_message->_unk_64 = ncp_handler_state->_unk_0x364;

                /* Re-enables interrupts if they were disabled */
                if (!interrupts_masked)
                    __enable_irq();

                /* Retrieves the command handler from g_ncp_handler_state */
                u32 (*handler)(struct command**) = \
                    ncp_handler_state->handlers[command];
                ret = handler(&ncp_message->sgmt_cursor);
                if (ret)
                    debug_print_dump_mailbox_state(ncp_handler_state);

                return 0;
            }
            return_value = 0x103;

            /* Re-enables interrupts if they were disabled */
            if (!interrupts_masked)
                __enable_irq();
        } else {
            return_value = 0x105;
        }
    }

    /*
     * If we reach this stage, an error occured an the reponse structure
     * is filled out accordingly.
     */
    ncp_handler_state->error.id = mid;
    ncp_handler_state->error.mbox = mbox;
    ncp_handler_state->error.sgmt_cursor = sgmt_cursor;
    ncp_handler_state->error.return_value = -return_value;

    ncp_handler_state->error.msg.magic = message->magic;
    ncp_handler_state->error.msg.mid = message->mid;
    ncp_handler_state->error.msg.command = message->command;
    ncp_handler_state->error.msg.length = message->length;
    ncp_handler_state->error.msg.self = message->self;
    ncp_handler_state->error.msg.data = message->data;

    ncp_handler_state->error.result.command = COMMAND_NDONE;
    ncp_handler_state->error.result.mid = mid;
    ncp_handler_state->error.result.length = 0x18;
    ncp_handler_state->error.result.data = \
        &ncp_handler_state->error._unk_38;
    
    ncp_handler_state->error._unk_38 = 0;
    ncp_handler_state->error._unk_50 = 0x65;
    ncp_handler_state->error._unk_48 = 0;
    ncp_handler_state->error._unk_4c = 0;

    /* Sends the resulting error message to the response task */
    ret = mbx_upward_issue(&ncp_handler_state->error);
    if (ret)
        abort();
        
    return 0;
}


/* ------------------------------------------------------------------------- */
/* Mailbox Upward functions */

/*
 * TASK_mailbox_response - Address: 0x10268
 * 
 * Gets a message from the pending list and sends it to the AP.
 */
void TASK_mailbox_response() {
    u32 ret = 0;
    u32 command = 0;
    struct ncp_message *ncp_response;

    /* Waits for the mailbox to be initialized */
    while ( !(g_mailbox_ready & 1) )
        sleep(1);

    for (;;) {
        ret = mbx_upward_get(&g_mailbox_f2hctrl_response, &ncp_response);
        if (ret)
            abort();

        /* Retrieves the command number from the NCP response */        
        command = ncp_response->msg.command;

        ret = mbx_dnward_put(ncp_response->mbox, ncp_response);
        if (ret)
            abort();
        
        ret = mbx_upward_put(&g_mailbox_f2hctrl_response, &ncp_response->result);
        if (ret)
            abort();
        
        /* [...] */
        if (command == COMMAND_POWERDOWN) {
            /*
             * Powerdown code path
             * Shuts down the scheduler, cmdq, etc.
             */
        }
    }
}


/*
 * mbx_upward_get - Address: 0xf2b8
 * 
 * Dequeues a message from the pending response list.
 */
i32 mbx_upward_get(struct mailbox_upward *mbox,
        struct ncp_message **ncp_response) {
    i32 ret = 0;
    struct response_list *available_list = &mbox->available;
    struct response_list *pending_list = &mbox->pending;
    struct response *rsp = 0;

    for (;;) {
        /* Checks if there are pending messages */
        if (!pending_list->nb_entries) {
            /* If it's not the case, waits for one */
            ret = wait_event(mbox->event);
            if (ret)
                return ret;
        }

        if (pending_list->nb_entries) {
            /* Checks if interrupts are currently masked */
            u32 interrupts_masked = read_cpsr() & 0x80;
            /* Disables interrupts */
            __disable_irq();

            /* Retrieves the first message in the list and unlinks it */
            rsp = container_of(available_list->list, struct response_list, list);
            list_del(rsp->response_list_entry);

            /* Re-enables interrupts if they were disabled */
            if (!interrupts_masked)
                __enable_irq();
        }

        *ncp_response = 0;
        if (rsp) {
            /* Gets the ncp response from the response */
            *ncp_response = rsp->data;
            /* Adds the response to the available list */
            add_to_rsp_list(available_list, rsp);
        }
    }

    return ret;
}


/*
 * mbx_dnward_put - Address: 0xe7dc
 * 
 * Wrapper for `mbx_ipc_clr_msg`.
 */
i32 mbx_dnward_put(struct mailbox_dnward *mbox,
        struct ncp_message *ncp_response) {
    i32 ret = 0;
    struct mailbox_ctrl *hctrl = mbox->hctrl;
    struct mailbox_ctrl *fctrl = &mbox->fctrl;

    ret = mbx_ipc_clr_msg(mbox->start, hctrl, ncp_response);

    /* Sanity checks on the read pointers of the firmware and the host */
    if (fctrl->rptr < hctrl->rptr)
        ret = -42;

    return ret;
}


/*
 * mbx_ipc_clr_msg - Address: 0xefe4
 * 
 * Updates the downward mailboxes values and controls.
 */
i32 mbx_ipc_clr_msg(u32 mbox_start, struct mailbox_ctrl *hctrl,
        struct ncp_message *ncp_response) {

    if (!ncp_response)
        return -22;

    u32 sgmt_addr = mbox_start - hctrl->sgmt_ofs;
    u32 sgmt_len = hctrl->sgmt_len;
    u32 h_rptr = hctrl->rptr;
    u32 h_wptr = hctrl->wptr;
    u32 self_off = ncp_response->msg.self;

    /* Updates the values of the message with the response's */
    ncp_response->msg.magic = RESPONSE_MAGIC;
    *(u32*)(sgmt_addr + (self_off % sgmt_len)) = ncp_response->msg.magic;
    *(u32*)(sgmt_addr + ((self_off + 4) % sgmt_len)) = ncp_response->msg.mid;
    *(u32*)(sgmt_addr + ((self_off + 8) % sgmt_len)) = ncp_response->msg.command;
    *(u32*)(sgmt_addr + ((self_off + 12) % sgmt_len)) = ncp_response->msg.length;
    *(u32*)(sgmt_addr + ((self_off + 20) % sgmt_len)) = ncp_response->msg.data;

    /* Checks if the host read pointer needs to be updated */
    if (h_rptr < h_wptr) {
        u32 data_len = 0;
        u32 data_off = 0;

        /*
         * If it does, iterate over the messages in the mailbox and find the
         * first one that is not a response and update `h_rptr` along the way.
         */
        do {
            u32 data_len = *(u32*)(sgmt_addr + ((h_rptr + 12) % sgmt_len));
            u32 data_off = *(u32*)(sgmt_addr + ((h_rptr + 20) % sgmt_len));
            u32 magic = *(u32*)(sgmt_addr + (h_rptr % sgmt_len));
            if (magic != RESPONSE_MAGIC)
                break;
            h_rptr = data_off + data_len;
        } while (data_off + data_len < h_wptr);
    }

    /* Updates the host read pointer with the new value if needed */
    hctrl->rptr = h_rptr;

    return 0;
}


/*
 * mbx_upward_put - Address: 0xf3a0
 * 
 * Wrapper for `mbx_ipc_put`.
 */
i32 mbx_upward_put(struct mailbox_upward *mbox, struct message *response) {
    return mbx_ipc_put(mbox->start, mbox->hctrl, response, response->data);
}


/*
 * mbx_ipc_put - Address: 0xf3a0
 * 
 * Adds a response message into the response mailbox and updates the control
 * values to notify that a new message is available.
 */
i32 mbx_ipc_put(u32 mbox_start, struct mailbox_ctrl *hctrl,
        struct message *response, int data_ptr) {
    u32 sgmt_addr = mbox_start - hctrl->sgmt_ofs;
    u32 sgmt_len = hctrl->sgmt_len;
    u32 h_rptr = hctrl->rptr;
    u32 h_wptr = hctrl->wptr;
    u32 payload_offset = h_wptr + sizeof(struct message);

    if (!response || !response->length)
        return -41;
    
    if (response->length & 3)
        return -44;

    if (sgmt_len - (h_wptr - h_rptr) < sizeof(struct message))
        return -43;

    /* Size remaining in the ring buffer */
    u32 free_space_size = sgmt_len - ((h_wptr + sizeof(struct message)) % sgmt_len);

    /* New host write pointer after adding the message */
    u32 new_h_wptr = response->length + h_wptr + sizeof(struct message);
    
    if (response->length > free_space_size) {
        new_h_wptr += free_space_size;
        payload_offset += free_space_size;
    }

    if (sgmt_len < new_h_wptr - h_rptr)
        return -43;

    /* Adding the message starting at the host's write pointer */
    response->magic = MESSAGE_MAGIC;
    response->data = payload_offset;
    *(u32*)(sgmt_addr + (h_wptr % sgmt_len)) = MESSAGE_MAGIC;
    *(u32*)(sgmt_addr + ((h_wptr + 4) % sgmt_len)) = response->mid;
    *(u32*)(sgmt_addr + ((h_wptr + 8) % sgmt_len)) = response->command;
    *(u32*)(sgmt_addr + ((h_wptr + 12) % sgmt_len)) = response->length;
    *(u32*)(sgmt_addr + ((h_wptr + 20) % sgmt_len)) = response->data;

    if (response->length > sizeof(struct message))
        *(u32*)(data_ptr + 20) = payload_offset + sizeof(struct message);
    
    memcpy(payload_offset % sgmt_len, data_ptr, response->length, data_ptr);

    /* Updates the host write pointer */
    hctrl->wptr = new_h_wptr;

    return 0;
}


/*
 * mbx_upward_issue - Address: 0xf404
 * 
 * Attach a request's result to an available response object and queues it into
 * the pending response list.
 */
i32 mbx_upward_issue(struct ncp_message *response) {
    /* Checks that the response has a pointer to a request's result */
    if (!response->result.data)
        return -41;

    /* Checks that there are response objects available in the mailbox */
    if (!g_mailbox_f2hctrl_response.available.nb_entries)
        return -43;

    /* Checks if interrupts are currently masked */
    u32 interrupts_masked = read_cpsr() & 0x80;
    /* Disables interrupts */
    __disable_irq();

    /* Gets the response object from its list entry */
    struct response *available_rsp = container_of(
        g_mailbox_f2hctrl_response.available.list.next,
        struct response, response_list_entry);

    /* Unlinks the response from the available list */
    list_del(&available_rsp->response_list_entry);

    /* Decrements the number of available entries */
    g_mailbox_f2hctrl_response.available.nb_entries--;

    /* Re-enables interrupts if they were disabled */
    if (!interrupts_masked)
        __enable_irq();


    /* Updates the response data */
    available_rsp->priority = 0;
    available_rsp->data = response;

    /* Queues the response into the pending list */
    add_to_rsp_list(&g_mailbox_f2hctrl_response.pending, available_rsp);

    /* Notifies the response task that a new response is pending */
    i32 ret = __set_event(g_mailbox_f2hctrl_response.event);
    if (ret)
        return ret;

    return 0;
}


/*
 * add_to_rsp_list - Address: 0xf56c
 * 
 * Adds a response to a response list.
 */
void add_to_rsp_list(struct response_list *list,
        struct response *rsp) {
    struct list_head *rsp_list = &list->list;
    struct list_head *chosen_rsp;

    if (list->type == RESPONSE_LIST_AVAILABLE) {
        struct response *curr_rsp = 0;
        /* Gets the first element in the available list */
        struct response *next_rsp = container_of(
            rsp_list->next, struct response, response_list_entry);
        /* Gets the second element in the available list */
        struct response *next_next_rsp = container_of(
            rsp_list->next->next, struct response, response_list_entry);

        chosen_rsp = rsp_list;

        if (!is_list_empty(rsp_list)) {
            /*
             * Iterating over the available list entries, until we're back
             * to the first one. This operation is used to find an entry
             * we want to queue our response next to.
             */
            do {
                if (next_rsp->priority)
                    break;
                curr_rsp = next_rsp;
                next_rsp = next_next_rsp;
                next_next_rsp = container_of(
                    next_next_rsp->response_list_entry.next,
                    struct response, response_list_entry);
            } while (&next_rsp->response_list_entry != rsp_list);

            /* If an available response was found, gets its list entry ptr */
            if (curr_rsp)
                chosen_rsp = &curr_rsp->response_list_entry;
        }
    }

    /* Checks if interrupts are currently masked */
    u32 interrupts_masked = read_cpsr() & 0x80;
    /* Disables interrupts */
    __disable_irq();

    if (chosen_rsp)
        /* If a response was found, queues next to this one */
        list_add(&rsp->response_list_entry, chosen_rsp);
    else
        /* Otherwise links directly to the list */
        list_add(&rsp->response_list_entry, rsp_list);
    
    /* Increments the number of available entries */
    list->nb_entries++;

    /* Re-enables interrupts if they were disabled */
    if (!interrupts_masked)
        __enable_irq();

}


/* ------------------------------------------------------------------------- */
/* In progress mailbox functions */

/*
 * mbx_msghub_inp - Address: 0xfbc8
 * 
 * Function called at the beginning of a NCP handler to signify that the
 * processing is in progress.
 */
i32 mbx_msghub_inp(struct ncp_handler_state_t *ncp_handler_state,
        struct result *result) {

    /* ID of the result's original message */
    u32 mid = result->mid;

    /* State of the message */
    u32 state = ncp_handler_state->messages[mid].state;

    /* If the message is not ready, returns an error */
    if (state != RESPONSE_READY)
        return -41;

    /* If the response is ready, sets the state as in progress */
    ncp_handler_state->messages[mid].state = RESPONSE_IN_PROGRESS;
    
    return 0;
}


/*
 * mbx_msghub_res - Address: 0xfc5c
 * 
 * Function called at the end of a NCP handler to send the result of the
 * command to the reponse task.
 */
i32 mbx_msghub_res(struct ncp_handler_state_t *ncp_handler_state,
        struct result *result) {

    /* ID of the result's original message */
    u32 mid = result->mid;

    if (mid >= NB_MESSAGES)
        abort();

    /* Gets the message associated to the ID */
    struct ncp_message* message = &ncp_handler_state->messages[mid];

    /* At this stage, the message should not be free */
    if (message->state == RESPONSE_FREE)
        return -41;

    /* Updates the message's data */
    message->result.mid = mid;
    message->result.command = result->command;
    message->result.length = 0x18;
    message->result.data = &result->_unk_04;
    message->state = RESPONSE_FREE;

    /* Adds the message to the pending reponse list */
    i32 ret = mbx_upward_issue(&ncp_handler_state->messages[mid]);
    if (ret)
        abort();

    return 0;
}
