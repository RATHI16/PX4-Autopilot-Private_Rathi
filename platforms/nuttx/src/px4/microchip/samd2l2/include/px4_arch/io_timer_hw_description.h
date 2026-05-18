/****************************************************************************
 * platforms/nuttx/src/px4/microchip/samd2l2/include/px4_arch/io_timer_hw_description.h
 *
 * SAMD21 TCC (Timer/Counter for Control) hardware description for PWM output.
 *
 * SAMD21 TCC peripherals:
 *   TCC0: 4 compare channels (CC0-CC3), 8 waveform outputs (WO[0:7])
 *   TCC1: 2 compare channels (CC0-CC1), 4 waveform outputs (WO[0:3])
 *   TCC2: 2 compare channels (CC0-CC1), 2 waveform outputs (WO[0:1])
 ****************************************************************************/

#pragma once

#include <px4_arch/io_timer.h>
#include <px4_platform_common/constexpr_util.h>
#include <hardware/samd21_memorymap.h>

/* -------------------------------------------------------------------------
 * Timer identifiers — mapped to SAMD21 TCC peripherals
 * -------------------------------------------------------------------------*/
namespace Timer
{
enum TimerChannel {
	Channel1 = 0,
	Channel2,
	Channel3,
	Channel4,
	Channel5,
	Channel6,
	Channel7,
	Channel8,
};

enum Timer {
	TCC0 = 0,
	TCC1,
	TCC2,
};
} /* namespace Timer */

namespace GPIO
{
enum PortGroup {
	PortA = 0,
	PortB,
};

enum Pin {
	Pin0 = 0, Pin1, Pin2,  Pin3,  Pin4,  Pin5,  Pin6,  Pin7,
	Pin8,     Pin9, Pin10, Pin11, Pin12, Pin13, Pin14, Pin15,
	Pin16,    Pin17,Pin18, Pin19, Pin20, Pin21, Pin22, Pin23,
	Pin24,    Pin25,Pin26, Pin27, Pin28, Pin29, Pin30, Pin31,
};
} /* namespace GPIO */

/* -------------------------------------------------------------------------
 * TCC base addresses
 * -------------------------------------------------------------------------*/
static constexpr uint32_t TCC_BASE[] = {
	SAM_TCC0_BASE,
	SAM_TCC1_BASE,
	SAM_TCC2_BASE,
};

/* -------------------------------------------------------------------------
 * io_timers_t — describes a single TCC peripheral.
 * Uses the rpi-style io_timers_t which only requires 'base'.
 * -------------------------------------------------------------------------*/
static inline constexpr io_timers_t
initIOTimer(Timer::Timer timer)
{
	io_timers_t t{};
	t.base = TCC_BASE[timer];
	return t;
}

struct TimerChannel_s { Timer::Timer timer; Timer::TimerChannel channel; };
struct GPIO_s         { GPIO::PortGroup port; GPIO::Pin pin; };

/* -------------------------------------------------------------------------
 * timer_io_channels_t — describes a single TCC waveform output pin.
 * -------------------------------------------------------------------------*/
static inline constexpr timer_io_channels_t
initIOTimerChannel(const io_timers_t timers[],
                   const TimerChannel_s tc,
                   const GPIO_s gpio)
{
	timer_io_channels_t ch{};
	ch.gpio_out      = 0;  /* pin mux configured in board init */
	ch.gpio_in       = 0;
	ch.timer_index   = (uint8_t)tc.timer;
	ch.timer_channel = (uint8_t)tc.channel;
	return ch;
}

/* -------------------------------------------------------------------------
 * io_timers_channel_mapping_t — empty placeholder
 * -------------------------------------------------------------------------*/
static inline constexpr io_timers_channel_mapping_t
initIOTimerChannelMapping(const io_timers_t timers[],
                          const timer_io_channels_t channels[])
{
	io_timers_channel_mapping_t m{};
	return m;
}

#define initIOTimerChannelCapture initIOTimerChannel
