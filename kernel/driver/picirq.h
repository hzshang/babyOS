#ifndef __KERN_DRIVER_PICIRQ_H__
#define __KERN_DRIVER_PICIRQ_H__
// #include <trap.h>
void pic_init(void);
void pic_enable(unsigned int irq);


#endif /* !__KERN_DRIVER_PICIRQ_H__ */

