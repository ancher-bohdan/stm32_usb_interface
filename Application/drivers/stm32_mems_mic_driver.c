#include "stm32f4xx_hal.h"

#include "stm32_mems_mic_driver.h"

I2S_HandleTypeDef hi2s2;
DMA_HandleTypeDef hdma_spi2_rx;

static void MEMS_MIC_DMA_PreConfig(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA1_Stream3_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream3_IRQn);
}

/**
  * @brief MEMS MIC I2S2 Initialization Function
  * @param None
  * @retval None
  */
void MEMS_MIC_Init(void)
{
  MEMS_MIC_DMA_PreConfig();

  hi2s2.Instance = SPI2;
  hi2s2.Init.Mode = I2S_MODE_MASTER_RX;
  hi2s2.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s2.Init.DataFormat = I2S_DATAFORMAT_24B;
  hi2s2.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
  hi2s2.Init.AudioFreq = I2S_AUDIOFREQ_48K;
  hi2s2.Init.CPOL = I2S_CPOL_LOW;
  hi2s2.Init.ClockSource = I2S_CLOCK_PLL;
  hi2s2.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
  HAL_I2S_Init(&hi2s2);
}

/**
  * @brief Starts audio stream from a MEMS MIC for a determined size. 
  * @param pBuffer: Pointer to the buffer 
  * @param Size: Number of audio data BYTES.
  * @param Config: DMA_DOUBLE_BUFFER_MODE_ENABLE, DMA_DOUBLE_BUFFER_MODE_DISABLE
  * @retval 0 if correct communication, else wrong communication
  */
void MEMS_MIC_Start(uint16_t *pBuffer, uint32_t Size, uint8_t Config)
{
    HAL_I2S_Receive_DMA(&hi2s2, pBuffer, Size);
    
    if(Config)
    {
        /* DMA_DOUBLE_BUFFER_MODE_ENABLE */
        HAL_I2S_DMAPause(&hi2s2);
        HAL_DMA_Abort(hi2s2.hdmarx);

        hi2s2.hdmarx->XferM1CpltCallback = hi2s2.hdmarx->XferCpltCallback;
        hi2s2.hdmarx->XferM1HalfCpltCallback = hi2s2.hdmarx->XferHalfCpltCallback;

        HAL_DMAEx_MultiBufferStart_IT(hi2s2.hdmarx,
                                    (uint32_t)&hi2s2.Instance->DR,
                                    (uint32_t)pBuffer,
                                    (uint32_t)(pBuffer + (Size >> 1)),
                                    Size >> 1);
        
        HAL_I2S_DMAResume(&hi2s2);
    }
}

/**
  * @brief Stop audio stream from MEMS MIC 
  * @param none
  */
void MEMS_MIC_Stop(void)
{
    HAL_I2S_DMAStop(&hi2s2);
}

__weak void MEMS_MIC_HalfCpltCallback(void)
{

}

__weak void MEMS_MIC_CpltCallback(void)
{

}

void HAL_I2S_RxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
  if(hi2s == &hi2s2)
  {
    MEMS_MIC_HalfCpltCallback();
  }
}

void HAL_I2S_RxCpltCallback(I2S_HandleTypeDef *hi2s)
{
  if(hi2s == &hi2s2)
  {
    MEMS_MIC_CpltCallback();
  }
}