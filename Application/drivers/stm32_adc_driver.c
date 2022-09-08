#include "stm32f4xx_hal.h"

#include "stm32_adc_driver.h"

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;
TIM_HandleTypeDef htim3;

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T3_TRGO;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = ENABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  HAL_ADC_Init(&hadc1);

  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  HAL_ADC_ConfigChannel(&hadc1, &sConfig);

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 1749;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  HAL_TIM_Base_Init(&htim3);

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig);

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig);
}

/**
  * @brief  Enable DMA interrupt in NVIC, Enable DMA1 clocking
  * @param  None.
  * @retval None.
  */
static void MX_ADC_PreConfig(void)
{
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA2_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
}

/**
  * @brief Ananlog MIC Initialization Function
  * @param None
  * @retval None
  */
void Analog_MIC_Init(void)
{
  MX_ADC_PreConfig();

  MX_ADC1_Init();

  MX_TIM3_Init();
}

/**
  * @brief Starts audio stream from a Ananlog MIC for a determined size. 
  * @param pBuffer: Pointer to the buffer 
  * @param Size: Number of audio data BYTES.
  * @param Config: DMA_DOUBLE_BUFFER_MODE_ENABLE, DMA_DOUBLE_BUFFER_MODE_DISABLE
  * @retval None
  */
void Analog_MIC_Start(uint16_t *pBuffer, uint32_t Size, uint8_t Config)
{
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)pBuffer, Size);

  if(Config)
  {
    hadc1.Instance->CR2 &= (~ADC_CR2_DMA);

    HAL_DMA_Abort(hadc1.DMA_Handle);

    hadc1.DMA_Handle->XferM1CpltCallback = hadc1.DMA_Handle->XferCpltCallback;
    hadc1.DMA_Handle->XferM1HalfCpltCallback = hadc1.DMA_Handle->XferHalfCpltCallback;

    HAL_DMAEx_MultiBufferStart_IT(hadc1.DMA_Handle,
                                  (uint32_t)&(hadc1.Instance->DR),
                                  (uint32_t)pBuffer,
                                  (uint32_t)(pBuffer + (Size >> 1)),
                                  Size >> 1);

    hadc1.Instance->CR2 |= ADC_CR2_DMA;
  }

  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
}

/**
  * @brief  Pauses the audio stream playing from the Ananlog MIC.
  * @param  None 
  * @retval None
  */
void Analog_MIC_Pause(void)
{
  HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
}

/**
  * @brief  Resumes the audio stream playing from the Ananlog MIC.
  * @param  None 
  * @retval None
  */
void Analog_MIC_Resume(void)
{
  HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
}

/**
  * @brief  Stop the audio stream playing from the Ananlog MIC.
  * @param  None 
  * @retval None
  */
void Analog_MIC_Stop(void)
{
  HAL_TIM_PWM_Stop(&htim3, TIM_CHANNEL_1);
  HAL_ADC_Stop_DMA(&hadc1);
  hadc1.DMA_Handle->Instance->CR &= ~((uint32_t)DMA_SxCR_DBM);
  
}

__weak void Analog_MIC_ConvCpltCallback(void)
{

}

__weak void Analog_MIC_ConvHalfCpltCallback(void)
{

}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc)
{
  if(hadc == &hadc1)
  {
    Analog_MIC_ConvCpltCallback();
  }
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc)
{
  if(hadc == &hadc1)
  {
    Analog_MIC_ConvHalfCpltCallback();
  }
}