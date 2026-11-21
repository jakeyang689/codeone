ENTRY(_start)

SECTIONS
{
  . = 0x40000000;
  __image_start = .;

  .text : ALIGN(4096) {
    *(.text*)
    . = ALIGN(16);
  }
  __text_start = ADDR(.text);
  __text_end = .;

  .rodata : ALIGN(4096) {
    *(.rodata*)
    . = ALIGN(16);
  }
  __rodata_start = ADDR(.rodata);
  __rodata_end = .;

  .data : ALIGN(4096) {
    *(.data*)
    . = ALIGN(16);
  }
  __data_start = ADDR(.data);
  __data_end = .;

  .got : ALIGN(4096) {
    *(.got) *(.got.plt)
    . = ALIGN(16);
  }

  /* Page tables and exception vector area: not loaded from the ELF; cleared/filled explicitly at boot. */
  . = ALIGN(4096);
  __pgt_area = .;
  . += 0x8000;

  . = ALIGN(2048);
  __vectors = .;
  . += 0x800;

  __bss_start = .;
  .bss : ALIGN(4096) {
    *(.bss*) *(COMMON)
    . = ALIGN(16);
  }
  __bss_end = .;

  . = ALIGN(16);
  . += 0x40000;
  __stack_top = .;
  __kernel_end = .;

  __ram_start = 0x40000000;
  __ram_end = 0x60000000;

  /DISCARD/ : { *(.eh_frame) *(.comment) *(.note*) *(.ARM.exidx*) }
}
