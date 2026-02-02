// SPDX-License-Identifier: MIT
// STM32F4 IRQ Numbers
//
// Reference: RM0090 Table 61 (Vector table for STM32F405xx/07xx)
#pragma once

namespace umi::stm32f4::irq {

// DMA1 Stream IRQs
constexpr int DMA1_Stream0 = 11;
constexpr int DMA1_Stream1 = 12;
constexpr int DMA1_Stream2 = 13;
constexpr int DMA1_Stream3 = 14;
constexpr int DMA1_Stream4 = 15;
constexpr int DMA1_Stream5 = 16;
constexpr int DMA1_Stream6 = 17;
constexpr int DMA1_Stream7 = 47;

// DMA2 Stream IRQs
constexpr int DMA2_Stream0 = 56;
constexpr int DMA2_Stream1 = 57;
constexpr int DMA2_Stream2 = 58;
constexpr int DMA2_Stream3 = 59;
constexpr int DMA2_Stream4 = 60;
constexpr int DMA2_Stream5 = 68;
constexpr int DMA2_Stream6 = 69;
constexpr int DMA2_Stream7 = 70;

// USB OTG
constexpr int OTG_FS       = 67;
constexpr int OTG_HS_EP1_OUT = 74;
constexpr int OTG_HS_EP1_IN  = 75;
constexpr int OTG_HS       = 77;

// Timers
constexpr int TIM1_BRK_TIM9  = 24;
constexpr int TIM1_UP_TIM10  = 25;
constexpr int TIM1_TRG_COM_TIM11 = 26;
constexpr int TIM1_CC        = 27;
constexpr int TIM2           = 28;
constexpr int TIM3           = 29;
constexpr int TIM4           = 30;
constexpr int TIM5           = 50;
constexpr int TIM6_DAC       = 54;
constexpr int TIM7           = 55;
constexpr int TIM8_BRK_TIM12 = 43;
constexpr int TIM8_UP_TIM13  = 44;
constexpr int TIM8_TRG_COM_TIM14 = 45;
constexpr int TIM8_CC        = 46;

// I2C
constexpr int I2C1_EV = 31;
constexpr int I2C1_ER = 32;
constexpr int I2C2_EV = 33;
constexpr int I2C2_ER = 34;
constexpr int I2C3_EV = 72;
constexpr int I2C3_ER = 73;

// SPI
constexpr int SPI1 = 35;
constexpr int SPI2 = 36;
constexpr int SPI3 = 51;

// USART
constexpr int USART1 = 37;
constexpr int USART2 = 38;
constexpr int USART3 = 39;
constexpr int UART4  = 52;
constexpr int UART5  = 53;
constexpr int USART6 = 71;

// External interrupts
constexpr int EXTI0  = 6;
constexpr int EXTI1  = 7;
constexpr int EXTI2  = 8;
constexpr int EXTI3  = 9;
constexpr int EXTI4  = 10;
constexpr int EXTI9_5   = 23;
constexpr int EXTI15_10 = 40;

// ADC
constexpr int ADC = 18;

// CAN
constexpr int CAN1_TX  = 19;
constexpr int CAN1_RX0 = 20;
constexpr int CAN1_RX1 = 21;
constexpr int CAN1_SCE = 22;
constexpr int CAN2_TX  = 63;
constexpr int CAN2_RX0 = 64;
constexpr int CAN2_RX1 = 65;
constexpr int CAN2_SCE = 66;

// Other
constexpr int WWDG    = 0;
constexpr int PVD     = 1;
constexpr int RTC_WKUP = 3;
constexpr int FLASH   = 4;
constexpr int RCC     = 5;
constexpr int SDIO    = 49;
constexpr int FPU     = 81;

}  // namespace umi::stm32f4::irq
