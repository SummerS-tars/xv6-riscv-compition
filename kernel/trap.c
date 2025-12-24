#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
// [新增] 必须添加以下头文件，且顺序很重要
#include "sleeplock.h" 
#include "fs.h"
#include "file.h"
#include "fcntl.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  initlock(&tickslock, "time");
}

// set up to take exceptions and traps while in the kernel.
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

//
// handle an interrupt, exception, or system call from user space.
// called from, and returns to, trampoline.S
// return value is user satp for trampoline.S to switch to.
//
uint64
usertrap(void)
{
  int which_dev = 0;

  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  w_stvec((uint64)kernelvec);

  struct proc *p = myproc();
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // 系统调用
    if(killed(p))
      kexit(-1); // 原有代码使用 kexit
    p->trapframe->epc += 4;
    intr_on();
    syscall();
  } else if((which_dev = devintr()) != 0){
    // 设备中断
  } else if((r_scause() == 13 || r_scause() == 15)) { 
    // [新增] Page Fault 处理 (13=Load, 15=Store)
    uint64 va = r_stval(); 
    struct vma *v = 0;

    // 1. 检查 VA 是否在 VMA 范围内
    for(int i = 0; i < 16; i++){
      if(p->vmas[i].used && va >= p->vmas[i].addr && va < p->vmas[i].addr + p->vmas[i].length){
        v = &p->vmas[i];
        break;
      }
    }

    if(v) {
      // 2. 分配物理页
      char *mem = kalloc();
      if(mem == 0) {
        setkilled(p); // 内存不足，杀掉进程
      } else {
        memset(mem, 0, PGSIZE);

        // 3. 从磁盘读取数据
        uint64 offset_in_file = v->offset + (PGROUNDDOWN(va) - v->addr);
        ilock(v->f->ip);
        readi(v->f->ip, 0, (uint64)mem, offset_in_file, PGSIZE); 
        iunlock(v->f->ip);

        // 4. 建立映射
        int perm = PTE_U;
        if(v->prot & PROT_READ) perm |= PTE_R;
        if(v->prot & PROT_WRITE) perm |= PTE_W;
        // 这里的 PTE_X 不是必须的，除非你想支持映射可执行文件

        if(mappages(p->pagetable, PGROUNDDOWN(va), PGSIZE, (uint64)mem, perm) < 0){
          kfree(mem);
          setkilled(p);
        }
      }
    } else {
      // 非法访问：打印信息并杀掉进程
      printf("usertrap(): unexpected scause %p pid=%d\n", (void*)r_scause(), p->pid);
      printf("            sepc=%p stval=%p\n", (void*)r_sepc(), (void*)r_stval());
      setkilled(p);
    }
  } else {
    // 其他意外中断
    printf("usertrap(): unexpected scause %p pid=%d\n", (void*)r_scause(), p->pid);
    printf("            sepc=%p stval=%p\n", (void*)r_sepc(), (void*)r_stval());
    setkilled(p);
  }

  if(killed(p))
    kexit(-1);

  if(which_dev == 2)
    yield();

  prepare_return();
  return MAKE_SATP(p->pagetable);
}

//
// set up trapframe and control registers for a return to user space
//
void
prepare_return(void)
{
  struct proc *p = myproc();

  // we're about to switch the destination of traps from
  // kerneltrap() to usertrap(). because a trap from kernel
  // code to usertrap would be a disaster, turn off interrupts.
  intr_off();

  // send syscalls, interrupts, and exceptions to uservec in trampoline.S
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // set up trapframe values that uservec will need when
  // the process next traps into the kernel.
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // set up the registers that trampoline.S's sret will use
  // to get to user space.
  
  // set S Previous Privilege mode to User.
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // clear SPP to 0 for user mode
  x |= SSTATUS_SPIE; // enable interrupts in user mode
  w_sstatus(x);

  // set S Exception Program Counter to the saved user pc.
  w_sepc(p->trapframe->epc);
}

// interrupts and exceptions from kernel code go here via kernelvec,
// on whatever the current kernel stack is.
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    // interrupt or trap from an unknown source
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // give up the CPU if this is a timer interrupt.
  if(which_dev == 2 && myproc() != 0)
    yield();

  // the yield() may have caused some traps to occur,
  // so restore trap registers for use by kernelvec.S's sepc instruction.
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  if(cpuid() == 0){
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
  }

  // ask for the next timer interrupt. this also clears
  // the interrupt request. 1000000 is about a tenth
  // of a second.
  w_stimecmp(r_time() + 1000000);
}

// check if it's an external interrupt or software interrupt,
// and handle it.
// returns 2 if timer interrupt,
// 1 if other device,
// 0 if not recognized.
int
devintr()
{
  uint64 scause = r_scause();

  if(scause == 0x8000000000000009L){
    // this is a supervisor external interrupt, via PLIC.

    // irq indicates which device interrupted.
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // the PLIC allows each device to raise at most one
    // interrupt at a time; tell the PLIC the device is
    // now allowed to interrupt again.
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000005L){
    // timer interrupt.
    clockintr();
    return 2;
  } else {
    return 0;
  }
}

