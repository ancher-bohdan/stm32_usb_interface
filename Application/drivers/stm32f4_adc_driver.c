#include "stm32f4xx_adc.h"
#include "stm32f4xx_gpio.h"
#include "stm32f4xx_rcc.h"
#include "stm32f4xx_dma.h"

#include "stm32f4_adc_driver.h"

#define ADC_STAB_DELAY_US 3U

/**ADC1 GPIO Configuration
PA3     ------> ADC1_IN3
*/

static void ADC_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure = { 0 };

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

static void ADC_DriverInit(void)
{
    ADC_InitTypeDef ADC_InitStructure = { 0 };
    ADC_CommonInitTypeDef ADC_CommonStructure = { 0 };

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1, ENABLE);

    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T2_TRGO;
    ADC_InitStructure.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_Rising;
    ADC_InitStructure.ADC_NbrOfConversion = 1;
    ADC_InitStructure.ADC_Resolution = ADC_Resolution_12b;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_Init(ADC1, &ADC_InitStructure);

    //ADC1->CR2 &= ~(ADC_CR2_EXTSEL);
    //ADC1->CR2 &= ~(ADC_CR2_EXTEN);

    ADC_CommonStructure.ADC_DMAAccessMode = ADC_DMAAccessMode_Disabled;
    ADC_CommonStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_CommonStructure.ADC_Prescaler = ADC_Prescaler_Div4;
    ADC_CommonStructure.ADC_TwoSamplingDelay = ADC_TwoSamplingDelay_20Cycles;
    ADC_CommonInit(&ADC_CommonStructure);

    ADC_RegularChannelConfig(ADC1, ADC_Channel_3, 1, ADC_SampleTime_480Cycles);
    ADC_DMARequestAfterLastTransferCmd(ADC1, ENABLE);
}

static void ADC_DMA_Init(void)
{
    DMA_InitTypeDef DMA_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);
    
    DMA_DeInit(DMA2_Stream0);
    DMA_InitStructure.DMA_Channel = DMA_Channel_0;  
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)(&(ADC1->DR));
    DMA_InitStructure.DMA_BufferSize = 1;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_Full;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_DoubleBufferModeCmd(DMA2_Stream0, ENABLE);

    DMA_Init(DMA2_Stream0, &DMA_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel = DMA2_Stream0_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    DMA_ITConfig(DMA2_Stream0, DMA_IT_TC | DMA_IT_HT, ENABLE);
}

void adc_init(void)
{
    ADC_GPIO_Init();

    ADC_DriverInit();

    ADC_DMA_Init();
}

void adc_on(void)
{
    uint32_t counter = 0;

    ADC_Cmd(ADC1, ENABLE);

    counter = (ADC_STAB_DELAY_US * (SystemCoreClock / 1000000U));
    while(counter != 0U)
    {
      counter--;
    }
}

void adc_start(uint16_t *samples_buffer, uint32_t samples_number)
{
    ADC_DMACmd(ADC1, ENABLE);
    DMA2_Stream0->M0AR = (uint32_t)samples_buffer;
    DMA2_Stream0->NDTR = samples_number >> 1;
    DMA_DoubleBufferModeConfig(DMA2_Stream0, (uint32_t)(samples_buffer + (samples_number >> 1)), DMA_Memory_0);
    DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_HTIF0 | DMA_IT_TCIF0);
    DMA_Cmd(DMA2_Stream0, ENABLE);
    ADC_SoftwareStartConv(ADC1);
}

void adc_sampling_wrapper(uint32_t samples, uint32_t size)
{
  adc_start((uint16_t *)samples, size);
}

uint32_t adc_pause(uint32_t cmd, uint32_t addr, uint32_t size)
{
  if(cmd == 0)
  {
    DMA_Cmd(DMA2_Stream0, DISABLE);
    ADC_DMACmd(ADC1, DISABLE);
    DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_HTIF0 | DMA_IT_TCIF0);
  }
  else if(cmd == 1)
  {
    adc_sampling_wrapper(addr, size);
  }
  return 0;
}

void DMA2_Stream0_IRQHandler(void)
{
  if (DMA_GetITStatus(DMA2_Stream0, DMA_IT_HTIF0))
  {
    DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_HTIF0);
    ADC_DMAHalfTransfere_Complete();
  }
  if (DMA_GetITStatus(DMA2_Stream0, DMA_IT_TCIF0))
  {
    DMA_ClearITPendingBit(DMA2_Stream0, DMA_IT_TCIF0);
    ADC_DMATransfere_Complete();
  }
}