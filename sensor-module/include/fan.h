/**
 * @file fan.h
 * @brief Intercooler fan output on PB0, TIM3 channel 3.
 */
#ifndef FAN_H
#define FAN_H

#include <stdbool.h>
#include <stdint.h>

/** @brief Set up TIM3 for PWM on PB0 and start with the fan off. */
void fan_init(void);

/**
 * @brief Run the control law.
 *
 * Local control ramps the duty between the on and full temperatures of the
 * hotter intercooler water sensor, with hysteresis on the way down. A CAN
 * override replaces that until it times out, so losing the CAN link falls back
 * to local control rather than leaving the fan stuck.
 */
void fan_update(void);

/** @brief Apply a duty from the ECU. Pass @ref CAN_FAN_RELEASE to release. */
void fan_set_override(uint8_t duty_pct);

/** @brief The duty currently being driven, in percent. */
uint8_t fan_duty(void);

/** @brief True while a CAN override is in effect. */
bool fan_override_active(void);

#endif /* FAN_H */
