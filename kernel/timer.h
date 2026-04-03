#ifndef TIMER_H
#define TIMER_H

#include "types.h"

#define PIT_FREQ    1193182
#define TICK_RATE   100       

void   timer_init(void);
u32    timer_ticks(void);
void   timer_tick(void);      
void   sleep_ms(u32 ms);


void   timer_poll(void);

#endif
