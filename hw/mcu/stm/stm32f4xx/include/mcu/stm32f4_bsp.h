/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef __MCU_STM32F4_BSP_H_
#define __MCU_STM32F4_BSP_H_

#include <hal/hal_gpio.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * BSP specific UART settings.
 */
struct stm32_uart_cfg {
    USART_TypeDef *suc_uart;		// UART dev registers
    volatile uint32_t *suc_rcc_reg;	// RCC register to modify
    uint32_t suc_rcc_dev;			// RCC device ID
    int8_t suc_pin_tx;				// physical pin assignment
    int8_t suc_pin_rx;				// physical pin assignment
	int8_t suc_pin_clk;				// physical pin assignment
    int8_t suc_pin_rts;				// physical pin assignment
    int8_t suc_pin_cts;				// physical pin assignment
    uint8_t suc_pin_af;				// alternate function register value (eg, GPIO_AF8_USART6)
	bool suc_clk_CPOL;				// clock polarity (CPOL=0 -> Data valid on falling edge, CPOL=1 -> Data valid on rising edge)
	bool suc_clk_CPHA;				// clock phase
    IRQn_Type suc_irqn;				// NVIC IRQ number
};

/*
 * Internal API for stm32f4xx mcu specific code.
 */
int hal_gpio_init_af(int pin, uint8_t af_type, enum hal_gpio_pull pull, uint8_t
od);

struct hal_flash;
extern struct hal_flash stm32f4_flash_dev;

#ifdef __cplusplus
}
#endif

#endif /* __MCU_STM32F4_BSP_H_ */
