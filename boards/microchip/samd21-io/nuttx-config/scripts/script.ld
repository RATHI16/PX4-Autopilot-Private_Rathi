/****************************************************************************
 * boards/microchip/samd21-io/nuttx-config/scripts/script.ld
 *
 * SAMD21J18A: 256KB flash at 0x00000000, 32KB SRAM at 0x20000000
 * PX4IO application starts at 0x00001000 (leaving 4KB for bootloader)
 ****************************************************************************/

MEMORY
{
    flash (rx)  : ORIGIN = 0x00001000, LENGTH = 252K
    sram  (rwx) : ORIGIN = 0x20000000, LENGTH = 32K
}

OUTPUT_ARCH(arm)
ENTRY(__start)
EXTERN(_vectors)
EXTERN(abort)

SECTIONS
{
    .text : {
        _stext = ABSOLUTE(.);
        *(.vectors)
        *(.text .text.*)
        *(.fixup)
        *(.gnu.warning)
        *(.rodata .rodata.*)
        *(.gnu.linkonce.t.*)
        *(.glue_7)
        *(.glue_7t)
        *(.got)
        *(.gcc_except_table)
        *(.gnu.linkonce.r.*)
        _etext = ABSOLUTE(.);
    } > flash

    .init_section : {
        _sinit = ABSOLUTE(.);
        KEEP(*(.init_array .init_array.*))
        _einit = ABSOLUTE(.);
    } > flash

    .ARM.extab : {
        *(.ARM.extab*)
    } > flash

    __exidx_start = ABSOLUTE(.);
    .ARM.exidx : {
        *(.ARM.exidx*)
    } > flash
    __exidx_end = ABSOLUTE(.);

    _eronly = ABSOLUTE(.);

    .data : {
        _sdata = ABSOLUTE(.);
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
        CONSTRUCTORS
        _edata = ABSOLUTE(.);
        . = ALIGN(4);
    } > sram AT > flash

    .bss : {
        _sbss = ABSOLUTE(.);
        *(.bss .bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = ABSOLUTE(.);
    } > sram

    .stab 0 : { *(.stab) }
    .stabstr 0 : { *(.stabstr) }
    .debug_abbrev 0 : { *(.debug_abbrev) }
    .debug_info 0 : { *(.debug_info) }
    .debug_line 0 : { *(.debug_line) }
    .debug_pubnames 0 : { *(.debug_pubnames) }
    .debug_aranges 0 : { *(.debug_aranges) }
}
