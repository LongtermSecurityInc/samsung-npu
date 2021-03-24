#include "npu.h"


/*
 * run_native_tasks - Address: 0x17594
 * 
 * Initializes and schedules the native tasks.
 */
int run_native_tasks(u32 base_stack_addr) {
    u32 ret = 0;
    u32 total_task_region_size = 0;

    /* Monitor task */
    snprintf(g_native_tasks[0].name, 6, "__MON");
    g_native_tasks[0].priority = 0;
    g_native_tasks[0].handler = TASK_monitor;
    g_native_tasks[0].max_sched_slices = 0x64;
    g_native_tasks[0].stack_size = 0x400;

    /* Idle task */
    snprintf(g_native_tasks[1].name, 6, "_IDLE");
    g_native_tasks[1]._unk_00[8] = 1;
    g_native_tasks[1].priority = 0xFF;
    g_native_tasks[1].handler = TASK_idle;
    g_native_tasks[1].max_sched_slices = 0x64;
    g_native_tasks[1].stack_size = 0x200;

    /* Low priority mailbox task */
    snprintf(g_native_tasks[2].name, 6, "__LOW");
    g_native_tasks[2]._unk_00[8] = 1;
    g_native_tasks[2].priority = 0x14;
    g_native_tasks[2].handler = TASK_mailbox_lowpriority;
    g_native_tasks[2].max_sched_slices = 0x64;
    g_native_tasks[2].stack_size = 0x800;

    /* High priority mailbox task */
    snprintf(g_native_tasks[3].name, 6, "_HIGH");
    g_native_tasks[3]._unk_00[8] = 1;
    g_native_tasks[3].priority = 0xA;
    g_native_tasks[3].handler = TASK_mailbox_highpriority;
    g_native_tasks[3].max_sched_slices = 0x64;
    g_native_tasks[3].stack_size = 0x400;

    /* Mailbox response task */
    snprintf(g_native_tasks[4].name, 6, "_RSPS");
    g_native_tasks[4]._unk_00[8] = 1;
    g_native_tasks[4].priority = 9;
    g_native_tasks[4].handler = TASK_mailbox_response;
    g_native_tasks[4].max_sched_slices = 0x64;
    g_native_tasks[4].stack_size = 0x400;

    /* Mailbox report task */
    snprintf(g_native_tasks[5].name, 6, "__RPT");
    g_native_tasks[5]._unk_00[8] = 1;
    g_native_tasks[5].priority = 0x14;
    g_native_tasks[5].handler = TASK_mailbox_report;
    g_native_tasks[5].max_sched_slices = 0x64;
    g_native_tasks[5].stack_size = 0x400;

    /* Imm dispatcher task */
    snprintf(g_native_tasks[6].name, 6, "__IMM");
    g_native_tasks[6]._unk_00[8] = 1;
    g_native_tasks[6].priority = 0xE;
    g_native_tasks[6].handler = TASK_dispatcher_imm;
    g_native_tasks[6].max_sched_slices = 0x64;
    g_native_tasks[6].stack_size = 0x800;

    /* Batch dispatcher task */
    snprintf(g_native_tasks[7].name, 6, "__BAT");
    g_native_tasks[7]._unk_00[8] = 1;
    g_native_tasks[7].priority = 0xF;
    g_native_tasks[7].handler = TASK_dispatcher_bat;
    g_native_tasks[7].max_sched_slices = 0x64;
    g_native_tasks[7].stack_size = 0x800;

    /* Instance task 0 */
    snprintf(g_native_tasks[8].name, 6, "JOBQ0");
    g_native_tasks[8]._unk_00[8] = 1;
    g_native_tasks[8].priority = 0x15;
    g_native_tasks[8].handler = TASK_instance;
    g_native_tasks[8].stack_size = 0x1000;
    g_native_tasks[8].max_sched_slices = 0x64;

    /* Instance task 1 */
    snprintf(g_native_tasks[9].name, 6, "JOBQ1");
    g_native_tasks[9]._unk_00[8] = 1;
    g_native_tasks[9].priority = 0x16;
    g_native_tasks[9].handler = TASK_instance;
    g_native_tasks[9].max_sched_slices = 0x64;
    g_native_tasks[9].stack_size = 0x200;

    /* Instance task 2 */
    snprintf(g_native_tasks[0xA].name, 6, "JOBQ2");
    g_native_tasks[0xA]._unk_00[8] = 1;
    g_native_tasks[0xA].priority = 0x17;
    g_native_tasks[0xA].handler = TASK_instance;
    g_native_tasks[0xA].max_sched_slices = 0x64;
    g_native_tasks[0xA].stack_size = 0x200;

    /* Sums all the stack size and makes sure it's not too big */
    for (int i = 0; i < NB_NATIVE_TASKS; i++)
        total_task_region_size += g_native_tasks[i].stack_size;
    if (total_task_region_size > 0x5000)
        abort();

    /*
     * Initializes the stack base addresses of all tasks and write the values
     * into the first dwords of the monitor task structure.
     */
    g_native_tasks[0]._unk_00[i] = base_stack_addr;
    for (int i = 0; i < NB_NATIVE_TASKS; i++) {
        g_native_tasks[0]._unk_00[i+1] = base_stack_addr;
        struct native_task *native_task = &g_native_tasks[i];
        native_task->id = i;
        native_task->stack_addr = base_stack_addr;
        base_stack_addr += native_task->stack_size;
    }

    /* Iterates over the native tasks */
    for (int i = 0; i < NB_NATIVE_TASKS; i++) {
        struct native_task *native_task = &g_native_tasks[i];
        /* Creates the actual task associated to a native task */
        ret = create_task(
            &native_task->task,
            native_task->handler,
            native_task->priority,
            native_task->name,
            native_task->max_sched_slices,
            native_task->stack_addr,
            native_task->stack_size
        );
        if (ret)
            return ret;

        /* Checks if the value in the next task is set (?) */
        if (g_native_tasks[i+1]._unk_00[8]) {
            /*
             * __resume_task adds the function to the ready list, but does not
             * start it yet because the scheduler is still stopped at this point
             * and will only be started when `schedule_start` is called
             */
            ret = __resume_task(native_task->task);
            if (ret)
                return ret;
        }
    }

    /*
     * Finally starts the scheduler that will run all the native tasks and
     * effectively mark the start of the system
     */
    ret = schedule_start();
    if (ret)
        return ret;
}
