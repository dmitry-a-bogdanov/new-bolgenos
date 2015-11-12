#include <bolgenos-ng/bolgenos-ng.h>
#include <bolgenos-ng/vga_console.h>
#include <bolgenos-ng/mmu.h>

#include "bootstrap.h"

multiboot_header_t
	__attribute__((section(".multiboot_header"), used))
	multiboot_header = mbh_initializer(MBH_ALIGN | MBH_MEMINFO);

void kernel_main() {
	vga_console_init();
	vga_clear_screen();

	vga_console_puts("Starting bolgenos-ng-" BOLGENOS_NG_VERSION "\n"); 

	setup_segments();

	vga_console_puts("CPU is initialized\n");

	do {
		asm ("hlt");
	} while(1);
}

