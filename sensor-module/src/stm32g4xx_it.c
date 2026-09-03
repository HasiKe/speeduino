/**
 * @file stm32g4xx_it.c
 * @brief Core interrupt handlers.
 *
 * The peripheral handlers live next to the drivers that own them: the ADC DMA
 * channels are in analog.c.
 */
#include "board.h"

void NMI_Handler(void)
{
  board_panic();
}

void HardFault_Handler(void)
{
  board_panic();
}

void MemManage_Handler(void)
{
  board_panic();
}

void BusFault_Handler(void)
{
  board_panic();
}

void UsageFault_Handler(void)
{
  board_panic();
}

void SVC_Handler(void)
{
}

void DebugMon_Handler(void)
{
}

void PendSV_Handler(void)
{
}

void SysTick_Handler(void)
{
  HAL_IncTick();
}
