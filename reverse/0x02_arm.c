#include "npu.h"


/*
 * arm_init - Address: 0x6fc
 * 
 * Main ARM CPU settings initialization function.
 */
void arm_init() {
    /* Unknown mapped device */
    *(u32*)(INTR_DEVICE_ADDR + 0x10080) = -1;
    *(u32*)(INTR_DEVICE_ADDR + 0x10084) = -1;
    *(u32*)(INTR_DEVICE_ADDR + 0x10088) = -1;
    *(u32*)(INTR_DEVICE_ADDR + 0x1008c) = -1;
    *(u32*)(INTR_DEVICE_ADDR + 0x10098) = -1;

    init_caches();
    init_exception();
    __disable_irq();
    init_interrupt();
    __enable_irq();
}


/*
 * init_caches - Address: 0x1508
 * 
 * CPU caches initialization wrapper.
 */
void init_caches() {
    instruction_synchronization_barrier(FULL_SYSTEM);
    data_synchronization_barrier(FULL_SYSTEM);

    /* 
     * Reads CTR to retrieve DMinLine, the log2 of the number of words in the
     * smallest cache line of all the data and unified caches that the core
     * controls.
     */
    u32 dminline_log2 = (read_ctr() >> 16) & 0xf;
    u32 dminline = 1 << dminline_log2;
    /*
     * g_data_cache_line_length is used in certain functions of the NPU to
     * invalidate data and instruction caches by VA.
     */
    g_data_cache_line_length = 4 * dminline;

    do_init_caches(1, 1, get_dcache_enable());
}


/*
 * do_init_caches - Address: 0xee4
 * 
 * Main CPU caches configuration function.
 */
void do_init_caches(int enable_mpu, int enable_icache, int enable_dcache) {
    /*
     * DACR - Domain Access Control Register
     * 
     *     D<n>=0b01: Client. Accesses are checked against the access 
     *                permission bits in the TLB entry.
     */
    write_dacr(0x55555555);

    /* Clean page table caches by VA */
    clean_cache_by_va(L1_TABLE_ADDR, L1_TABLE_SIZE);
    clean_cache_by_va(L2_TABLE_ADDR, L2_TABLE_SIZE);

    /* Various cache cleaning/invalidation operations */
    data_synchronization_barrier(OUTER_SHAREABLE);
    tlb_invalidate_all();
    branch_predictor_invalidate_all();
    data_synchronization_barrier(OUTER_SHAREABLE_WAIT_STORES);
    instruction_synchronization_barrier(OUTER_SHAREABLE);
    instruction_cache_invalidate_all();
    instruction_synchronization_barrier(FULL_SYSTEM);
    data_synchronization_barrier(FULL_SYSTEM);

    /* MPU, instruction caches and data caches configuration */
    u32 sctlr = read_sctlr();
    sctlr = (enable_mpu) ? sctlr | 1 : sctlr & 0xFFFFFFFE;
    sctlr = (enable_icache) ? sctlr | 0x1000 : sctlr & 0xFFFFEFFF;
    sctlr = (enable_dcache) ? sctlr | 4 : sctlr & 0xFFFFFFFB;

    /*
     * SCTLR - System Control Register
     *
     *     I=enable_icache
     *     C=enable_dcache
     *     M=enable_mpu.
     */
    write_sctlr(sctlr);
}


/*
 * init_exception - Address: 0xa0c
 * 
 * Sets the function pointers for exception handlers.
 */
int init_exception() {
    g_undefined_instruction_fptr = undefined_instruction_handler;
    g_prefetch_abort_fptr = prefetch_abort_handler;
    g_data_abort_fptr = data_abort_handler;
    g_irq_fiq_fptr = irq_fiq_handler;
}


/*
 * init_interrupt - Address: 0x1558
 * 
 * Initializes the interrupt for the NPU by configuring the Global Interrupt
 * Controller.
 */
int init_interrupt() {
    /*
     * MPIDR - Multiprocessor Affinity Register
     *
     *     Aff0: Affinity level 0. Lowest level affinity field.
     *           Indicates the core number in the processor.
     */
    g_current_core = read_mpidr() & 3;

    /*
     * GICD_ITARGETSRn - Interrupt Processor Targets Registers
     *
     *     Resets the pending interrupts for CPU `g_current_core`
     */
    for (int i = 0; i < 0x400; i += 4) {
        for (int j = 0; j < 4; j++) {
            *(u32*)(GICD_ITARGETSRn + i) &= \
                ~((1 << g_current_core) << (j * 8));
        }
    }

    /*
     * GICC_CTLR - CPU Interface Control Register
     *
     *     EnableGrp0=0b1: Enables signaling of Group 0 interrupts by
     *                     the CPU interface to the connected processor:
     *     EnableGrp1=0b1: Enables signaling of Group 1 interrupts by
     *                     the CPU interface to the connected processor:
     *     EnableGrp1=0b1: 
     *     AckCtl=0b1:     
     */
    *(u32*)(GICC_CTLR) = 7;
   
    /*
     * GICC_PMR - Interrupt Priority Mask Register
     *
     *     Enable=0b111111111: Provides an interrupt priority filter. Only
     *                         interrupts with higher priority than the value
     *                         in this register are signaled to the processor.
     */ 
    *(u32*)(GICC_PMR) = 0xff;

    /*
     * GICD_CTLR - Distributor Control Register
     *
     *     EnableGrp0=0b1: Enables forwarding pending Group 0 interrupts from
     *                     the Distributor to the CPU interfaces
     *     EnableGrp1=0b1: Enables forwarding pending Group 1 interrupts from
     *                     the Distributor to the CPU interfaces
     */ 
    irq_lock(GIC_LOCK_ADDR);
    *(u32*)(GICD_CTLR) = 3;
    irq_release(GIC_LOCK_ADDR);
}


/*
 * handle_isr_func - Address: 0x468
 * 
 * Registers an interrupt handler and enables it in the GIC.
 */
void request_irq(u32 interrupt_number, u32 unk, void (*handler)(void)) {
    if ( interrupt_number >= 0x200 )
        abort();

    /* Adds the interrupt handler to the local interrupt vector table */
    g_interrupt_vector[interrupt_number]._unk_00 = unk;
    g_interrupt_vector[interrupt_number].handler = handler;
    /* Configures the GIC to take the new interrupt into account */
    config_interrupt(interrupt_number);
    /* Enables the interrupt */
    set_enable_interrupt(interrupt_number);
}


/*
 * set_enable_interrupt - Address: 0x1750
 * 
 * Redirects an interupt from the distributor to the CPU interfaces.
 */
void set_enable_interrupt(u32 interrupt_number) {
    u32 table_offset = 4 * (interrupt_number >> 5);
    u32 interrupt_offset = interrupt_number & 0x1F;

    /*
     * GICD_ISENABLERn - Interrupt Set-Enable Registers
     *
     *     The GICD_ISENABLERs provide a Set-enable bit for each interrupt
     *     supported by the GIC. Writing 1 to a Set-enable bit enables
     *     forwarding of the corresponding interrupt from the Distributor to
     *     the CPU interfaces.
     */ 
    irq_lock(GIC_LOCK_ADDR);
    *(u32*)(GICD_ISENABLERn + table_offset) = 1 << interrupt_offset;
    irq_release(GIC_LOCK_ADDR);
}


/*
 * config_interrupt - Address: 0x1620
 * 
 * Modifies GIC registers to enable and configure a new interrupt.
 */
void config_interrupt(u32 interrupt_number) {
    u32 is_edge_triggered = 0;
    u32 interrupt_priority = 0;

    /*
     * Retrieves the interrupt priority and whether it should be configured as
     * edge-triggered or level-sensitive. The process is a bit more complicated
     * in assembly, than a simple call to a function, but it's not particularily
     * interesting and can be omitted.
     */
    search_interrupt_config_by_id(interrupt_number, &is_edge_triggered,
        &interrupt_priority);

    irq_lock(GIC_LOCK_ADDR);
    
    u32 interrupt_offset = (8 * interrupt_number) & 0x1F;
    u32 *pending_interrupt = GICD_ITARGETSRn + (interrupt_number >> 2) << 2;

    /* Clears pending interrupts of ID `interrupt_number` */
    *pending_interrupt &= ~(0xff << interrupt_offset);
    /* Sends pending `interrupt_number` to CPU `g_current_core` */
    *pending_interrupt |= ((1 << g_current_core) & 0xFF) << interrupt_offset;

    /* Assigns the interrupt to group 0 */
    *(u32*)(GICD_IGROUPRn + 4 * (interrupt_number >> 5)) &= \
        ~(1 << (interrupt_number & 0x1F));

    /*
     * GICD_ICFGRn - Interrupt Set-Enable Registers
     *
     *     The GICD_ICFGRs provide a 2-bit Int_config field for each interrupt
     *     supported by the GIC. This field identifies whether the corresponding
     *     interrupt is edge-triggered or level-sensitive.
     */ 
    u32 interrupt_config = 2 << (2 * (interrupt_number & 0xF));
    if (is_edge_triggered)
        *(u32*)(GICD_ICFGRn + 4 * (interrupt_number >> 4)) |= interrupt_config;
    else
        *(u32*)(GICD_ICFGRn + 4 * (interrupt_number >> 4)) &= ~interrupt_config;

    /* Clears interrupt priority for interupt `interrupt_number` */
    *pending_interrupt &= ~(0xff << interrupt_offset);
    /* Sets interrupt priority for interupt `interrupt_number` */
    *pending_interrupt |= (interrupt_priority << interrupt_offset;

    irq_release(GIC_LOCK_ADDR);
}


/*
 * irq_fiq_handler - Address: 0x3a4
 * 
 * Handler for IRQ and FIQ exceptions.
 */
void irq_fiq_handler() {
    asm volatile(
        "srsdb sp!, 0x13\n"
        "cps 0x13\n"
        "cps 0x12\n"
    );

    g_do_schedule = 0;
    handle_isr_func();
    if ( g_do_schedule == 1 )
        schedule_interrupt();

    asm volatile(
        "cps #0x13\n"
        "rfefd sp!, #0x13\n"
    );
}


/*
 * handle_isr_func - Address: 0x468
 * 
 * Redirects to a specific interrupt handler based on the interrupt number.
 */
void handle_isr_func() {
    /*
     * GICC_IAR - Interrupt Acknowledge Register
     *
     *     Interrupt ID [9:0]: Interrupt identifier
     */ 
    u32 interrupt_number = *(u32*)(GICC_IAR) & 0x3ff;
    if (interrupt_number == 0x3ff)
        return;

    /* Gets the interrupt handler using the interrupt number */
    void (*interrupt_handler)(void) = \
        g_interrupt_vector[interrupt_number].handler;
    if (!interrupt_handler)
        return;

    /* Calls the interrupt handler */
    interrupt_handler();

    /*
     * GICC_EOIR - End of Interrupt
     *
     *     This write-only register is used when the software finishes handling
     *     an interrupt. The End of Interrupt Register has the same format as
     *     the Interrupt Acknowledge Register
     */ 
    *(u32*)(GICC_EOIR) = interrupt_number;
}
