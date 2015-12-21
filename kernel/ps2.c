#include <bolgenos-ng/ps2.h>

#include <bolgenos-ng/asm.h>
#include <bolgenos-ng/error.h>
#include <bolgenos-ng/mem_utils.h>
#include <bolgenos-ng/pic_common.h>
#include <bolgenos-ng/printk.h>
#include <bolgenos-ng/string.h>
#include <bolgenos-ng/time.h>


/**
* IRQ for the first PS/2 line.
*/
#define FIRST_LINE_IRQ		(min_pic_irq + 1)


/**
* IRQ for the second PS/2 line.
*/
#define SECOND_LINE_IRQ		(min_pic_irq + 12)


/**
* Maximal number of devices that can be registered.
*/
#define MAX_REGISTERED_DEVICES			10


/**
* Timeout for self-test procedure.
*/
#define SELFTEST_TIMEOUT			5 /* ms */


/**
* Timeout for output operation.
*/
#define OUTPUT_TIMEOUT				1 /* ms */


enum ps2_reply {
	ps2_rpl_port_test_ok		= 0x00,
	ps2_rpl_port_test_clk_low	= 0x01,
	ps2_rpl_port_test_clk_high	= 0x02,
	ps2_rpl_port_test_data_low	= 0x03,
	ps2_rpl_port_test_data_high	= 0x04,

	ps2_rpl_self_test_ok		= 0x55,
	ps2_rpl_self_test_fail		= 0xfc
};


// PS/2 status register bits
typedef enum {
	ps2_sr_out_buf_status	= 1 << 0,
	ps2_sr_in_buf_status	= 1 << 1,
	ps2_sr_post_passed	= 1 << 2,
	ps2_sr_comm_or_data	= 1 << 3,
	// unknown		= 1 << 4,
	// unknown		= 1 << 5,
	ps2_sr_timeout		= 1 << 6,
	ps2_sr_parity_error	= 1 << 7
} ps2_sr_field_t;


// PS/2 configuration byte bits
enum ps2_cb {
	ps2_cb_int_first	= 1 << 0,
	ps2_cb_int_second	= 1 << 1,
	ps2_cb_system		= 1 << 2,
	ps2_cb_clock_first	= 1 << 4,
	ps2_cb_clock_second	= 1 << 5,
	ps2_cb_translation	= 1 << 6
};


typedef enum {
	ps2_cmd_read_cb		= 0x20,
	ps2_cmd_write_cb	= 0x60,

	ps2_cmd_self_test	= 0xaa,
	ps2_cmd_port_test_1	= 0xab,
	ps2_cmd_port_test_2	= 0xa9,

	ps2_cmd_disable_1	= 0xad,
	ps2_cmd_enable_1	= 0xae,
	ps2_cmd_disable_2	= 0xa7,
	ps2_cmd_enable_2	= 0xa8,

	ps2_cmd_redirect_2nd	= 0xd4
} ps2_command_t;


enum ps2_port {
	ps2_data_port		= 0x60,
	ps2_status_reg		= 0x64,
	ps2_command_reg		= 0x64
};



// TODO: replace array with list in order to avoid troubles with support
// of many types of keyboards or mice
static struct ps2_dev *ps2_known_devices[MAX_REGISTERED_DEVICES];
static int ps2_known_device_count = 0;


static struct ps2_dev *ps2_active_devices[__ps2_dev_max];

static void send_byte(uint8_t byte);
static uint8_t read_conf_byte();
static void write_conf_byte(uint8_t conf_byte);
static void probe_devices();
static void ps2_irq_handler(ps2_line_t line);
static void first_line_irq(irq_t);
static void second_line_irq(irq_t);
static void enable_device(ps2_line_t idx);
static void disable_device(ps2_line_t idx);
static int get_ps2_lines(uint8_t conf_byte);
static void enable_ps2_interrupts(uint8_t *conf_byte, int lines);
static void disable_ps2_interrupts(uint8_t *conf_byte, int lines);
static void disable_translation(uint8_t *conf_byte);
static int controller_selftest();
static int test_line(ps2_line_t line);
static uint8_t read_status();
static int wait_for_flag(ps2_sr_field_t flag, int val, int ms);

void ps2_register_device(struct ps2_dev *dev) {
	if (ps2_known_device_count < MAX_REGISTERED_DEVICES) {
		ps2_known_devices[ps2_known_device_count++] = dev;
	}
}


void ps2_init() {
	uint8_t conf;
	printk("initializing PS/2 controller...\n");
	disable_device(ps2_dev_1);
	disable_device(ps2_dev_2);

	ps2_clean_buffer();

	conf = read_conf_byte();

	printk("PS/2 conf byte=%lu\n", (unsigned long) conf);

	int ps2_lines = get_ps2_lines(conf);

	printk("this system has %lu PS/2 port(s)\n",
		(long unsigned) ps2_lines);

	disable_ps2_interrupts(&conf, ps2_dev_1|ps2_dev_1);
	disable_translation(&conf);

	write_conf_byte(conf);

	if (!controller_selftest()) {
		panic("PS/2: controller self-test failed!");
	}

	enable_device(ps2_dev_2);
	
	conf = read_conf_byte();
	if (conf & (ps2_cb_clock_second|ps2_cb_clock_first)) {
		printk("both PS/2 devs present\n");
	} else if (conf & ps2_cb_clock_first) {
		printk("only first PS/2 dev present\n");
	} else if (conf & ps2_cb_clock_second) {
		printk("only second PS/2 dev present\n");
	} else {
		printk("no devs present\n");
	}

	disable_device(ps2_dev_2);

	for (ps2_line_t ps2_line = __ps2_dev_min;
			ps2_line < __ps2_dev_max; ++ps2_line) {
		if (!test_line(ps2_line)) {
			printk("PS/2: line %li failed self-test!\n",
					(long) ps2_line);
		} else {
			printk("PS/2: line %li passed self-test\n",
					(long) ps2_line);
		}
	}

	register_irq_handler(FIRST_LINE_IRQ, first_line_irq);
	register_irq_handler(SECOND_LINE_IRQ, second_line_irq);


	enable_device(ps2_dev_1);
	enable_device(ps2_dev_2);

	probe_devices();

	conf = read_conf_byte();
	enable_ps2_interrupts(&conf, ps2_dev_1|ps2_dev_2);
	write_conf_byte(conf);
};


uint8_t ps2_receive_byte() {
	return inb(ps2_data_port);
}


void ps2_send_command(ps2_command_t cmd) {
	outb(ps2_command_reg, cmd);
}


int ps2_wait_for_output(int ms) {
	return ! wait_for_flag(ps2_sr_in_buf_status, 0, ms);
}


int ps2_wait_for_input(int ms) {
	return !! wait_for_flag(ps2_sr_out_buf_status, 1, ms);
}


int ps2_can_read() {
	return ps2_wait_for_input(0);
}


void ps2_clean_buffer() {
	uint8_t status_register = read_status();
	while (status_register & ps2_sr_out_buf_status) {
		ps2_receive_byte(); // ignore input
		status_register = read_status();
	}
}


ps2_ioret_t ps2_send_byte_dev(ps2_line_t line, uint8_t byte) {
	if (line == ps2_dev_2) {
		ps2_send_command(ps2_cmd_redirect_2nd);
	}
	int can_write = ps2_wait_for_output(OUTPUT_TIMEOUT);
	if (!can_write) {
		return ps2_ioret_timeout;
	}
	send_byte(byte);
	return ps2_ioret_ok;
}


ps2_ioret_t ps2_send_byte_with_ack(ps2_line_t line, uint8_t byte,
		uint8_t ack) {
	ps2_ioret_t ret;
	ret = ps2_send_byte_dev(line, byte);
	if (ret != ps2_ioret_ok) {
		return ret;
	}
	if (ps2_wait_for_input(OUTPUT_TIMEOUT)) {
		uint8_t byte = ps2_receive_byte();
		if (byte == ack) {
			return ps2_ioret_ok;
		} else {
			return ps2_ioret_wrong_ack;
		}
	} else {
		return ps2_ioret_timeout;
	}
}


/**
* \brief Error descriptions.
*
* Array with strings that describes possible PS2 I/O errors.
*/
static char *ps2_err_descr[] = {
	[ps2_ioret_ok] = "No error",
	[ps2_ioret_wrong_ack] = "Wrong ack",
	[ps2_ioret_timeout] = "Timeout",
	[ps2_ioret_unknown_error] = "Unknown error",
};

char *ps2_ioret_strerror(ps2_ioret_t error) {
	if (error >= ps2_ioret_unknown_error)
		return ps2_err_descr[ps2_ioret_unknown_error];
	return ps2_err_descr[error];
}


/**
* \brief First PS/2 line interrupt handler.
*
* The function call \ref ps2_irq_handler with correct parameter.
*
* \param vec Unused parameter that is needed to match types.
*/
static void first_line_irq(irq_t vec __attribute__((unused))) {
	ps2_irq_handler(ps2_dev_1);
}


/**
* \brief Second PS/2 line interrupt handler.
*
* The function call \ref ps2_irq_handler with correct parameter.
*
* \param vec Unused parameter that is needed to match types.
*/
static void second_line_irq(irq_t vec __attribute__((unused))) {
	ps2_irq_handler(ps2_dev_2);
}


/**
* \brief Handle IRQ from PS/2 device.
*
* The function transfers handling of interrupt to corresponding driver.
*
* \param line PS/2 device that raised interrupt.
*/
static void ps2_irq_handler(ps2_line_t line) {
	if (ps2_active_devices[line]) {
		ps2_active_devices[line]->irq_handler();
	}
}


/**
* \brief Read configuration byte.
*
* The function reads configuration byte.
*
* \return Configuration byte.
*/
static uint8_t read_conf_byte() {
	uint8_t conf_byte;
	ps2_send_command(ps2_cmd_read_cb);
	conf_byte = ps2_receive_byte();
	return conf_byte;
}


/**
* \brief Write configuration byte.
*
* The function writes configuration byte to PS/2 controller.
*
* \param conf_byte Configuration byte to be written.
*/
static void write_conf_byte(uint8_t conf_byte) {
	ps2_send_command(ps2_cmd_write_cb);
	send_byte(conf_byte);
}


/**
* \brief Probe PS/2 devices.
*
* Function probes all registered PS/2 drivers to all PS/2 devices.
*/
static void probe_devices() {
	// for each line do ...
	for (ps2_line_t line = __ps2_dev_min;
			line < __ps2_dev_max; ++line) {
		struct ps2_dev *active_dev = NULL;
		int active_dev_count = 0;
		// for each registered device
		for (int dev_index = 0; dev_index < ps2_known_device_count;
				++dev_index) {
			probe_ret_t ret;
			ret = ps2_known_devices[dev_index]->probe(line);
			if (ret == probe_ok) {
				active_dev = ps2_known_devices[dev_index];
				active_dev_count++;
			}
		}
		if (active_dev_count > 1) {
			char info[100];
			snprintf(info, 100, "more than 1 probed devices for "
				"PS/2 line %li\n", (long) line);
			bug(info);
		}

		printk("PS/2[%li]: active_dev = %li\n",
			(long) line, (long) active_dev);

		ps2_active_devices[line] = active_dev;
	}
}


/**
* \brief Enable PS/2 device.
*
* The function enables specified PS/2 device.
* \param idx Device to be enabled.
*/
static void enable_device(ps2_line_t idx) {
	if (idx == ps2_dev_1) {
		ps2_send_command(ps2_cmd_enable_1);
	} else {
		ps2_send_command(ps2_cmd_enable_2);
	}
}


/**
* \brief Disable PS/2 device.
*
* The function disables specified PS/2 device.
* \param idx Device to be disabled.
*/
static void disable_device(ps2_line_t idx) {
	ps2_command_t cmd;
	if (idx == ps2_dev_1) {
		cmd = ps2_cmd_disable_1;
	} else {
		cmd = ps2_cmd_disable_2;
	}
	ps2_send_command(cmd);
}


/**
* \brief Get number of PS/2 lines.
*
* The function calculates number of PS/2 lines from configuration bytes.
*
* \param conf_byte Configuration byte.
* \return Number of PS/2 lines.
*/
static int get_ps2_lines(uint8_t conf_byte) {
	int ps2_lines = 1;
	if (conf_byte & ps2_cb_clock_second) {
		ps2_lines = 2;
	}
	return ps2_lines;
}


/**
* \brief Enable interrupts from devices.
*
* The function enable interrupts from specified PS/2 devices.
*
* \param lines Bitwise OR-ed PS/2 lines devices that should start raise
* interrupts.
*/
static void enable_ps2_interrupts(uint8_t *conf_byte, int lines) {
	if (lines & ps2_dev_1) {
		*conf_byte |= ps2_cb_int_first;
	}
	if (lines & ps2_dev_2) {
		*conf_byte |= ps2_cb_int_second;
	}
}


/**
* \brief Disable interrupts from devices.
*
* The function disables interrupts from specified PS/2 devices.
*
* \param lines Bitwise OR-ed PS/2 lines devices that should not raise
* interrupts.
*/
static void disable_ps2_interrupts(uint8_t *conf_byte, int lines) {
	if (lines & ps2_dev_1) {
		*conf_byte &= ~ps2_cb_int_first;
	}
	if (lines & ps2_dev_2) {
		*conf_byte &= ~ps2_cb_int_second;
	}
}


/**
* \brief Disable translation option.
*
* The function clears translation bytes in provided PS/2 configuration byte.
*
* \param conf_byte Pointer to configuration byte.
*/
static void disable_translation(uint8_t *conf_byte) {
	*conf_byte &= ~ps2_cb_translation;
}


/**
* \brief Run PS/2 controller self-test.
*
* The function runs PS/2 controller self-test with \ref SELFTEST_TIMEOUT and
* returns status.
*/
static int controller_selftest() {
	ps2_send_command(ps2_cmd_self_test);
	int can_read = ps2_wait_for_input(SELFTEST_TIMEOUT);
	if (!can_read) {
		// self-test timeout!
		return 0;
	}
	uint8_t test_result = ps2_receive_byte();
	if (test_result == ps2_rpl_self_test_ok) {
		return 1;
	}
	return 0;
}


/**
* \brief Send byte.
*
* Send byte to data port.
*
* \param byte Byte to be sent.
*/
static void send_byte(uint8_t byte) {
	outb(ps2_data_port, byte);
}


/**
* \brief Read PS/2 status.
*
* The function reads PS/2 status register.
*
* \return PS/2 status register.
*/
static uint8_t read_status() {
	return inb(ps2_status_reg);
}


/**
* \brief Wait for flag in SR.
*
* The functions waits specified value for given flag druing specified time.
*
* \param flag Flag to wait.
* \param val Value for waiting.
* \param ms Time to wait.
* \return Value of flag after waiting.
*/
static int wait_for_flag(ps2_sr_field_t flag, int val, int ms) {
	uint8_t status;
	int wait = 0;
	if (val) {
		while (!((status = read_status()) & flag)
				&& wait < ms) {
			++wait;
			sleep_ms(1 /* ms */);
		}
	} else {
		while (((status = read_status()) & flag)
				&& wait < ms) {
			++wait;
			sleep_ms(1 /* ms */);
		}
	}
	return status & flag;
}


/**
* \brief Test PS/2 line.
*
* The function tests specified PS/2 line.
*
* \param line Line to be tested.
* \return 1 if test is ok; 0 otherwise.
*/
static int test_line(ps2_line_t line) {
	ps2_command_t cmd;
	if (line == ps2_dev_1) {
		cmd = ps2_cmd_port_test_1;
	} else {
		cmd = ps2_cmd_port_test_2;
	}
	ps2_send_command(cmd);
	int can_read = ps2_wait_for_input(SELFTEST_TIMEOUT);
	if (!can_read) {
		printk("no responce to self-test\n");
		return 0;
	}
	uint8_t test_result = ps2_receive_byte();
	if (test_result == ps2_rpl_port_test_ok) {
		return 1;
	}
	printk("line test result = %lu\n", (long unsigned) test_result);
	return 0;
}
