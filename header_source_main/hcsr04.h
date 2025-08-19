/*
 * hcsr04.h
 *
 *  Created on: Aug 19, 2025
 *      Author: Enes
 */

#ifndef INC_HCSR04_H_
#define INC_HCSR04_H_

#include "stm32f4xx_hal.h"
#include <stdbool.h>

struct HCSR04_t;
typedef struct HCSR04_t* HCSR04_Handle_t;

typedef struct {
    TIM_HandleTypeDef* tim_handle;         // Input Capture için kullanılacak Timer.
    uint32_t            tim_ic_channel;     // Input Capture için Timer kanalı.
    GPIO_TypeDef* trig_port;          // Trig pininin portu.
    uint16_t            trig_pin;           // Trig pininin numarası.
} HCSR04_Config_t;

HCSR04_Handle_t HCSR04_Init(HCSR04_Config_t* config);

void HCSR04_Destroy(HCSR04_Handle_t dev);

void HCSR04_Trigger(HCSR04_Handle_t dev);

bool HCSR04_IsMeasurementReady(HCSR04_Handle_t dev);

float HCSR04_GetDistanceCm(HCSR04_Handle_t dev);

void HCSR04_Register_For_IRQ(HCSR04_Handle_t dev);

void HCSR04_TIM_IC_IRQHandler(TIM_HandleTypeDef *htim);

#endif /* INC_HCSR04_H_ */
