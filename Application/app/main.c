#include "stm32f4xx.h"
#include "stm324xg_eval.h"
#include "stm32f4_adc_driver.h"
#include "stm32f4_tim_usb_fb.h"

#include "dsp.h"

#include <stdbool.h>

void Delay_blocking(__IO uint32_t timeout);

static __IO int32_t TimingDelay;

extern uint8_t test_flag_work;

static void send_mclk_to_sof_ratio_feedback(uint32_t feedback)
{
}

int main(void)
{
  RCC_ClocksTypeDef RCC_Clocks;

  /* Initialize LEDS */
  STM_EVAL_LEDInit(LED1);
  STM_EVAL_LEDInit(LED2);
  STM_EVAL_LEDInit(LED3);
  STM_EVAL_LEDInit(LED4);
       
  /* SysTick end of count event each 10ms */
  RCC_GetClocksFreq(&RCC_Clocks);
  SysTick_Config(RCC_Clocks.HCLK_Frequency / 1000);

  RCC_HSEConfig(RCC_HSE_ON);
  while(!RCC_WaitForHSEStartUp())
  {
  }

  dsp_init();

  adc_init();
  adc_on();

  TIM_FB_Init();
  TIM_FB_Set_trigger_fb_transaction_callback(send_mclk_to_sof_ratio_feedback);

  while (1)
  {
    dsp_process();
  }
}

void Delay_blocking(__IO uint32_t timeout)
{
  TimingDelay = (int32_t)(timeout & 0x7FFFFFFF);
  
  while(TimingDelay != 0);
}

void TimingDelay_Decrement(void)
{
  TimingDelay--;
}

#ifdef  USE_FULL_ASSERT

/**
  * @brief  Reports the name of the source file and the source line number
  *   where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t* file, uint32_t line)
{ 
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

  /* Infinite loop */
  while (1)
  {
  }
}
#endif

