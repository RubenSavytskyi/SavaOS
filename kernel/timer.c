#include "timer.h"
#include "types.h"

static volatile u32 ticks = 0;
static u32 last_pit = 0;

void timer_init(void) {
    
    u16 divisor = PIT_FREQ / TICK_RATE;
    outb(0x43, 0x36);               
    outb(0x40, divisor & 0xFF);
    outb(0x40, (divisor >> 8) & 0xFF);
    ticks = 0;
    last_pit = 0;
}


static u16 pit_read(void) {
    outb(0x43, 0x00);   
    u8 lo = inb(0x40);
    u8 hi = inb(0x40);
    return (u16)((hi << 8) | lo);
}

void timer_poll(void) {
    u16 cur = pit_read();
    
    u16 divisor = PIT_FREQ / TICK_RATE;
    
    if (cur > last_pit) {
        ticks++;
    }
    last_pit = cur;
    (void)divisor;
}

u32 timer_ticks(void) {
    return ticks;
}

void sleep_ms(u32 ms) {
    u32 target_ticks = ticks + (ms * TICK_RATE / 1000) + 1;
    while (ticks < target_ticks) {
        timer_poll();
        __asm__ volatile ("pause");
    }
}
