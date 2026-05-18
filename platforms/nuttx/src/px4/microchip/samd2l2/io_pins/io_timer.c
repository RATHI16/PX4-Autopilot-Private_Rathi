/****************************************************************************
 * SAMD21 io_timer stub — build placeholder.
 * Full TCC channel driver goes here in Phase 2.
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>
#include <px4_arch/io_timer.h>

/* Required extern data referenced by io_timer.h */
io_timer_channel_allocation_t allocations[IOTimerChanModeSize];

const io_timers_t io_timers[MAX_IO_TIMERS];
const io_timers_channel_mapping_t io_timers_channel_mapping;
const timer_io_channels_t timer_io_channels[MAX_TIMER_IO_CHANNELS];

const io_timers_t led_pwm_timers[MAX_LED_TIMERS];
const timer_io_channels_t led_pwm_channels[MAX_TIMER_LED_CHANNELS];

int io_timer_init_timer(unsigned timer)                                { return 0; }
int io_timer_set_rate(unsigned timer, unsigned rate)                   { return 0; }
int io_timer_channel_init(unsigned channel, io_timer_channel_mode_t mode,
                          channel_handler_t handler, void *context)    { return 0; }
int io_timer_set_enable(bool state, io_timer_channel_mode_t mode,
                        io_timer_channel_allocation_t channels_mask)   { return 0; }
uint16_t io_channel_get_ccr(unsigned channel)                          { return 0; }
int io_timer_set_ccr(unsigned channel, uint16_t value)                 { return 0; }
uint32_t io_timer_get_group(unsigned timer)                            { return 0; }
int io_timer_validate_channel_index(unsigned channel)                  { return 0; }
int io_timer_is_channel_free(unsigned channel)                         { return 1; }
int io_timer_free_channel(unsigned channel)                            { return 0; }
int io_timer_get_channel_mode(unsigned channel)                        { return IOTimerChanMode_NotUsed; }
int io_timer_get_mode_channels(io_timer_channel_mode_t mode)           { return 0; }
void io_timer_trigger(void)                                            { }
uint32_t io_timer_channel_get_gpio_output(unsigned channel)            { return 0; }
uint32_t io_timer_channel_get_as_pwm_input(unsigned channel)           { return 0; }
