#include <bolgenos-ng/asm.h>
#include <bolgenos-ng/bolgenos-ng.h>
#include <bolgenos-ng/error.h>
#include <bolgenos-ng/irq.h>
#include <bolgenos-ng/mem_utils.h>
#include <bolgenos-ng/mmu.h>
#include <bolgenos-ng/multiboot_info.h>
#include <bolgenos-ng/pic_8259.h>
#include <bolgenos-ng/pic_common.h>
#include <bolgenos-ng/printk.h>
#include <bolgenos-ng/ps2.h>
#include <bolgenos-ng/ps2_keyboard.h>
#include <bolgenos-ng/string.h>
#include <bolgenos-ng/time.h>
#include <bolgenos-ng/vga_console.h>


/**
* \brief Kernel main function.
*
* The main kernel function. The fuction performs full bootstrap of kernel
*	and then goes to idle state.
*/
void kernel_main() {
	interrupts_disable();

	multiboot_info_init();

	vga_console_init();
	vga_clear_screen();

	printk("Starting bolgenos-ng-" BOLGENOS_NG_VERSION "\n");
	if (mboot_is_meminfo_valid()) {
		printk("Detected memory: low = %lu kb, high = %lu kb\n",
			(long unsigned) mboot_get_low_mem(),
			(long unsigned) mboot_get_high_mem());
	} else {
		panic("Bootloader didn't provide memory info!\n");
	}

	setup_segments();
	setup_interrupts();

	system_pic = &pic_8259;
	system_pic->setup();

	init_timer();

	interrupts_enable();

	printk("CPU is initialized\n");

	ps2_keyboard_init();
	ps2_init();

	do {
		asm ("hlt");
	} while(1);
}

