#define L1_TABLE_ADDR 0x5002C000
#define L1_TABLE_SIZE 0x4000
#define L2_TABLE_ADDR 0x50030000
#define L2_TABLE_SIZE 0x1000

#define MINIMUM_CHUNK_SIZE 8


/* -------------------------------------------------------------------------- */
/* Basic types & structures */

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef char i8;
typedef short i16;
typedef int i32;

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

struct workqueue {
    u32 task;
    struct list_head head;
    u32 service;
    void* obj;
};


/* -------------------------------------------------------------------------- */
/* Heap */

#define HEAP_START_ADDR 0x80000
#define HEAP_END_ADDR 0xE0000

struct heap_chunk {
    u32 size;
    struct heap_chunk *next;
};

struct heap_state {
    u32 _unk_00;
    u32 _unk_04;
    struct heap_chunk *freelist;
    u32 _unk_0c;
    u32 _unk_10;
    u32 _unk_14;
    u32 _unk_18;
    u32 _unk_1c;
};


/* -------------------------------------------------------------------------- */
/* Caches, Interrupts & Exceptions */

#define GIC_LOCK_ADDR 0x4000207C

#define GIC_DISTRIBUTOR_REG 0x40019000
#define GICD_CTLR        (GIC_DISTRIBUTOR_REG + 0x000)
#define GICD_IGROUPRn    (GIC_DISTRIBUTOR_REG + 0x080)
#define GICD_ISENABLERn  (GIC_DISTRIBUTOR_REG + 0x100)
#define GICD_IPRIORITYRn (GIC_DISTRIBUTOR_REG + 0x400)
#define GICD_ITARGETSRn  (GIC_DISTRIBUTOR_REG + 0x800)
#define GICD_ICFGRn      (GIC_DISTRIBUTOR_REG + 0xC00)

#define GIC_CPU_IFACE_REG 0x4001A000
#define GICC_CTLR (GIC_CPU_IFACE_REG + 0x00)
#define GICC_PMR  (GIC_CPU_IFACE_REG + 0x04)
#define GICC_IAR  (GIC_CPU_IFACE_REG + 0x0c)
#define GICC_EOIR (GIC_CPU_IFACE_REG + 0x10)

u8 g_data_cache_line_length;
u8 g_do_schedule;
u8 g_current_core;

struct interrupt_vector_entry {
    u32 _unk_00;
    void (*handler)(void);
};

#define MAX_INTR 0x200
struct interrupt_vector_entry g_interrupt_vector[MAX_INTR];


/* -------------------------------------------------------------------------- */
/* Timers */

struct timer {
    u32 _unk_00;
    u32 _unk_04;
    u32 _unk_08;
    u32 _unk_0c;
    u32 _unk_10;
    u32 _unk_14;
    u32 _unk_18;
    u32 _unk_1c;
    struct list_head _unk_20;
    u32 _unk_28;
    u32 _unk_2c;
};

struct timers_state_t {
    u32 _unk_00;
    struct list_head _unk_04;
    u32 _unk_0c;
    struct list_head _unk_10;
};

u32 g_ticks;
struct timers_state_t g_timers_state;


/* -------------------------------------------------------------------------- */
/* Events */

#define MAX_EVENT_IDS 0x96
#define MAX_EVENTS 0x3c
#define EVENT_MAGIC 0xF3CD03ED

enum event_state {
    EVENT_READY = 1,
    EVENT_WAITING = 2,
};

struct event {
    u32 magic;
    u32 id;
    u32 _unk_0c;
    u32 state;
    struct list_head event_list_entry;
    struct list_head waiting_list_entry;
    struct workqueue wq;
};

struct events_state_t {
    u32 event_flags[MAX_EVENT_IDS + 1];
    struct list_head event_waiting_lists[MAX_EVENT_IDS + 1];
    u32 standby_cnts[MAX_EVENT_IDS + 1];
    u32 trigger_cnts[MAX_EVENT_IDS + 1];
    u32 nb_events;
    struct list_head event_list;
    struct event events[MAX_EVENTS];
};

struct events_state_t g_events_state;


/* -------------------------------------------------------------------------- */
/* Semaphores */

#define SEM_MAGIC 0xF3CD03E3

struct semaphores_state_t {
    u32 nb_semaphores;
    struct list_head semaphore_list;
};

struct semaphore {
    u32 magic;
    u32 count;
    u32 name;
    struct list_head entry;
    struct workqueue wq;
};

struct semaphores_state_t g_semaphores_state;


/* -------------------------------------------------------------------------- */
/* Tasks */

#define TASK_MAGIC 0xC0FFEE0
#define TASK_MAX_PRIORITY 0x100

enum task_state {
    TASK_SUSPENDED = 1,
    TASK_READY = 2,
    TASK_SLEEPING = 4,
    TASK_PENDING = 8,
    TASK_NOT_READY = 16,
    TASK_RUNNING = 32,
};

struct task {
    u32 magic;
    void *stack_ptr;
    void *stack_start;
    void *stack_end;
    u32 stack_size;
    u32 state;
    u32 _unk_18;
    u32 priority;
    void (*handler)(void *);
    u32 max_sched_slices;
    u32 total_sched_slices;
    u32 remaining_sched_slices;
    i32 delay;
    void *args;
    char *name;
    struct list_head tasks_list_entry;
    struct list_head ready_list_entry;
    struct list_head delayed_list_entry;
    struct list_head pending_list_entry;
    struct workqueue* wait_queue;
    char _unk_60[60];
};

struct task *g_current_task;
struct task *g_task_to_schedule;


/* -------------------------------------------------------------------------- */
/* Scheduling */

struct scheduler_state_t {
    u32 scheduler_stopped;
    u32 forbid_scheduling;
    u8 prio_grp1[4];
    u8 prio_grp2[4][8];
    u8 prio_grp0;
    struct list_head tasks_list;
    struct list_head delayed_list;
    struct list_head ready_list[TASK_MAX_PRIORITY];
    u32 _unk_840;
    u32 nb_tasks;
    u32 count_sched_slices;
};

struct scheduler_state_t g_scheduler_state;

u8 g_priority_table[] = {
    0,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
    4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
    5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
    4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
    6,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
    4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
    5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
    4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
    7,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
    4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
    5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
    4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
    6,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
    4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
    5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
    4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,
};


/* -------------------------------------------------------------------------- */
/* Comms */

struct event* g_mailbox_low_prio_event;
struct event* g_mailbox_high_prio_event;

struct cmdq_info {
    u32 _unk_00;
    u32 _unk_04;
    u32 done_handler_intr_0;
    u32 process_handler_intr_0;
    u32 err_handler_intr_0;
    u32 _unk_14;
    u32 done_handler_intr_1;
    u32 process_handler_intr_1;
    u32 err_handler_intr_1;
    u32 event_id_0;
    u32 event_id_1;
    u32 _unk_2c;
    u32 _unk_30;
    u32 _unk_34;
    struct event *event0;
    struct event *event1;
};

// struct cmdq_info g_cmdq_info[2] = {
//     {
//         ._unk_00 = 0x401E0000,
//         ._unk_04 = 0x75,
//         .done_handler_intr_0 = 0x87,
//         .process_handler_intr_0 = 0x88,
//         .err_handler_intr_0 = 0x76,
//         ._unk_04 = 0x165,
//         .done_handler_intr_1 = 0x177,
//         .process_handler_intr_1 = 0x178,
//         .err_handler_intr_1 = 0x166,
//         .event_id_0 = 0x2f,
//         .event_id_1 = 0x30,
//     },
//     {
//         ._unk_00 = 0x402E0000,
//         ._unk_04 = 0xc5,
//         .done_handler_intr_0 = 0xd7,
//         .process_handler_intr_0 = 0xd8,
//         .err_handler_intr_0 = 0xc6,
//         ._unk_04 = 0x185,
//         .done_handler_intr_1 = 0x1c7,
//         .process_handler_intr_1 = 0x1c8,
//         .err_handler_intr_1 = 0x1b6,
//         .event_id_0 = 0x7a,
//         .event_id_1 = 0x7b,
//     },
// };


/* -------------------------------------------------------------------------- */
/* Native tasks */

#define NB_NATIVE_TASKS 0xb

struct native_task {
	u32 _unk_00[0xc];
	u32 id;
	char name[8];
	u32 priority;
	u32 handler;
	u32 max_sched_slices;
	u32 stack_addr;
	u32 stack_size;
	struct task task;
	u32 _unk_ec;
};

struct native_task g_native_tasks[NB_NATIVE_TASKS];


/* -------------------------------------------------------------------------- */
/* Mailbox */

#define MAILBOX_VERSION		    4
#define MAILBOX_SIGNATURE1		0x0C0FFEE0
#define MAILBOX_SIGNATURE2		0xC0DEC0DE
#define MAILBOX_SHARED_MAX		64

#define MAILBOX_H2FCTRL_LPRIORITY	0
#define MAILBOX_H2FCTRL_HPRIORITY	1
#define MAILBOX_H2FCTRL_MAX		    2

#define MAILBOX_F2HCTRL_RESPONSE	0
#define MAILBOX_F2HCTRL_REPORT		1
#define MAILBOX_F2HCTRL_MAX		    2

#define MAILBOX_GROUP_LPRIORITY	    0
#define MAILBOX_GROUP_HPRIORITY	    2
#define MAILBOX_GROUP_RESPONSE		3
#define MAILBOX_GROUP_REPORT		4
#define MAILBOX_GROUP_MAX		    5


struct mailbox_ctrl {
	u32 sgmt_ofs;
	u32 sgmt_len;
	u32 wptr;
	u32 rptr;
};

struct mailbox_hdr {
	u32 max_slot;
	u32 debug_time;
	u32 debug_code;
	u32 log_level;
	u32 log_dram;
	u32 reserved[8];
	struct mailbox_ctrl	h2fctrl[MAILBOX_H2FCTRL_MAX];
	struct mailbox_ctrl	f2hctrl[MAILBOX_F2HCTRL_MAX];
	u32 totsize;
	u32 version;
	u32 signature2;
	u32 signature1;
};

struct cmd_load {
	u32 oid; /* object id */
	u32 tid; /* task id */
};

struct cmd_unload {
	u32 oid;
};

struct cmd_process {
	u32 oid;
	u32 fid; /* frame id */
};

struct cmd_profile_ctl {
	u32 ctl;
};

struct cmd_purge {
	u32 oid;
};

struct cmd_powerdown {
	u32 dummy;
};

struct cmd_done {
	u32 fid;
};

struct cmd_ndone {
	u32 fid;
	u32 error; /* error code */
};

struct cmd_group_done {
	u32 group_idx; /* group index points one element of group vector array */
};

struct cmd_fw_test {
	u32 param;	/* Number of testcase to be executed */
};

enum message_cmd {
	COMMAND_LOAD,
	COMMAND_UNLOAD,
	COMMAND_PROCESS,
	COMMAND_PROFILE_CTL,
	COMMAND_PURGE,
	COMMAND_POWERDOWN,
	COMMAND_FW_TEST,
	COMMAND_H2F_MAX_ID,
	COMMAND_DONE = 100,
	COMMAND_NDONE,
	COMMNAD_GROUP_DONE,
	COMMNAD_ROLLOVER,
	COMMAND_MAX_ID,
	COMMAND_F2H_MAX_ID
};

enum profile_ctl_code {
	PROFILE_CTL_CODE_START,		/* length: Size of profiling buffer, */
 	/* payload: dvaddr of profiling buffer */
	PROFILE_CTL_CODE_STOP,		/* length: 0, payload: 0 */
};

#define MESSAGE_MAGIC 0xC0DECAFE
#define RESPONSE_MAGIC 0xCAFEC0DE
#define NB_MESSAGES 0x20
#define NB_COMMANDS 0x8

struct message {
	u32 magic;
	u32 mid;
	u32 command;
	u32 length;
	u32 self;
	u32 data;
};

struct result {
    u32 _unk_00;
    u32 _unk_04;
    u32 retval;
    u32 _unk_0c;
    u32 _unk_10;
    u32 _unk_14;
    u32 _unk_18;
    u32 command;
    u32 mid;
};

struct command {
	union {
		struct cmd_load		load;
		struct cmd_unload	unload;
		struct cmd_process	process;
		struct cmd_profile_ctl	profile_ctl;
		struct cmd_fw_test	fw_test;
		struct cmd_purge	purge;
		struct cmd_powerdown	powerdown;
		struct cmd_done		done;
		struct cmd_ndone	ndone;
		struct cmd_group_done	gdone;
	} c; /* specific command properties */

	u32 length; /* the size of payload */
	u32 payload;
};

enum RESPONSE_STATE {
    RESPONSE_FREE,
    RESPONSE_READY,
    RESPONSE_IN_PROGRESS,
};

struct ncp_message {
    struct message msg;
    struct message result;
    struct mailbox_dnward *mbox;
    u32 sgmt_cursor;
    u32 _unk_38;
    u32 _unk_3c;
    u32 _unk_40;
    u32 _unk_44;
    u32 _unk_48;
    u32 _unk_4c;
    u32 _unk_50;
    u32 id;
    u32 _unk_58;
    u32 _unk_5c;
    u32 state;
    u32 _unk_64;
};

struct ncp_error {
    struct message msg;
    struct message result;
    struct mailbox_dnward *mbox;
    u32 sgmt_cursor;
    u32 _unk_38;
    u32 return_value;
    u32 _unk_40;
    u32 _unk_44;
    u32 _unk_48;
    u32 _unk_4c;
    u32 _unk_50;
    u32 id;
    u32 _unk_58;
    u32 _unk_5c;
};

struct ncp_handler_state_t {
    struct ncp_message messages[NB_MESSAGES];
    struct ncp_error error;
    u32 (*handlers[9])(struct command**);
    u32 _unk_0x361;
    u32 _unk_0x362;
    u32 _unk_0x363;
    u32 _unk_0x364;
};

struct mailbox_dnward {
    u32 event_id_off;
    struct event* event;
    u32 start;
    struct mailbox_ctrl *hctrl;
    struct mailbox_ctrl fctrl;
};

struct response {
    u16 id;
    u16 priority;
    u32 _unk_04;
    struct ncp_message* data;
    struct list_head response_list_entry;
};

enum RESPONSE_LIST_TYPE {
    RESPONSE_LIST_AVAILABLE,
    RESPONSE_LIST_PENDING,
};

struct response_list {
    u32 type;
    u32 nb_entries;
    u32 _unk_8;
    struct list_head list;
};

struct mailbox_upward {
    u32 event_id_off;
    struct event* event;
    u32 start;
    struct mailbox_ctrl *hctrl;
    struct response responses[NB_MESSAGES];
    struct response_list available;
    struct response_list pending;
};

struct mailbox_report {
    u32 event_id_off;
    u32 _unk_04;
    u32 _unk_08;
    u32 _unk_0c;
    struct mailbox_ctrl *hctrl;
};

u32 g_mailbox_ready;
u32 g_debug_time;
struct mailbox_hdr g_mailbox_hdr;
struct mailbox_hdr *g_mailbox_hdr_ptr;
struct ncp_handler_state_t g_ncp_handler_state;
struct mailbox_dnward g_mailbox_h2fctrl_lpriority;
struct mailbox_dnward g_mailbox_h2fctrl_hpriority;
struct mailbox_upward g_mailbox_f2hctrl_response;
struct mailbox_report g_mailbox_f2hctrl_report;
