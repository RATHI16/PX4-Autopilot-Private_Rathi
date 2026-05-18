/****************************************************************************
 * boards/microchip/samd21-io/src/timer_config.cpp
 *
 * PWM timer channel mapping for SAMD21J18A PX4IO.
 *
 * Timer groups:
 *   TCC2: 2 channels  → PWM 1-2  (PA0, PA1)
 *   TCC1: 2 channels  → PWM 3-4  (PA6, PA7)
 *   TCC0: 4 channels  → PWM 5-8  (PA8, PA9, PA10, PA19)
 *
 * All three groups can be set to independent PWM rates via
 * PX4IO_P_SETUP_PWM_RATE_GROUP0/1/2.
 ****************************************************************************/

#include <px4_arch/io_timer_hw_description.h>

constexpr io_timers_t io_timers[MAX_IO_TIMERS] = {
	initIOTimer(Timer::TCC2),
	initIOTimer(Timer::TCC1),
	initIOTimer(Timer::TCC0),
};

constexpr timer_io_channels_t timer_io_channels[MAX_TIMER_IO_CHANNELS] = {
	/* TCC2 group — channels 1-2 */
	initIOTimerChannel(io_timers, {Timer::TCC2, Timer::Channel1}, {GPIO::PortA, GPIO::Pin0}),
	initIOTimerChannel(io_timers, {Timer::TCC2, Timer::Channel2}, {GPIO::PortA, GPIO::Pin1}),
	/* TCC1 group — channels 3-4 */
	initIOTimerChannel(io_timers, {Timer::TCC1, Timer::Channel1}, {GPIO::PortA, GPIO::Pin6}),
	initIOTimerChannel(io_timers, {Timer::TCC1, Timer::Channel2}, {GPIO::PortA, GPIO::Pin7}),
	/* TCC0 group — channels 5-8 */
	initIOTimerChannel(io_timers, {Timer::TCC0, Timer::Channel1}, {GPIO::PortA, GPIO::Pin8}),
	initIOTimerChannel(io_timers, {Timer::TCC0, Timer::Channel2}, {GPIO::PortA, GPIO::Pin9}),
	initIOTimerChannel(io_timers, {Timer::TCC0, Timer::Channel3}, {GPIO::PortA, GPIO::Pin10}),
	initIOTimerChannel(io_timers, {Timer::TCC0, Timer::Channel4}, {GPIO::PortA, GPIO::Pin19}),
};

constexpr io_timers_channel_mapping_t io_timers_channel_mapping =
	initIOTimerChannelMapping(io_timers, timer_io_channels);
