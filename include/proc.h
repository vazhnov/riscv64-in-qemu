#ifndef _PROC_H_
#define _PROC_H_

#include "riscv.h"
#include "sys.h"
#include "spinlock.h"
#include "syscalls.h"
#include "fs.h"

#define MAX_PROCS 8

// REG_* constants are indexes into trap_frame_t.regs. (Add here as needed)
#define REG_RA 0
#define REG_SP 1
#define REG_FP 7
#define REG_A0 9
#define REG_A1 10
#define REG_A2 11
#define REG_A3 12
#define REG_A7 16

// PROC_STATE_AVAILABLE signifies an unoccupied slot in the process table, it's
// available for use by a new process.
#define PROC_STATE_AVAILABLE 0

// PROC_STATE_READY means the process is ready to be scheduled.
#define PROC_STATE_READY 1

// PROC_STATE_RUNNING means this process was given the time to run by the
// scheduler.
#define PROC_STATE_RUNNING 2

// PROC_STATE_SLEEPING means this process has yielded execution via sleep() or
// wait() system calls. It will not be scheduled until either enough time has
// elapsed (in case of sleep()) or until the child process exits (in case of
// wait()).
#define PROC_STATE_SLEEPING 3

typedef struct trap_frame_s {
    regsize_t regs[31]; // all registers except r0
    regsize_t pc;
} trap_frame_t;

typedef struct process_s {
    spinlock lock;
    uint32_t pid;
    char *name;
    struct process_s* parent;
    trap_frame_t trap;

    // stack_page points to the base of the page allocated for stack (i.e. it's
    // the value returned by allocate_page()). We need to save it so that we
    // can later pass it to release_page(), as well as when copying the entire
    // stack around, e.g. during fork().
    void *stack_page;
    void *kstack_page;

    // state contains the state of the process, as well as the process table
    // slot itself (e.g. signifying the availability of the slot).
    uint32_t state;

    // wakeup_time contains a timer value when this process should continue
    // executing after a sleep() system call. If state != PROC_STATE_SLEEPING,
    // the value of wakeup_time has no meaning and should be ignored. If the
    // process was put into sleep by a wait() syscall instead of sleep(),
    // wakeup_time should be set to zero.
    uint64_t wakeup_time;

    file_t* files[MAX_PROC_FDS];

    // current sp within kstack_page:
    void *kernel_stack; // each process has its own kernel-side stack,
                        // otherwise syscalls from different processes would
                        // thrash each other's stack
} process_t;

typedef struct proc_table_s {
    spinlock lock;
    process_t procs[MAX_PROCS];
    int num_procs;
    uint32_t pid_counter;

    // is_idle is a flag meaning that the kernel isn't running any user
    // process. This can mean we're fresh after the boot and no user process
    // was scheduled yet, or it could mean all the processes are asleep waiting
    // for something, and thus, the kernel didn't have anything to schedule
    // last time it tried.
    //
    // In technical terms, this means that trap_frame.pc does not point to any
    // user process and likely points to park_hart() within kernel itself, so
    // we shouldn't jump back to it.
    int is_idle; // TODO: is this obsoleted by cpu.proc == NULL?
} proc_table_t;

typedef struct cpu_s {
    process_t *proc;       // the process running on this cpu, or null
    void *kernel_stack;    // copy of proc->kernel_stack or null
} cpu_t;

// defined in proc.c
extern proc_table_t proc_table;

// we're still single-core, so put it here. Later this needs to become an array
// of one cpu_t per hart
//
// defined in proc.c
extern cpu_t cpu;

// trap_frame is the piece of memory to hold all user registers when we enter
// the trap. When the scheduler picks the new process to run, it will save
// trap_frame in the process_t of the old process and will populate trap_frame
// with the new process's context.
//
// When we're ready to return to userland, trap_frame is expected to already
// contain everything the userland needs to have. In case of a fresh process, it
// needs to have at least regs[REG_SP] populated.
//
// In between the traps mscratch will point here so that we can save user
// registers without trashing any.
extern trap_frame_t trap_frame;

// init_test_processes initializes the process table with a set of userland
// processes that will get executed by default. Kind of like what an initrd
// would do, but a poor man's version until we can do better.
void init_test_processes(uint32_t runflags);
void assign_init_program(char const* prog);

void init_process_table(uint32_t runflags);
void schedule_user_process();

// find_ready_proc iterates over the proc table looking for the first available
// proc that's in a PROC_STATE_READY state. Wraps around and starts from zero
// until reaches the same index it started with, in which case it returns the
// same process.
//
// MUST be called with proc_table.lock held.
process_t* find_ready_proc();

// init_global_trap_frame makes sure that mscratch contains a pointer to
// trap_frame before the first userland process gets scheduled.
void init_global_trap_frame();

// proc_fork implements the fork syscall. It will create a new entry in the
// process table, prepare it for being scheduled and return to the parent
// process the pid of the child.
uint32_t proc_fork();

// proc_exit implements the exit syscall. It will remove the process from the
// table, release all the resources taken by the process and call the
// scheduler.
void proc_exit();

// proc_execv implements the exec system call. The 'v' suffix means the
// arguments are passed vectorized, in an array of pointers to strings. The
// last element in argv must be a NULL pointer.
//
// For now, the filename argument is expected to contain a program name as
// specified in userland_programs.
uint32_t proc_execv(char const* filename, char const* argv[]);

// proc_wait implements the wait system call.
int32_t proc_wait();

// proc_sleep implements the sleep system call.
int32_t proc_sleep(uint64_t milliseconds);

// should_wake_up checks the proc's wakeup_time against current time and
// returns true if wakeup_time >= now.
int should_wake_up(process_t* proc);

// alloc_process finds an available slot in the process table and returns its
// address. It will immediately acquire the process lock when it finds the
// slot. It is the caller's responsibility to release it when it's done with
// it.
process_t* alloc_process();

// init_proc initializes a given process struct. Returns the same pointer it
// was passed, for convenience. Must be called with proc_table.lock held.
process_t* init_proc(process_t* proc);

// alloc_pid returns a unique process identifier suitable to assign to a newly
// created process.
uint32_t alloc_pid();

// current_proc returns the userland process that's currently scheduled for
// running.
process_t* current_proc();

// myproc is an opinionated version of current_proc which panics if no process
// is found.
process_t* myproc();

// copy_trap_frame copies the contents of src into dst.
void copy_trap_frame(trap_frame_t* dst, trap_frame_t* src);

uint32_t proc_plist(uint32_t *pids, uint32_t size);

uint32_t proc_pinfo(uint32_t pid, pinfo_t *pinfo);

// proc_open and proc_close are the entry points of open()/close() syscalls,
// they start by dealing with the process-level file descriptors, then call the
// lower level FS stuff.
int32_t proc_open(char const *filepath, uint32_t flags);
int32_t proc_close(uint32_t fd);
int32_t proc_read(uint32_t fd, void *buf, uint32_t size);

// fd_alloc allocates a file descriptor in process's open files list and
// assigns a file pointer to it. proc.lock
// must be held.
int32_t fd_alloc(process_t *proc, file_t *f);
void fd_free(process_t *proc, int32_t fd);

#endif // ifndef _PROC_H_
