#include <bolgenos-ng/pit.hpp>

#include <bolgenos-ng/error.h>

#include <bolgenos-ng/asm.hpp>
#include <bolgenos-ng/interrupt_controller.hpp>
#include <bolgenos-ng/irq.hpp>
#include <bolgenos-ng/mem_utils.hpp>
#include <bolgenos-ng/stdtypes.hpp>
#include <bolgenos-ng/time.hpp>

#include <lib/ostream.hpp>

#include "frequency_divider.hpp"

#include "config.h"


namespace {


/// \brief PIT frequency.
///
/// Frequency of PIT chip 8253/8254. This frequency will be divided
/// by configured value.
constexpr unsigned long PIT_FREQUENCY = 1193182;


/// \brief Max divider.
///
/// Maximal number that can be used as frequency divider.
constexpr unsigned long MAX_DIVIDER = 65535;


enum pit_port: uint16_t {
	timer			= 0x40,
	speaker			= 0x42,
	cmd			= 0x43,
};


enum num_mode: uint8_t {
	bcd			= 0 << 0,
	bin			= 1 << 0,
};


enum oper_mode: uint8_t {
	m0			= 0 << 1,
	m1			= 1 << 1,
	m2			= 2 << 1,
	m3			= 3 << 1,
	m4			= 4 << 1,
	m5			= 5 << 1,
};


enum acc_mode: uint8_t {
	latch			= 0 << 4,
	low			= 1 << 4,
	high			= 2 << 4,
	both			= 3 << 4,
};


enum pit_channel: uint8_t {
	ch0			= 0 << 6,
	ch1			= 1 << 6,
	ch2			= 2 << 6,
	back			= 3 << 6,
};


class PitIRQHandler: public irq::IRQHandler {
public:
	PitIRQHandler() = delete;
	PitIRQHandler(const PitIRQHandler&) = delete;
	PitIRQHandler(PitIRQHandler&&) = delete;
	PitIRQHandler& operator =(const PitIRQHandler&) = delete;
	PitIRQHandler& operator =(PitIRQHandler&&) = delete;


	explicit PitIRQHandler(pit::details::FrequencyDivider *divider)
			:_divider(divider)
	{
	}

	virtual ~PitIRQHandler()
	{
		delete _divider;
		_divider = nullptr;
	}

	status_t handle_irq(irq::irq_t vector __attribute__((unused))) override
	{
		if (_divider->do_tick()) {
#if VERBOSE_TIMER_INTERRUPT
			lib::cout << "jiffy #" << time::jiffies << lib::endl;
#endif
			++time::jiffies;
		}
		return status_t::HANDLED;
	}

private:
	pit::details::FrequencyDivider *_divider;
};

} // namespace



void pit::init() {
	const irq::irq_t timer_irq = devices::InterruptController::instance()->min_irq_vector() + 0;

	auto freq_divider = new pit::details::FrequencyDivider();
	freq_divider->set_frequency(HZ, PIT_FREQUENCY, MAX_DIVIDER);
	if (freq_divider->is_low_frequency())
		lib::cwarn << "PIT: losing accuracy of timer" << lib::endl;

	irq::InterruptsManager::instance()->add_handler(timer_irq, new PitIRQHandler(freq_divider));

	uint8_t cmd = pit_channel::ch0|acc_mode::latch|oper_mode::m2|num_mode::bin;

	x86::outb(pit_port::cmd, cmd);
	x86::outb(pit_port::timer, bitmask(freq_divider->pit_timeout(), 0, 0xff));
	x86::outb(pit_port::timer, bitmask(freq_divider->pit_timeout(), 8, 0xff));
}

