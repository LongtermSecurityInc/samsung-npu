#include "npu.h"


/*
 * timer_irq_handler - Address: 0x334
 * 
 * Handler for the timer interrupt.
 */
void timer_irq_handler() {
    time_tick();
    if ( !g_scheduler_state.scheduler_stopped )
        schedule_tick();
}


/*
 * time_tick - Address: 0xb430
 * 
 * Updates the tick counts and checks if timers have expired.
 */
void time_tick()
{
    ++g_ticks;
    
    /* unused */
}
