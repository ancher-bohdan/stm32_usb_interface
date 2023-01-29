#ifndef __AUDIO_FEEDBACK_DRIVER__
#define __AUDIO_FEEDBACK_DRIVER__

#include "stm32f4xx_hal.h"
#include <stdint.h>

void FBCK_Init(uint32_t ideal_bitrate);
void FBCK_Start(void);
void FBCK_Stop(void);
void FBCK_adjust_bitrate(uint8_t free_buf_space);

__weak void FBCK_send_feedback(uint32_t feedback);

#endif /* __AUDIO_FEEDBACK_DRIVER__ */
