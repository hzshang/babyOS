#include <x86.h>
#include <trap.h>
#include <stdio.h>
#include <picirq.h>

/* *
 * Support for time-related hardware gadgets - the 8253 timer,
 * which generates interruptes on IRQ-0.
 * */

#define IO_TIMER1           0x040               // 8253 Timer #1

/* *
 * Frequency of all three count-down timers; (TIMER_FREQ/freq)
 * is the appropriate count to generate a frequency of freq Hz.
 * */

#define TIMER_FREQ      1193182
#define TIMER_DIV(x)    ((TIMER_FREQ + (x) / 2) / (x))

#define TIMER_MODE      (IO_TIMER1 + 3)         // timer mode port
#define TIMER_SEL0      0x00                    // select counter 0
#define TIMER_RATEGEN   0x04                    // mode 2, rate generator
#define TIMER_16BIT     0x30                    // r/w counter 16 bits, LSB first
#define TICK_NUM 100
volatile size_t ticks;

/* *
 * clock_init - initialize 8253 clock to interrupt 100 times per second,
 * and then enable IRQ_TIMER.
 * */
void ticks_callback(struct trapframe* tf){
    ticks++;
    if(ticks%TICK_NUM == 0){
//        kprintf("%d ticks\n",TICK_NUM);
    }
}

void clock_init(void) {
    // set 8253 timer-chip
    outb(TIMER_MODE, TIMER_SEL0 | TIMER_RATEGEN | TIMER_16BIT);
    outb(IO_TIMER1, TIMER_DIV(100) % 256);
    outb(IO_TIMER1, TIMER_DIV(100) / 256);
    // initialize time counter 'ticks' to zero
    ticks = 0;
    kprintf("setup timer interrupts\n");
    pic_enable(IRQ_TIMER,ticks_callback);
}




void babysleep(size_t sleep){
    size_t current_tick = ticks;
    while(ticks - current_tick < sleep){
        asm("hlt");
    }
}





