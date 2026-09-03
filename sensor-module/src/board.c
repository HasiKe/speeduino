#include "board.h"
#include "config.h"
#include "fan.h"

/**
 * @brief 16MHz HSE resonator to 170MHz system clock.
 *
 * PLL: 16MHz / M=4 = 4MHz reference, * N=85 = 340MHz VCO, / R=2 = 170MHz.
 * 170MHz needs the boost voltage range and four flash wait states.
 */
static void clock_init(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};

  /* Boost mode has to be selected before the clock is raised above 150MHz,
   * and the PWR block needs its clock before it can be written to. */
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState = RCC_HSE_ON;
  osc.PLL.PLLState = RCC_PLL_ON;
  osc.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM = RCC_PLLM_DIV4;
  osc.PLL.PLLN = 85;
  osc.PLL.PLLP = RCC_PLLP_DIV2;
  osc.PLL.PLLQ = RCC_PLLQ_DIV2;
  osc.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&osc) != HAL_OK)
  {
    board_panic();
  }

  clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV1;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_4) != HAL_OK)
  {
    board_panic();
  }
}

/**
 * @brief Peripheral kernel clocks.
 *
 * FDCAN runs straight off the 16MHz HSE. That divides exactly into 500 kbit/s
 * with a 32 time quantum bit, so the bit timing has no rounding error and does
 * not move if the system clock is ever changed.
 */
static void peripheral_clock_init(void)
{
  RCC_PeriphCLKInitTypeDef periph = {0};

  periph.PeriphClockSelection = RCC_PERIPHCLK_FDCAN | RCC_PERIPHCLK_ADC12;
  periph.FdcanClockSelection = RCC_FDCANCLKSOURCE_HSE;
  /* Asynchronous ADC clock off the system clock. analog.c divides it by 4,
   * giving 42.5MHz, inside the 60MHz the ADC is rated for. */
  periph.Adc12ClockSelection = RCC_ADC12CLKSOURCE_SYSCLK;
  if (HAL_RCCEx_PeriphCLKConfig(&periph) != HAL_OK)
  {
    board_panic();
  }
}

void board_init(void)
{
  HAL_Init();
  clock_init();
  peripheral_clock_init();

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();
  __HAL_RCC_DMAMUX1_CLK_ENABLE();
}

void board_panic(void)
{
  /* Losing the intercooler fan is worse than running it needlessly, so drive
   * it hard on the way out. The fan module may not be up yet, in which case
   * this does nothing and the pin stays where the 10k gate pull down puts it. */
  __disable_irq();
  fan_set_override(FAN_FAILSAFE_DUTY_PCT);

  for (;;)
  {
    /* An independent watchdog would reset us here. Until one is fitted this
     * stops the module rather than letting it publish stale readings. */
  }
}

/** @brief Called by the HAL for every unhandled error. */
void Error_Handler(void)
{
  board_panic();
}
