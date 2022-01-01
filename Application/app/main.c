#include "stm32f4xx.h"
#include "stm324xg_eval.h"
//#include "stm32f4_adc_driver.h"
#include "stm32f4_mems_mic.h"
#include "stm324xg_usb_audio_codec.h"

#include "usbd_audio_core.h"
#include "usbd_usr.h"
#include "usb_conf.h"

#include <stdbool.h>

#include "pdm2pcm_glo.h"

void Delay_blocking(__IO uint32_t timeout);

__ALIGN_BEGIN USB_OTG_CORE_HANDLE  USB_OTG_dev __ALIGN_END;

static __IO int32_t TimingDelay;

static PDM_Filter_Handler_t pdm_filter_handle =
{
  .bit_order = PDM_FILTER_BIT_ORDER_MSB,
  .endianness = PDM_FILTER_ENDIANNESS_BE,
  .high_pass_tap = 0,
  .in_ptr_channels = 1,
  .out_ptr_channels = 1
};

void USBAudioInit()
{
  USBD_Init(&USB_OTG_dev,
#ifdef USE_USB_OTG_HS
            USB_OTG_HS_CORE_ID,
#else
            USB_OTG_FS_CORE_ID, 
#endif
            &USR_desc, &AUDIO_cb, &USR_cb);
}

#define PDM_BUFFER_SIZE 100
#define PCM_BUFFER_SIZE 100

uint16_t PDM_data[2][PDM_BUFFER_SIZE];
uint8_t data_buffer_idx = 0xFF; // contains index of buffer with PDM data to convert

uint16_t PCM_Data[4][PCM_BUFFER_SIZE];
uint8_t pcm_data_w = 0;
uint8_t pcm_data_r = 0;
bool is_streaming = false;

int main(void)
{
  int res = 0;
  PDM_Filter_Config_t pdm_config = 
  {
    .decimation_factor = PDM_FILTER_DEC_FACTOR_128,
    .mic_gain = 51,
    .output_samples_number = PCM_BUFFER_SIZE
  };

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

  MEMS_MIC_Init();
  res = EVAL_AUDIO_Init(OUTPUT_DEVICE_AUTO, 100, 16000);

  RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;
  CRC->CR = CRC_CR_RESET;

  res += PDM_Filter_Init(&pdm_filter_handle);
  res += PDM_Filter_setConfig(&pdm_filter_handle, &pdm_config);
  if(res != 0)
  {
    while(1) {}
  }

  MEMS_MIC_DMA_Start(PDM_data, PDM_BUFFER_SIZE * 2);

  //adc_init();
  //adc_on();

  //USBAudioInit();

  while (1)
  {
    if(data_buffer_idx != 0xFF)
    {
      PDM_Filter((void *)(&(PDM_data[data_buffer_idx][0])), (void *)(&(PCM_Data[(pcm_data_w++) % 4][0])), &pdm_filter_handle);
      data_buffer_idx = 0xFF;
      if(!is_streaming)
      {
        if(pcm_data_w == 2)
        {
          is_streaming = true;
          Audio_MAL_Play((uint32_t)(&(PCM_Data[0][0])), PCM_BUFFER_SIZE * 4);
        }
      }
      STM_EVAL_LEDToggle(LED1);
    }
  }
}

void EVAL_AUDIO_TransferComplete_CallBack(uint32_t pBuffer, uint32_t Size)
{
  pcm_data_r = (pcm_data_r + 1) % 4;
  if((pcm_data_r) == pcm_data_w)
  {
    STM_EVAL_LEDOn(LED4);
  }
  STM_EVAL_LEDToggle(LED2);
}

void EVAL_AUDIO_HalfTransfer_CallBack(uint32_t pBuffer, uint32_t Size)
{
  pcm_data_r = (pcm_data_r + 1) % 4;
  if((pcm_data_r) == pcm_data_w)
  {
    STM_EVAL_LEDOn(LED4);
  }
  STM_EVAL_LEDToggle(LED2);
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

