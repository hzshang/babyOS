#ifndef __KERN_DRIVER_PICIRQ_H__
#define __KERN_DRIVER_PICIRQ_H__
#include <trap.h>
void pic_init(void);
void pic_enable(unsigned int irq,void (*fn)(struct trapframe*));
extern void (*irq_array[0x20])(struct trapframe*);
#define IRQ_OFFSET      32

#endif /* !__KERN_DRIVER_PICIRQ_H__ */

