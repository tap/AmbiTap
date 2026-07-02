/* AmbiTap: target-independent ambisonics library
 * Minimal Cortex-M55 startup for running the RT-profile check under QEMU
 * (-machine mps3-an547 -semihosting). Not a production BSP: just enough to
 * boot, enable the FPU, initialize newlib + semihosting I/O, run main(),
 * and report the exit status back through semihosting.
 * Timothy Place
 * Copyright 2026 Timothy Place.
 */

#include <stdint.h>
#include <string.h>

extern int  main(void);
extern void exit(int) __attribute__((noreturn));
extern void __libc_init_array(void);
extern void initialise_monitor_handles(void); /* librdimon semihosting I/O */

/* Linker-script symbols. */
extern char __bss_start__[], __bss_end__[];
extern char __heap_start__[], __heap_end__[];
extern char __stack_top__[];

void Reset_Handler(void) {
    /* FPU on (CPACR CP10/CP11 full access) before any float instruction. */
    volatile uint32_t* cpacr = (volatile uint32_t*)0xE000ED88u;
    *cpacr |= (0xFu << 20);
    __asm volatile("dsb\n isb" ::: "memory");

    memset(__bss_start__, 0, (size_t)(__bss_end__ - __bss_start__));
    /* .data is loaded in place by the QEMU ELF loader (VMA == LMA). */

    initialise_monitor_handles();
    __libc_init_array();
    exit(main());
}

static void Default_Handler(void) {
    for (;;) {
    }
}

__attribute__((section(".isr_vector"), used)) const void* const g_vector_table[16] = {
    __stack_top__, /* initial SP */
    (void*)Reset_Handler,
    (void*)Default_Handler, /* NMI */
    (void*)Default_Handler, /* HardFault */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    (void*)Default_Handler, /* SVCall */
    0,
    0,
    (void*)Default_Handler, /* PendSV */
    (void*)Default_Handler, /* SysTick */
};

/* Stubs normally supplied by crti.o/crtn.o (excluded by -nostartfiles). */
void _init(void) {}
void _fini(void) {}

/* Heap for newlib malloc, bounded by the linker script. */
void* _sbrk(int incr) {
    static char* brk = 0;
    if (!brk) brk = __heap_start__;
    if (brk + incr > __heap_end__) return (void*)-1;
    char* prev = brk;
    brk += incr;
    return prev;
}
