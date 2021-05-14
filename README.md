# Reversing and Exploiting Samsung's Neural Processing Unit

Companion repository for the [*Reversing and Exploiting Samsung's Neural Processing Unit*](https://www.longterm.io/samsung_npu.html) article on [Longterm Security](https://longterm.io/)'s blog.

## Repository

This repository is organised as follows:

* `binaries/`
    * `npu_sXX_binary.bin`
        * NPU binaries found in the firmwares of the Samsung Galaxy S20 ([`G980FXXS5CTL5`](https://www.sammobile.com/samsung/galaxy-s20/firmware/SM-G980F/XEF/download/G980FXXS5CTL5/1117440/)) and S10 ([`G970FXXS9DTK9`](https://www.sammobile.com/samsung/galaxy-s10e/firmware/SM-G970F/XEF/download/G970FXXS9DTK9/845938/)).
    * `npu_sXX_dump.bin`
        * Dumps of the NPU firmwares from running phones. These are the binaries we used in the blogpost to reverse engineer the NPU.
* `patches/`
    * Kernel patch to re-enable memory dumps of the NPU from the kernel. The Samsung kernel version we used as a base is `G980FXXU5CTL1`.
* `reverse/`
    * Files containing our reverse engineered comprehension of the NPU. They detail:
        * the initialization of the NPU;
        * components such as the heap, events, semaphores, timers, events, etc.;
        * tasks and the scheduling algorithm;
        * the implementation of the mailbox used to communicate between the NPU and the kernel.
* `tools/`
    * `npu_sram_dumper`
        * Tool to dump the NPU from a running phone.
    * `npu_firmware_extractor`
        * Tool to extract the NPU firmware from a boot image.

## Reference

* Reversing and Exploiting Samsung's Neural Processing Unit - *Longterm Security*
    * [https://www.longterm.io/samsung_npu.html](https://www.longterm.io/samsung_npu.html)
