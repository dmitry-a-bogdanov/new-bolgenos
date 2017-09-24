#include <bolgenos-ng/irq.hpp>

#include <algorithm>
#include <forward_list>

#include <bolgenos-ng/compiler.h>
#include <bolgenos-ng/error.h>

#include <bolgenos-ng/asm.hpp>
#include <bolgenos-ng/interrupt_controller.hpp>
#include <bolgenos-ng/mem_utils.hpp>

#include <lib/ostream.hpp>

#include <m4/idt.hpp>

irq::InterruptsManager *irq::InterruptsManager::_instance = nullptr;


namespace
{

bolgenos::irq::dispatcher_function_t *dispatcher;

}


void bolgenos::irq::set_dispatcher(dispatcher_function_t *f)
{
	dispatcher = f;
}


bolgenos::irq::dispatcher_function_t* bolgenos::irq::get_dispatcher()
{
	return dispatcher;
}


irq::InterruptsManager::InterruptsManager()
{
	bolgenos::irq::set_dispatcher(handle_irq);
	idt_pointer.base = bolgenos::irq::idt;
	uint16_t idt_size = irq::NUMBER_OF_LINES*irq::GATE_SIZE - 1;
	idt_pointer.limit = idt_size;
	asm volatile("lidt %0"::"m" (idt_pointer));
}

irq::InterruptsManager *irq::InterruptsManager::instance()
{
	if (!_instance) {
		_instance = new InterruptsManager{};
	}
	return _instance;
}


void irq::InterruptsManager::add_handler(irq_t vector, IRQHandler *handler)
{
	_irq_handlers[vector].push_front(handler);
}


void irq::InterruptsManager::add_handler(exception_t exception, ExceptionHandler *handler)
{
	_exceptions_handlers[exception].push_front(handler);
}


bool irq::InterruptsManager::is_exception(irq::irq_t vector)
{
	return vector < exception_t::max;
}


irq::IRQHandler::status_t irq::InterruptsManager::dispatch_exception(exception_t exception,
		stack_ptr_t frame_pointer)
{
	auto& handlers = _exceptions_handlers[exception];

	if (handlers.empty()) {
		return irq::IRQHandler::status_t::NONE;
	}

	std::for_each(handlers.begin(), handlers.end(),
		[frame_pointer](irq::ExceptionHandler *handler) -> void {
			handler->handle_exception(frame_pointer);
	});

	return irq::IRQHandler::status_t::HANDLED;
}


irq::IRQHandler::status_t irq::InterruptsManager::dispatch_interrupt(irq_t vector)
{
	auto& handlers = _irq_handlers[vector];
	auto used_handler = std::find_if(handlers.begin(), handlers.end(),
		[vector] (irq::IRQHandler* handler) -> bool {
			return handler->handle_irq(vector) == irq::IRQHandler::status_t::HANDLED;
	});
	if (used_handler == handlers.end()) {
		return irq::IRQHandler::status_t::NONE;
	}
	return irq::IRQHandler::status_t::HANDLED;

}


void irq::InterruptsManager::handle_irq(irq_t vector, void *frame)
{
	irq::IRQHandler::status_t status;

	auto manager = irq::InterruptsManager::instance();
	if (is_exception(vector)) {
		status = manager->dispatch_exception(static_cast<exception_t>(vector), frame);
	} else {
		status = manager->dispatch_interrupt(vector);
	}

	if (status != irq::IRQHandler::status_t::HANDLED) {
		lib::ccrit << "Unhandled IRQ" << vector << lib::endl;
		panic("Fatal interrupt");
	}

	devices::InterruptController::instance()->end_of_interrupt(vector);
}




lib::ostream& irq::operator <<(lib::ostream& out,
		const irq::registers_dump_t& regs)
{
	lib::scoped_format_guard format_guard(out);

	out	<< lib::setw(0) << lib::hex << lib::setfill(' ');
	out	<< " eax = "
			<< lib::setw(8) << lib::setfill('0')
				<< regs.eax
			<< lib::setfill(' ') << lib::setw(0)
		<< ' '
		<< " ebx = " << lib::setw(8) << regs.ebx << lib::setw(0) << ' '
		<< " ecx = " << lib::setw(8) << regs.ecx << lib::setw(0) << ' '
		<< " edx = " << lib::setw(8) << regs.edx << lib::setw(0) << ' '
		<< lib::endl
		<< " esi = " << lib::setw(8) << regs.esi << lib::setw(0) << ' '
		<< " edi = " << lib::setw(8) << regs.edi << lib::setw(0) << ' '
		<< " ebp = " << lib::setw(8) << regs.ebp << lib::setw(0) << ' '
		<< " esp = " << lib::setw(8) << regs.esp << lib::setw(0) << ' ';

	return out;
}


lib::ostream& irq::operator <<(lib::ostream& out,
		const irq::execution_info_dump_t& exe)
{
	lib::scoped_format_guard format_guard(out);

	out	<< lib::setw(0) << lib::hex
		<< "eflg = " << lib::setw(8) << exe.eflags << lib::setw(0) << ' '
		<< "  cs = " << lib::setw(8) << exe.cs << lib::setw(0) << ' '
		<< " eip = " << lib::setw(8) << exe.eip << lib::setw(0);

	return out;
}


lib::ostream& irq::operator <<(lib::ostream& out,
		const irq::int_frame_error_t& frame)
{
	lib::scoped_format_guard format_guard(out);

	out	<< lib::hex
		<< lib::setw(0) << " err = "
		<< lib::setw(8) << frame.error_code
		<< lib::endl
		<< frame.exe << lib::endl
		<< frame.regs;

	return out;
}


lib::ostream& irq::operator <<(lib::ostream& out,
		const irq::int_frame_noerror_t& frame)
{
	lib::scoped_format_guard format_guard(out);

	out	<< lib::setw(0) << lib::hex
		<< frame.exe << lib::endl
		<< frame.regs;

	return out;
}


// Compile-time guards
static_assert(sizeof(irq::registers_dump_t) == 8*4,
	"Wrong size of registers' dump structure");


static_assert(sizeof(irq::execution_info_dump_t) == 4 + 2 + 4,
	"Wrong size of execution info structure");


static_assert(sizeof(irq::int_frame_noerror_t) ==
	sizeof(irq::registers_dump_t) + sizeof(irq::execution_info_dump_t),
	"Wrong size of interrupt frame (no error code)");


static_assert(sizeof(irq::int_frame_error_t) ==
	sizeof(irq::int_frame_noerror_t) + 4,
	"Wrong size of interrupt frame (error code)");

