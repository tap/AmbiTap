/* AmbiTap: linker script for the QEMU mps3-an547 (Cortex-M55) RT-profile
 * check. Code + tables in ITCM at 0x0 (the AN547 reset vector base); data,
 * bss, heap, and stack in the DDR window (plenty of room; QEMU's ELF loader
 * places segments directly, so VMA == LMA and no startup copy is needed).
 * Timothy Place
 * Copyright 2026 Timothy Place.
 */

MEMORY
{
    ITCM (rx)  : ORIGIN = 0x00000000, LENGTH = 512K
    DDR  (rwx) : ORIGIN = 0x60000000, LENGTH = 16M
}

ENTRY(Reset_Handler)

SECTIONS
{
    .isr_vector : { KEEP(*(.isr_vector)) } > ITCM

    .text : {
        *(.text*)
        *(.rodata*)
        KEEP(*(.init))
        KEEP(*(.fini))
        . = ALIGN(4);
        __preinit_array_start = .;
        KEEP(*(.preinit_array))
        __preinit_array_end = .;
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
        __fini_array_start = .;
        KEEP(*(.fini_array))
        __fini_array_end = .;
        *(.ARM.extab*)
        __exidx_start = .;
        *(.ARM.exidx*)
        __exidx_end = .;
    } > ITCM

    .data : {
        . = ALIGN(8);
        *(.data*)
        . = ALIGN(8);
    } > DDR

    .bss (NOLOAD) : {
        . = ALIGN(8);
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end__ = .;
    } > DDR

    .heap_stack (NOLOAD) : {
        . = ALIGN(8);
        __heap_start__ = .;
        . += 8M;                 /* heap */
        __heap_end__ = .;
        . += 256K;               /* stack */
        . = ALIGN(8);
        __stack_top__ = .;
    } > DDR

    /* newlib compatibility aliases */
    PROVIDE(end = __heap_start__);
    PROVIDE(__stack = __stack_top__);
}
