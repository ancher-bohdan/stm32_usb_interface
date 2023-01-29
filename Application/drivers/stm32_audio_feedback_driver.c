#include "stm32f4xx_hal.h"

#include "stm32_audio_feedback_driver.h"

#include <stdbool.h>

#define ARR_SIZE(arr)   (sizeof(arr) / sizeof(arr[0]))

#define FB_RATE         8

#define BUFF_FREE_SPACE_UPPER_BOUND     56
#define BUFF_FREE_SPACE_LOWER_BOUND     25

TIM_HandleTypeDef htim2;
DMA_HandleTypeDef hdma_tim2_ch1;

static uint32_t g_mclk_to_sof_ratios[FB_RATE << 1];
static bool g_is_feedback_calculated = true;
static uint32_t g_ideal_bitrate;

static void FBCK_DMA_PreConfig(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA1_Stream5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
}

/**
  * @brief Feedback initialization Function
  * @param None
  * @retval None
  */
void FBCK_Init(uint32_t ideal_bitrate)
{
  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_SlaveConfigTypeDef sSlaveConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_IC_InitTypeDef sConfigIC = {0};

  FBCK_DMA_PreConfig();

  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 4294967295;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  HAL_TIM_Base_Init(&htim2);

  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_ETRMODE2;
  sClockSourceConfig.ClockPolarity = TIM_CLOCKPOLARITY_NONINVERTED;
  sClockSourceConfig.ClockPrescaler = TIM_CLOCKPRESCALER_DIV1;
  sClockSourceConfig.ClockFilter = 0;
  HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig);

  HAL_TIM_IC_Init(&htim2);

  sSlaveConfig.SlaveMode = TIM_SLAVEMODE_RESET;
  sSlaveConfig.InputTrigger = TIM_TS_ITR1;
  HAL_TIM_SlaveConfigSynchro(&htim2, &sSlaveConfig);

  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig);

  HAL_TIMEx_RemapConfig(&htim2, TIM_TIM2_USBFS_SOF);

  sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
  sConfigIC.ICSelection = TIM_ICSELECTION_TRC;
  sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
  sConfigIC.ICFilter = 0;
  HAL_TIM_IC_ConfigChannel(&htim2, &sConfigIC, TIM_CHANNEL_1);

  g_ideal_bitrate = ideal_bitrate;

}

void FBCK_Start(void)
{
    HAL_TIM_IC_Start_DMA(&htim2, TIM_CHANNEL_1, g_mclk_to_sof_ratios, ARR_SIZE(g_mclk_to_sof_ratios));
    g_is_feedback_calculated = true;
}

void FBCK_Stop(void)
{
    HAL_TIM_IC_Stop_DMA(&htim2, TIM_CHANNEL_1);
    g_is_feedback_calculated = true;
}

void FBCK_adjust_bitrate(uint8_t free_buf_space)
{
    if(free_buf_space > 100) return;
    if(free_buf_space >= BUFF_FREE_SPACE_UPPER_BOUND)
        g_is_feedback_calculated = false;
    else if(free_buf_space <= BUFF_FREE_SPACE_LOWER_BOUND)
        g_is_feedback_calculated = true;
}

/*=====================================================================*/
/*======================= INTERNAL FUNCTIONS ==========================*/
/*=====================================================================*/

static uint32_t __update_mclk_to_sof_ratio(uint8_t start_idx)
{
    if(g_is_feedback_calculated)
    {
        uint32_t res = 0;
        uint8_t i = 0;

        for(i = 0; i < FB_RATE; i++)
        {
            res += g_mclk_to_sof_ratios[start_idx + i];
        }

        return res;
    }
    else
    {
        return g_ideal_bitrate;
    }
}

void HAL_TIM_IC_CaptureHalfCpltCallback(TIM_HandleTypeDef *htim)
{
    if(htim == &htim2)
    {
        if(FBCK_send_feedback) FBCK_send_feedback(__update_mclk_to_sof_ratio(0));
    }
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if(htim == &htim2)
    {
        if(FBCK_send_feedback) FBCK_send_feedback(__update_mclk_to_sof_ratio(FB_RATE));
    }
}