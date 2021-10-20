#ifndef SMP_H
#define SMP_H

void start_thread();

extern volatile DECLARE_LOCK(taskLock);

extern volatile void (*task)(void*);
extern void* task_arg;
#endif
