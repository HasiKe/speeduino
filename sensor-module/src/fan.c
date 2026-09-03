#include "fan.h"

#include "analog.h"
#include "board.h"
#include "config.h"

/* TIM3 runs from a 1MHz tick so the period fits the 16 bit counter at any
 * sensible PWM frequency. */
#define FAN_TIMER_TICK_HZ      1000000UL

static TIM_HandleTypeDef fanTimer;
static bool initialised = false;
static uint8_t currentDuty = 0U;
static bool fanRunning = false;

static bool overrideActive = false;
static uint8_t overrideDuty = 0U;
static uint32_t overrideStamp = 0U;

static uint32_t periodTicks = 0UL;

static void apply_duty(uint8_t duty_pct)
{
  if (duty_pct > 100U)
  {
    duty_pct = 100U;
  }
  currentDuty = duty_pct;

  if (!initialised)
  {
    return;
  }

  uint32_t compare = (periodTicks * (uint32_t)duty_pct) / 100UL;
  __HAL_TIM_SET_COMPARE(&fanTimer, TIM_CHANNEL_3, compare);
}

void fan_init(void)
{
  GPIO_InitTypeDef gpio = {0};
  TIM_OC_InitTypeDef oc = {0};

  __HAL_RCC_TIM3_CLK_ENABLE();

  gpio.Pin = FAN_PWM_PIN;
  gpio.Mode = GPIO_MODE_AF_PP;
  gpio.Pull = GPIO_PULLDOWN; /* Backs up the 10k gate pull down on the board */
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = GPIO_AF2_TIM3;
  HAL_GPIO_Init(FAN_PWM_PORT, &gpio);

  periodTicks = FAN_TIMER_TICK_HZ / FAN_PWM_FREQ_HZ;

  fanTimer.Instance = TIM3;
  fanTimer.Init.Prescaler = (uint32_t)((SYSCLK_FREQ_HZ / FAN_TIMER_TICK_HZ) - 1UL);
  fanTimer.Init.CounterMode = TIM_COUNTERMODE_UP;
  fanTimer.Init.Period = periodTicks - 1UL;
  fanTimer.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  fanTimer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&fanTimer) != HAL_OK)
  {
    board_panic();
  }

  oc.OCMode = TIM_OCMODE_PWM1;
  oc.Pulse = 0U;
  oc.OCPolarity = TIM_OCPOLARITY_HIGH;
  oc.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&fanTimer, &oc, TIM_CHANNEL_3) != HAL_OK)
  {
    board_panic();
  }

  if (HAL_TIM_PWM_Start(&fanTimer, TIM_CHANNEL_3) != HAL_OK)
  {
    board_panic();
  }

  initialised = true;
  apply_duty(0U);
}

/**
 * @brief Local control law on the hotter intercooler water sensor.
 *
 * Off below the switch on temperature, then a straight ramp from the minimum
 * duty up to full by the time the full duty temperature is reached. Switching
 * off again needs the temperature to drop through the hysteresis band, so the
 * fan does not chatter around the threshold.
 */
static uint8_t local_duty(void)
{
  int16_t tempC = 0;

  if (!analog_intercooler_temp_c(&tempC))
  {
    /* Both water sensors are unusable. Running the fan needlessly is cheaper
     * than cooking the intercooler because a sensor fell off. */
    return (uint8_t)FAN_FAILSAFE_DUTY_PCT;
  }

  int16_t switchOff = (int16_t)(FAN_ON_TEMP_C - FAN_OFF_HYSTERESIS_C);

  if (fanRunning)
  {
    if (tempC <= switchOff)
    {
      fanRunning = false;
      return 0U;
    }
  }
  else
  {
    if (tempC < (int16_t)FAN_ON_TEMP_C)
    {
      return 0U;
    }
    fanRunning = true;
  }

  if (tempC >= (int16_t)FAN_FULL_TEMP_C)
  {
    return 100U;
  }

  int32_t span = (int32_t)FAN_FULL_TEMP_C - (int32_t)FAN_ON_TEMP_C;
  if (span <= 0)
  {
    return 100U;
  }

  int32_t above = (int32_t)tempC - (int32_t)FAN_ON_TEMP_C;
  if (above < 0)
  {
    above = 0;
  }

  int32_t duty = (int32_t)FAN_MIN_DUTY_PCT
               + ((100 - (int32_t)FAN_MIN_DUTY_PCT) * above) / span;

  if (duty > 100) { duty = 100; }

  return (uint8_t)duty;
}

void fan_update(void)
{
  if (overrideActive && board_elapsed(overrideStamp, CAN_OVERRIDE_TIMEOUT_MS))
  {
    /* The ECU stopped talking. Fall back to local control rather than holding
     * whatever duty was last commanded. */
    overrideActive = false;
  }

  apply_duty(overrideActive ? overrideDuty : local_duty());
}

void fan_set_override(uint8_t duty_pct)
{
  if (duty_pct == CAN_FAN_RELEASE)
  {
    overrideActive = false;
    return;
  }

  overrideDuty = (duty_pct > 100U) ? 100U : duty_pct;
  overrideActive = true;
  overrideStamp = board_millis();
  apply_duty(overrideDuty);
}

uint8_t fan_duty(void)
{
  return currentDuty;
}

bool fan_override_active(void)
{
  return overrideActive;
}
