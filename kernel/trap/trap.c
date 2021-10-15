#include <trap.h>
#include <types.h>
#include <x86.h>
#include <mmu.h>
#include <memlayout.h>
#include <stdio.h>
#include <clock.h>
#include <picirq.h>
static struct gatedesc idt[256] = {{0}};
static struct pseudodesc idt_pd = {
    sizeof(idt) - 1, (uintptr_t)idt
};

void intr_init(){
    extern uintptr_t __vectors[];
    for(int i=0;i<sizeof(idt)/sizeof(struct gatedesc);i++){
        SETGATE(idt[i],0,GD_KTEXT, __vectors[i], DPL_KERNEL);
    }
    SETGATE(idt[T_SWITCH_TOK], 0, GD_KTEXT, __vectors[T_SWITCH_TOK], DPL_USER);
    lidt(&idt_pd);
}

void trap(struct trapframe *tf){
    uint32_t trap_num = tf->tf_trapno - IRQ_OFFSET;
    if(irq_array[trap_num]){
        irq_array[trap_num](tf);
    }else{
        kprintf("unknown intr number: %d\n",tf->tf_trapno);
    }
}

