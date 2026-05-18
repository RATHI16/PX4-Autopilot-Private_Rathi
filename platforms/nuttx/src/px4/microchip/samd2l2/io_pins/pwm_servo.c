/****************************************************************************
 * SAMD21 PWM servo output stub — build placeholder.
 * Real TCC implementation goes here in Phase 2 (after build is green).
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>
#include <drivers/drv_pwm_output.h>

int up_pwm_servo_init(uint32_t channel_mask)     { return 0; }
void up_pwm_servo_deinit(uint32_t channel_mask)  { }
void up_pwm_servo_arm(bool armed, uint32_t mask) { }
int  up_pwm_servo_set(unsigned ch, uint16_t pos) { return 0; }
uint16_t up_pwm_servo_get(unsigned ch)   { return 0; }
int  up_pwm_servo_set_rate_group_update(unsigned group, unsigned rate) { return 0; }
uint32_t up_pwm_servo_get_rate_group(unsigned group) { return 0; }
void up_pwm_update(unsigned channel_mask)        { }
