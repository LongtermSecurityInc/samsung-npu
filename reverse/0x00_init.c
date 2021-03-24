#include "npu.h"


/*
 * reset_handler - Address: 0xE4
 * 
 * First function called by the firmware.
 */
void reset_handler() {
    /* [...] */

    /*
     * CPACR - Architectural Feature Access Control Register
     *
     *     ASEDIS=0b0: This control permits execution of Advanced SIMD
     *                 instructions at PL0 and PL1.
     *     TRCDIS=0b0: This control has no effect on PL0 and PL1 System
     *                 register accesses to trace registers.
     *     cp10=0b11:  Permits full access to the floating-point
     *                 and Advanced SIMD functionality from PL0 and PL1.
     */    
    write_cpacr(0xFFFFFFF);

    /*
     * CPSR - Current Program Status Register
     *
     *     I=0b1: IRQ exceptions are masked.
     */
    write_cpsr(read_cpsr() | 0x80);

    /*
     * SCTLR - System Control Register
     *
     *     I=0b0: Instruction caches disabled.
     *     C=0b0: Data and unified caches disabled.
     *     M=0b0: MPU disabled.
     */
    write_sctlr(read_sctlr() & 0xFFFFEFFA);

    /*
     * CPSR - Current Program Status Register
     *
     *     M=0b0011: Runs in Supervisor mode.
     *     F=0b1:    FIQ exception masked.
     *     I=0b1:    IRQ exceptions are masked.
     */
    write_cpsr(read_cpsr() & 0xFFFFFF00 | 0xD3);

    flush_instruction_cache();

    set_pe_mode_stack_pointer(MODE_ABORT, 0x50037800);
    set_pe_mode_stack_pointer(MODE_FIQ, 0x50036800);
    set_pe_mode_stack_pointer(MODE_IRQ, 0x50035800);
    set_pe_mode_stack_pointer(MODE_SUPERVISOR, 0x50034800);

    /* Initializes the virtual address space for the NPU */
    init_memory_management();

    /*
     * SCTLR - System Control Register
     *
     *     I=0b0: Instruction caches disabled.
     *     C=0b0: Data and unified caches disabled.
     *     M=0b1: MPU enabled.
     */
    write_sctlr(read_sctlr() & 0xEFFFEFFB | 1);

    set_pe_mode_stack_pointer(MODE_ABORT, 0x37800);
    set_pe_mode_stack_pointer(MODE_FIQ, 0x36800);
    set_pe_mode_stack_pointer(MODE_IRQ, 0x35800);
    set_pe_mode_stack_pointer(MODE_SUPERVISOR, 0x34800);

    /*
     * Resets the memory backward from the signature found at the end of the
     * NPU.bin binary.
     */
    if (!initial_bzero) {
        backward_bzero_from_end_of_file(0xE934);
        initial_bzero = 1;
    }

    main();
}


/*
 * init_memory_management - Address: 0x6dc
 * 
 * Configures memory-related registers and initializes the page tables with
 * init_page_tables.
 */
int init_memory_management() {
    /*
     * SCTLR - System Control Register
     *
     *     M=0b0:    MPU disabled.
     *     A=0b0:    Alignment fault checking disabled.
     *     C=0b0:    Data and unified caches disabled.
     *     SW=0b0:   SWP and SWPB are undefined.
     *     Z=0b1:    Program flow prediction enabled.
     *     I=0b0:    Instruction caches disabled.
     *     V=0b0:    Normal exception vectors, base address 0x00000000.
     *     RR=0b0:   Normal cache replacement strategy, for example, random
     *               replacement.
     *     DZ=0b0:   Divide by zero returns the result zero, and no exception is
     *               taken.
     *     FI=0b0:   All Fast Interrupts performance features enabled.
     *     VE=0b0:   Use the FIQ and IRQ vectors from the vector table.
     *     EE=0b0:   value of the CPSR.E bit on entry to an exception vector is
     *               little endian.
     *     NMFI=0b0: Fast interrupts (FIQs) can be masked in the CPSR.
     *     TE=0b0:   Exceptions, including reset, handled in ARM state.
     *     IE=0b0:   Little-endian byte ordering in the instructions.
     */
    write_sctlr(0xC50878);

    /*
     * ACTLR - Auxiliary Control Register
     * 
     * Note: The fields of this register differs based on the ARM-v7
     *       implementation used. Based on the value passed to it, it seems like
     *       the most likely implementation is the Cortex-A5.
     *
     *     SMP=0b1:     Data requests with Inner Cacheable Shared attributes are
     *                  treated as cacheable.
     *     EXCL=0b0:    Exclusive L1/L2 cache control is disabled.
     *     DODMBS=0b0:  Optimized Data Memory Barrier behavior is enabled.
     *     DWBST=0b1:   Data write bursts are disabled.
     *     RADIS=0b0:   Data Cache read-allocate mode is enabled.
     *     L1PCTL=0b11: 3 outstanding L1 prefetches are allowed.
     *     BP=0b00:     Normal branch prediction operation.
     *     RSDIS=0b0:   Return stack operation is enabled.
     *     BTDIS=0b0:   Indirect Branch Target Address Cache is enabled.
     *     DBDI=0b0:    Branch dual issue is enabled.
     */
    write_actlr(0x6041);

    /*
     * CPACR - Architectural Feature Access Control Register
     *
     *     ASEDIS=0b0: This control permits execution of Advanced SIMD
     *                 instructions at PL0 and PL1.
     *     TRCDIS=0b0: This control has no effect on PL0 and PL1 System
     *                 register accesses to trace registers.
     *     cp10=0b00: PL0 and PL1 accesses to floating-point and Advanced SIMD
     *                registers or instructions are UNDEFINED.
     */    
    write_cpacr(0);

    /*
     * SCR - Secure Configuration Register
     *
     *     NS=0b0: Secure mode enabled.
     *     IRQ=0b0: IRQ mode entered when IRQ is taken.
     *     FIQ=0b0: FIQ mode entered when FIQ is taken.
     *     EA=0b0: Abort mode handles external aborts.
     *     FW=0b0: CPSR.F bit can be modified only in Secure state.
     *     AW=0b0: CPSR.A bit can be modified only in Secure state.
     *     nET=0b0: Early termination permitted.
     */ 
    write_scr(0);

    return init_page_tables();
}


/*
 * init_page_tables - Address: 0x11b8
 * 
 * Configures the page tables/memory mappings and the caches.
 */
int init_page_tables() {
    /* [...] */

    SetTransTable(0, 0x50000000, 0x1D000, 0, 0x180D);
    SetTransTable(0x1D000, 0x5001D000, 0x3000, 0, 0x180D);
    SetTransTable(0x20000, 0x50020000, 0xC000, 0, 0x180D);
    SetTransTable(0x2C000, 0x5002C000, 0x4000, 0, 0x180D);
    SetTransTable(0x30000, 0x50030000, 0x1000, 0, 0x180D);
    SetTransTable(0x31000, 0x50031000, 0x2800, 0, 0x1C0D);
    SetTransTable(0x33800, 0x50033800, 0x1000, 0, 0x1C0D);
    SetTransTable(0x34800, 0x50034800, 0x1000, 0, 0x1C0D);
    SetTransTable(0x35800, 0x50035800, 0x1000, 0, 0x1C0D);
    SetTransTable(0x36800, 0x50036800, 0x1000, 0, 0x1C0D);
    SetTransTable(0x37800, 0x50037800, 0x5000, 0, 0x1C0D);
    SetTransTable(0x3C800, 0x5003C800, 0x2B800, 0, 0x1C0D);
    SetTransTable(0x68000, 0x50068000, 0x18000, 0, 0x1C01);
    SetTransTable(0x80000, 0x50080000, 0x60000, 0, 0x1C0D);
    SetTransTable(0x10000000, 0x10000000, 0x10000000, 0, 0x816);
    SetTransTable(0x40100000, 0x40100000, 0x100000, 0, 0xC16);
    SetTransTable(0x40300000, 0x40300000, 0x100000, 0, 0x1C0E);
    SetTransTable(0x40600000, 0x40600000, 0x100000, 0, 0x1C12);
    SetTransTable(0x40200000, 0x40200000, 0x100000, 0, 0xC16);
    SetTransTable(0x40300000, 0x40300000, 0x100000, 0, 0x1C0E);
    SetTransTable(0x40700000, 0x40700000, 0x100000, 0, 0x1C12);
    SetTransTable(0x50100000, 0x50100000, 0x200000, 0, 0x1C02);
    SetTransTable(0x50000000, 0x50000000, 0xE0000, 0, 0x1C02);
    SetTransTable(0x40400000, 0x40400000, 0x100000, 0, 0x1C02);
    SetTransTable(0x40000000, 0x40000000, 0x100000, 0, 0xC16);
    SetTransTable(0x80000000, 0x80000000, 0x60000000, 0, 0x1C02);

    /*
     * TTBCR - Translation Table Base Control Register
     * 
     *     N=0b000: Boundary size of TTBR0 is 16KB (always use TTBR0 since N=0).
     *     PD0=0b0: Processor performs a translation table walk on a TLB miss 
     *              when using TTBR0.
     *     PD1=0b0: Processor performs a translation table walk on a TLB miss 
     *              when using TTBR1.
     */
    write_ttbcr(0);

    /* Writes the L1 page table address to TTBR0 */
    write_ttbr0_el1(L1_TABLE_ADDR);

    /*
     * DACR - Domain Access Control Register
     * 
     *     D<n>=0b01: Client. Accesses are checked against the access 
     *                permission bits in the TLB entry.
     */
    write_dacr(0x55555555);

    clean_cache_by_va(L1_TABLE_ADDR, L1_TABLE_SIZE);
    clean_cache_by_va(L2_TABLE_ADDR, L2_TABLE_SIZE);

    tlb_invalidate_all();
    branch_predictor_invalidate_all();
    instruction_cache_invalidate_all();

    int clean_caches = 0;
    int invalidate_caches = 1;
    init_caches(clean_caches, invalidate_caches);

    return 0;
}


/*
 * SetTransTable - Address: 0xa5c
 * 
 * Creates a page table entry.
 */
void SetTransTable(u32 virt_addr, u32 phys_addr, u32 size, u32 l2_pt_idx,
    u32 perms) {
    /* [...] */

    /*
     * `l1_idx` is the index of the first entry in the L1 translation
     * table that corresponds to the `virt_addr` memory mapping. 
     * `l1_idx_end` is the index of the last entry in the L1 translation
     * table that corresponds to the `virt_addr` memory mapping. 
     */
    l1_entry_idx = virt_addr >> 20;
    l1_idx_end = (virt_addr + size) >> 20;
    if ( size << 12 )
        ++l1_idx_end;

    /*
     * `use_short_descriptors` determines whether the memory mapping will use:
     *   - short descriptors and insert entries in the L1 and L2 translation
     *     tables
     *   - section descriptors and only insert entries in the L1 translation
     *     table
     * Samsung uses the least significant bit of their `perms` arguments to
     * encode this information.
     */
    use_short_descriptors = perms & 1;
    if (use_short_descriptors)
    {
        /*
         * There are only four L2 translation tables. `l2_pt_idx` is a
         * parameter that can be used to select which one to use when doing a
         * memory mapping. However, it's not used in practice and kept to 0
         * while letting the rest of the code figure out which one to select.
         */
        l2_entry_addr = ((l2_pt_idx << 10) + L2_TABLE_ADDR) & 0xFFFFFC00;

        /* Sanity checks [...] */

        while (l1_idx < l1_idx_end) {
            /*
             * `l1_descriptor` is the value of the descriptor that will be added
             * to the L1 translation table and that will point to the L2
             * translation table. The NS and PXN bits are set to 0 by default
             * for short descriptor entries.
             */
            l1_descriptor = l2_entry_addr & 0xFFFFFC00 | 1;
            *(u32 *)(L1_TABLE_ADDR + 4*l1_idx) = l1_descriptor;

            /*
            * `l2_idx` is the index of the first entry in the L2 translation
            * table that corresponds to the `virt_addr` memory mapping. 
            * `l2_idx_end` is the index of the last entry in the L2 translation
            * table that corresponds to the `virt_addr` memory mapping.
            */
            l2_idx = (virt_addr >> 12) & 0xFF;
            l2_idx_end = l2_entry_idx + (size >> 12);
            if ( size << 20 )
                ++l2_entry_idx_end;

            /*
            * An L2 translation table can only store 0x100 entries. If the index
            * is bigger than 0xFF it means we are trying to map a memory range
            * larger than 0x100000.
            * In that case, 0x100000 are mapped and on the next loop iteration
            * the remaining size is mapped.
            * Ex.: If we want to map 0x123000 bytes starting from 0x200000,
            *      we first map 0x100000 bytes at 0x200000 and then
            *      0x23000 bytes from 0x300000.
            */
            if ( l2_idx_end > 0xFF )
            {
                virt_addr += (0x100 - l2_idx) << 12;
                size += (l2_idx - 0x100) << 12
                l2_idx_end = 0xFF;
            }

            while (l2_idx < l2_idx_end) {
                /*
                 * Permissions passed to `SetTransTable` are by default for 
                 * section descriptors and need therefore to be remapped to the
                 * appropriate format used by short descriptors.
                 */
                permissions = 2
                    | (perms >> 6) & 0x30   /* AP[0:1] */
                    | (perms >> 6) & 0x1C0  /* TEX[0:2] */
                    | (perms >> 6) & 0x200  /* AP[0:2] */
                    | (perms >> 6) & 0x400  /* S */
                    | (perms >> 6) & 0x800  /* nG */
                    | (perms >> 4) & 1;     /* XN */
                    | perms & 0xC           /* C, B */
                l2_descriptor = phys_addr & 0xFFFFF000 | permissions;

                *(u32 *)(l2_entry_addr + 4*l2_idx) = l2_descriptor;

                phys_addr += 0x1000;
                l2_idx++;
            }
            l1_idx++;
            l2_entry_addr += 0x400;
        }
    } else { /* Mapping Sections */
        l1_descriptor = phys_addr & 0xFFF00000 | perms;
        *(u32 *)(L1_TABLE_ADDR + 4*l1_idx) = l1_descriptor;
        ++l1_idx;
        phys_addr += 0x100000;
    }
}
