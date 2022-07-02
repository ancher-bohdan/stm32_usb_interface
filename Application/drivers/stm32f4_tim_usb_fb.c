#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_dma.h"
#include "stm32f4xx_tim.h"

/**TIM2_ETR GPIO Configuration
PA15     ------> TIM2_ETR
*/

#define FB_RATE     2

static uint32_t g_mclk_to_sof_ratios[FB_RATE << 1];
void (*g_trigger_fb_transaction)(uint32_t mclk_to_sof_ratio);


static void TIM_FB_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;

    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource15, GPIO_AF_TIM2);
}

static void TIM_FB_General_init()
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    TIM_ICInitTypeDef TIM_ICInitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    
    TIM_Cmd(TIM2, DISABLE);
    TIM_TimeBaseStructure.TIM_Period            = 0xffffffff;
    TIM_TimeBaseStructure.TIM_Prescaler         = 0;
    TIM_TimeBaseStructure.TIM_ClockDivision     = 0;
    TIM_TimeBaseStructure.TIM_CounterMode       = TIM_CounterMode_Up;
    TIM_TimeBaseStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    TIM_ETRClockMode2Config(TIM2, TIM_ExtTRGPSC_OFF, TIM_ExtTRGPolarity_NonInverted, 0x00);

    TIM_ICInitStructure.TIM_Channel = TIM_Channel_1;
    TIM_ICInitStructure.TIM_ICPolarity = TIM_ICPolarity_Rising;
    TIM_ICInitStructure.TIM_ICSelection = TIM_ICSelection_TRC;
    TIM_ICInitStructure.TIM_ICPrescaler = TIM_ICPSC_DIV1;
    TIM_ICInitStructure.TIM_ICFilter = 0;
    TIM_ICInit(TIM2, &TIM_ICInitStructure);

    TIM_RemapConfig(TIM2, TIM2_USBFS_SOF);
    TIM_SelectInputTrigger(TIM2, TIM_TS_ITR1);
    TIM_SelectSlaveMode(TIM2, TIM_SlaveMode_Reset);

    TIM_DMACmd(TIM2, TIM_DMA_CC1, ENABLE);
    TIM_Cmd(TIM2, ENABLE);
}

static void TIM_FB_DMA_Init()
{
    DMA_InitTypeDef DMA_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);
    
    DMA_DeInit(DMA1_Stream5);
    DMA_InitStructure.DMA_Channel = DMA_Channel_3;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)(&(TIM2->CCR1));
    DMA_InitStructure.DMA_Memory0BaseAddr = (uint32_t)g_mclk_to_sof_ratios;
    DMA_InitStructure.DMA_BufferSize = sizeof(g_mclk_to_sof_ratios) / sizeof(g_mclk_to_sof_ratios[0]);
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_PeripheralDataSize_Word;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;

    DMA_Init(DMA1_Stream5, &DMA_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Stream5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    DMA_ITConfig(DMA1_Stream5, DMA_IT_TC | DMA_IT_HT, ENABLE);
}

static void calculate_mclk_to_sof_ratio(uint8_t buff_index)
{
    uint8_t i = 0;
    uint32_t res = 0;

    for(i = 0; i < FB_RATE; i++)
    {
        res += g_mclk_to_sof_ratios[buff_index + i];
    }

    if(g_trigger_fb_transaction != (void *)0) g_trigger_fb_transaction(res << 8);
}

void TIM_FB_Init()
{
    TIM_FB_GPIO_Init();

    TIM_FB_General_init();

    TIM_FB_DMA_Init();
}

void TIM_FB_Start()
{
    DMA_Cmd(DMA1_Stream5, ENABLE);
}

void TIM_FB_Stop()
{
    DMA_Cmd(DMA1_Stream5, DISABLE);
}

void TIM_FB_Set_trigger_fb_transaction_callback(void (*trigger_fb_transaction)(uint32_t mclk_to_sof_ratio))
{
    g_trigger_fb_transaction = trigger_fb_transaction;
}

void DMA1_Stream5_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA1_Stream5, DMA_IT_HTIF5))
    {
        DMA_ClearITPendingBit(DMA1_Stream5, DMA_IT_HTIF5);
        calculate_mclk_to_sof_ratio(0);
    }
    if (DMA_GetITStatus(DMA1_Stream5, DMA_IT_TCIF5))
    {
        DMA_ClearITPendingBit(DMA1_Stream5, DMA_IT_TCIF5);
        calculate_mclk_to_sof_ratio(FB_RATE);
    }
}
