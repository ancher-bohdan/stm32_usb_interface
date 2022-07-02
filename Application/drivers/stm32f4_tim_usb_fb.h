#ifndef __STM32F4_TIM_USB_FB__
#define __STM32F4_TIM_USB_FB__

void TIM_FB_Init();
void TIM_FB_Start();
void TIM_FB_Stop();

void TIM_FB_Set_trigger_fb_transaction_callback(void (*trigger_fb_transaction)(uint32_t mclk_to_sof_ratio));

#endif
