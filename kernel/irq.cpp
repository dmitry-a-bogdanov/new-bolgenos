#include <bolgenos-ng/irq.hpp>

#include <bolgenos-ng/asm.h>
#include <bolgenos-ng/compiler.h>
#include <bolgenos-ng/error.h>
#include <bolgenos-ng/mem_utils.h>
#include <bolgenos-ng/mmu.h>
#include <bolgenos-ng/pic_common.h>
#include <bolgenos-ng/printk.h>
#include <bolgenos-ng/string.h>
#include <bolgenos-ng/time.h>

#include <bolgenos-ng/stdtypes.hpp>

#include <lib/algorithm.hpp>
#include <lib/list.hpp>

namespace {


using HandlersChain = lib::list<irq::irq_handler_t>;

HandlersChain registered_isrs[irq::number_of_irqs::value];

void irq_dispatcher(irq::irq_t vector);


using gate_field_t = uint64_t;

#define __reserved(size) gate_field_t macro_concat(__reserved__, __LINE__) : size

// Common gate type that can alias every other gate in strict terms.
// As the result we must use the same type for all gate fields.
struct __attribute__((packed)) gate_t {
	__reserved(16);
	__reserved(16);
	__reserved(5);
	__reserved(3);
	__reserved(3);
	__reserved(1);
	__reserved(1);
	__reserved(2);
	__reserved(1);
	__reserved(16);
};
static_assert(sizeof(gate_t) == 8, "gate_t has wrong size");

enum irq_gate_type {
	task = 0x5,
	intr = 0x6,
	trap = 0x7,
};

#define __as_gate(in) reinterpret_cast<gate_t *>((in))

struct __attribute__((packed)) task_gate_t {
	__reserved(16);
	gate_field_t tss_segment: 16;
	__reserved(5);
	__reserved(3);
	gate_field_t gate_type : 3;
	gate_field_t task_zero : 1;
	__reserved(1);
	gate_field_t dpl : 2;
	gate_field_t present : 1;
	__reserved(16);
};

static_assert(sizeof(task_gate_t) == 8, "task_gate_t has wrong size");

// Comment__why_using_get_gate:
//
// Strict aliasing rules are only applied to pointer and cannot be applied
// to lvalue of some non-pointer type. This constraint include also explicit
// typecasting after getting address. As the result, workaround with
// casting function is need. However, it doesn't mean that code violates
// strict alising rules, because types are still compatible.
inline gate_t *task_get_gate(task_gate_t *task) {
	return __as_gate(task);
}

struct __attribute__((packed)) int_gate_t {
	int_gate_t(gate_field_t offs, gate_field_t seg)
			: offset_00_15(bitmask(offs, 0, 0xffff)),
			segment(seg), _reserved(0), zeros(0),
			gate_type(irq_gate_type::intr), flag_32_bit(1),
			zero_bit(0), dpl(0), present(1),
			offset_16_31(bitmask(offs, 16, 0xffff)) {
	}
	gate_field_t offset_00_15:16;
	gate_field_t segment:16;
	gate_field_t _reserved:5;
	gate_field_t zeros:3;
	gate_field_t gate_type:3;
	gate_field_t flag_32_bit:1;
	gate_field_t zero_bit:1;
	gate_field_t dpl:2;
	gate_field_t present:1;
	gate_field_t offset_16_31:16;
};

static_assert(sizeof(int_gate_t) == 8, "int_gate_t has wrong size");

// see Comment__why_using_get_gate
inline gate_t *int_get_gate(int_gate_t *intr) {
	return __as_gate(intr);
}


struct __attribute__((packed)) trap_gate_t {
	gate_field_t offset_00_15: 16;
	gate_field_t segment: 16;
	__reserved(5);
	gate_field_t zeros :3;
	gate_field_t gate_type : 3;
	gate_field_t flag_32_bit : 1;
	gate_field_t zero_bit : 1;
	gate_field_t dpl : 2;
	gate_field_t present : 1;
	gate_field_t offset_16_31 : 16;
};

#define __decl_trap_gate(offset_, segment_) {				\
	.offset_00_15 = bitmask(offset_, 0, 0xffff),			\
	.segment = segment_,						\
	.zeros = 0x0,							\
	.gate_type = GATE_TRAP,						\
	.flag_32_bit = 1,						\
	.zero_bit = 0,							\
	.dpl = 0x0,							\
	.present = 1,							\
	.offset_16_31 = bitmask(offset_, 16, 0xffff)			\
}

// See Comment__why_using_get_gate
inline gate_t *trap_get_gate(trap_gate_t *trap) {
	return __as_gate(trap);
}


static_assert(sizeof(trap_gate_t) == 8, "trap_gate_t has wrong size");


#define __decl_isr_stage_asm(num)					\
	asm(								\
		".align 16\n"						\
		"__isr_stage_asm_" #num ":\n"				\
			"pushal\n"					\
			"call __isr_stage_c_" #num "\n"			\
			"popal\n"					\
			"iret\n"					\
	);								\
	extern "C" void __isr_stage_asm_ ## num()


#define __decl_isr_stage_c(num, generic_isr)				\
	extern "C" void __attribute__((used)) __isr_stage_c_ ## num () {		\
		generic_isr(num);					\
		end_of_irq(num);					\
	}

#define __decl_isr(num, function)					\
	__decl_isr_stage_asm(num);					\
	__decl_isr_stage_c(num, function);


#define __decl_common_gate(num, table)					\
	do {								\
		int_gate_t gate((uint32_t) __isr_stage_asm_ ## num, KERNEL_CS);	\
		table[num] = *int_get_gate(&gate);			\
	} while(0)

// comment__why_not_use_counter:
//
// Looks like using __COUNTER__ is unsafe since it may depends on
// compilation order.
__decl_isr(0, irq_dispatcher);
__decl_isr(1, irq_dispatcher);
__decl_isr(2, irq_dispatcher);
__decl_isr(3, irq_dispatcher);
__decl_isr(4, irq_dispatcher);
__decl_isr(5, irq_dispatcher);
__decl_isr(6, irq_dispatcher);
__decl_isr(7, irq_dispatcher);
__decl_isr(8, irq_dispatcher);
__decl_isr(9, irq_dispatcher);
__decl_isr(10, irq_dispatcher);
__decl_isr(11, irq_dispatcher);
__decl_isr(12, irq_dispatcher);
__decl_isr(13, irq_dispatcher);
__decl_isr(14, irq_dispatcher);
__decl_isr(15, irq_dispatcher);
__decl_isr(16, irq_dispatcher);
__decl_isr(17, irq_dispatcher);
__decl_isr(18, irq_dispatcher);
__decl_isr(19, irq_dispatcher);
__decl_isr(20, irq_dispatcher);
__decl_isr(21, irq_dispatcher);
__decl_isr(22, irq_dispatcher);
__decl_isr(23, irq_dispatcher);
__decl_isr(24, irq_dispatcher);
__decl_isr(25, irq_dispatcher);
__decl_isr(26, irq_dispatcher);
__decl_isr(27, irq_dispatcher);
__decl_isr(28, irq_dispatcher);
__decl_isr(29, irq_dispatcher);
__decl_isr(30, irq_dispatcher);
__decl_isr(31, irq_dispatcher);
__decl_isr(32, irq_dispatcher);
__decl_isr(33, irq_dispatcher);
__decl_isr(34, irq_dispatcher);
__decl_isr(35, irq_dispatcher);
__decl_isr(36, irq_dispatcher);
__decl_isr(37, irq_dispatcher);
__decl_isr(38, irq_dispatcher);
__decl_isr(39, irq_dispatcher);
__decl_isr(40, irq_dispatcher);
__decl_isr(41, irq_dispatcher);
__decl_isr(42, irq_dispatcher);
__decl_isr(43, irq_dispatcher);
__decl_isr(44, irq_dispatcher);
__decl_isr(45, irq_dispatcher);
__decl_isr(46, irq_dispatcher);
__decl_isr(47, irq_dispatcher);


gate_t idt[irq::number_of_irqs::value] _irq_aligned_;
table_pointer idt_pointer _irq_aligned_;

} // namespace

void irq::init() {
	// See comment__why_not_use_counter
	__decl_common_gate(0, idt);
	__decl_common_gate(1, idt);
	__decl_common_gate(2, idt);
	__decl_common_gate(3, idt);
	__decl_common_gate(4, idt);
	__decl_common_gate(5, idt);
	__decl_common_gate(6, idt);
	__decl_common_gate(7, idt);
	__decl_common_gate(8, idt);
	__decl_common_gate(9, idt);
	__decl_common_gate(10, idt);
	__decl_common_gate(11, idt);
	__decl_common_gate(12, idt);
	__decl_common_gate(13, idt);
	__decl_common_gate(14, idt);
	__decl_common_gate(15, idt);
	__decl_common_gate(16, idt);
	__decl_common_gate(17, idt);
	__decl_common_gate(18, idt);
	__decl_common_gate(19, idt);
	__decl_common_gate(20, idt);
	__decl_common_gate(21, idt);
	__decl_common_gate(22, idt);
	__decl_common_gate(23, idt);
	__decl_common_gate(24, idt);
	__decl_common_gate(25, idt);
	__decl_common_gate(26, idt);
	__decl_common_gate(27, idt);
	__decl_common_gate(28, idt);
	__decl_common_gate(29, idt);
	__decl_common_gate(30, idt);
	__decl_common_gate(31, idt);
	__decl_common_gate(32, idt);
	__decl_common_gate(33, idt);
	__decl_common_gate(34, idt);
	__decl_common_gate(35, idt);
	__decl_common_gate(36, idt);
	__decl_common_gate(37, idt);
	__decl_common_gate(38, idt);
	__decl_common_gate(39, idt);
	__decl_common_gate(40, idt);
	__decl_common_gate(41, idt);
	__decl_common_gate(42, idt);
	__decl_common_gate(43, idt);
	__decl_common_gate(44, idt);
	__decl_common_gate(45, idt);
	__decl_common_gate(46, idt);
	__decl_common_gate(47, idt);

	idt_pointer.limit = sizeof(idt) - 1;
	idt_pointer.base = idt;
	asm volatile("lidt %0"::"m" (idt_pointer));
}


void irq::register_handler(irq_t vector, irq_handler_t routine) {
	if (!registered_isrs[vector].push_front(routine)) {
		panic("Failed to register ISR");
	}
}

namespace {


void irq_dispatcher(irq::irq_t vector) {
	HandlersChain &handlers = registered_isrs[vector];
	lib::for_each(handlers.begin(), handlers.end(),
		[vector] (const irq::irq_handler_t &handler) -> void {
			handler(vector);
	});
}

}
