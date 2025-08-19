/*
 * hcsr04.c
 *
 *  Created on: Aug 19, 2025
 *      Author: Enes
 */

#include "hcsr04.h"
#include <stdlib.h>

typedef enum{
	CAPTURE_IDLE,
	WAITING_FOR_RISING_EDGE,
	WAITING_FOR_FALLING_EDGE
}CaptureState_t;

struct HCSR04_t{
	TIM_HandleTypeDef* tim_handle;
	uint32_t tim_ic_channel;
	GPIO_TypeDef* trig_port;
	uint16_t trig_pin;

	volatile CaptureState_t capture_state;
	volatile uint32_t rising_edge_time;
	volatile uint32_t falling_edge_time;
	volatile bool is_ready;
	volatile float distance_cm;
};


// --- KAYIT MEKANİZMASI ---
static HCSR04_Handle_t registered_handles[16] = {NULL}; // Birden fazla timer'ı desteklemek için bir dizi
static TIM_HandleTypeDef* registered_timers[16] = {NULL};


void HCSR04_Register_For_IRQ(HCSR04_Handle_t dev) {
    for(int i = 0; i < 16; i++) {
        if(registered_timers[i] == NULL || registered_timers[i] == dev->tim_handle) {
            registered_handles[i] = dev;
            registered_timers[i] = dev->tim_handle;
            return;
        }
    }
}

// --- INIT ve DESTROY ---
HCSR04_Handle_t HCSR04_Init(HCSR04_Config_t* config) {
    HCSR04_Handle_t dev = (HCSR04_Handle_t)malloc(sizeof(struct HCSR04_t));
    if(dev == NULL) return NULL;

    *dev = (struct HCSR04_t){
        .tim_handle = config->tim_handle,
        .tim_ic_channel = config->tim_ic_channel,
        .trig_port = config->trig_port,
        .trig_pin = config->trig_pin,
        .capture_state = CAPTURE_IDLE,
        .is_ready = false
    };
    return dev;
}

void HCSR04_Destroy(HCSR04_Handle_t dev) { /* ... free(dev) ... */ }

// --- ANA FONKSİYONLAR ---

// Kısa bir busy-wait döngüsü ile 10us'lik bir gecikme.
// Bu kadar kısa süreler için HAL_Delay kullanmaktan daha iyidir.
static void delay_us(uint32_t us) {
    volatile uint32_t counter = us * (SystemCoreClock / 1000000 / 5); // Yaklaşık hesap
    while(counter--);
}

void HCSR04_Trigger(HCSR04_Handle_t dev) {
    if (dev == NULL || dev->capture_state != CAPTURE_IDLE) return;

    dev->is_ready = false;
    dev->capture_state = WAITING_FOR_RISING_EDGE;

    // Timer'ı yükselen kenarı yakalamak için ayarla ve interrupt'ı başlat
    __HAL_TIM_SET_CAPTUREPOLARITY(dev->tim_handle, dev->tim_ic_channel, TIM_INPUTCHANNELPOLARITY_RISING);
    HAL_TIM_IC_Start_IT(dev->tim_handle, dev->tim_ic_channel);

    // 10us'lik tetikleme sinyalini gönder
    HAL_GPIO_WritePin(dev->trig_port, dev->trig_pin, GPIO_PIN_SET);
    delay_us(10);
    HAL_GPIO_WritePin(dev->trig_port, dev->trig_pin, GPIO_PIN_RESET);
}

bool HCSR04_IsMeasurementReady(HCSR04_Handle_t dev) {
    return (dev != NULL) ? dev->is_ready : false;
}

float HCSR04_GetDistanceCm(HCSR04_Handle_t dev) {
    if (dev != NULL && dev->is_ready) {
        return dev->distance_cm;
    }
    return -1.0f; // Hata veya hazır değil
}

// --- INTERRUPT YÖNLENDİRİCİSİ ---
void HCSR04_TIM_IC_IRQHandler(TIM_HandleTypeDef *htim) {
    HCSR04_Handle_t dev = NULL;
    for(int i = 0; i < 16; i++) {
        if(registered_timers[i] == htim) {
            dev = registered_handles[i];
            break;
        }
    }
    if (dev == NULL) return;

    if (dev->capture_state == WAITING_FOR_RISING_EDGE) {
        // 1. Yükselen kenar yakalandı, başlangıç zamanını kaydet
        dev->rising_edge_time = HAL_TIM_ReadCapturedValue(htim, dev->tim_ic_channel);

        // 2. Timer'ı düşen kenarı yakalamak için yeniden ayarla
        __HAL_TIM_SET_CAPTUREPOLARITY(htim, dev->tim_ic_channel, TIM_INPUTCHANNELPOLARITY_FALLING);

        // 3. Durumu güncelle
        dev->capture_state = WAITING_FOR_FALLING_EDGE;
    }
    else if (dev->capture_state == WAITING_FOR_FALLING_EDGE) {
        // 4. Düşen kenar yakalandı, bitiş zamanını kaydet
        dev->falling_edge_time = HAL_TIM_ReadCapturedValue(htim, dev->tim_ic_channel);

        // 5. Timer'ı durdur, artık işimiz bitti
        HAL_TIM_IC_Stop_IT(htim, dev->tim_ic_channel);

        // 6. Pulse süresini hesapla (timer overflow'u da hesaba kat)
        uint32_t pulse_width;
        if (dev->falling_edge_time > dev->rising_edge_time) {
            pulse_width = dev->falling_edge_time - dev->rising_edge_time;
        } else { // Timer sıfırlanmış (overflow)
            pulse_width = (dev->tim_handle->Instance->ARR - dev->rising_edge_time) + dev->falling_edge_time;
        }

        // 7. Mesafeyi hesapla (1µs/tick ayarı sayesinde matematik basit)
        // Mesafe (cm) = (süre_us * 0.0343) / 2
        dev->distance_cm = (float)pulse_width * 0.01715f;

        // 8. Ölçümün bittiğini ve sonucun hazır olduğunu bildir
        dev->is_ready = true;
        dev->capture_state = CAPTURE_IDLE;

        // 9. Bir sonraki ölçüm için timer'ı tekrar yükselen kenara ayarla
        __HAL_TIM_SET_CAPTUREPOLARITY(htim, dev->tim_ic_channel, TIM_INPUTCHANNELPOLARITY_RISING);
    }
}
