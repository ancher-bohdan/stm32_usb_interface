#include "stm32f4xx.h"

/**MEMS MIC GPIO Configuration
PC3     ------> I2S2_SD
PB10    ------> I2S2_CK
PB12    ------> I2S2_WS // Not used for MEMS MIC
*/

static void MEMS_MIC_I2S_GPIO_init()
{
    GPIO_InitTypeDef GPIO_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOC, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);

    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_PinAFConfig(GPIOC, GPIO_PinSource3, GPIO_AF_SPI2);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_SPI2);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource12, GPIO_AF_SPI2);
}

static void MEMS_MIC_I2S_Init()
{
    I2S_InitTypeDef I2S_InitStructure;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);

    I2S_InitStructure.I2S_AudioFreq = 64000;//62500;//64000;
    I2S_InitStructure.I2S_CPOL = I2S_CPOL_High;
    I2S_InitStructure.I2S_DataFormat = I2S_DataFormat_16b;
    I2S_InitStructure.I2S_MCLKOutput = I2S_MCLKOutput_Disable;
    I2S_InitStructure.I2S_Mode = I2S_Mode_MasterRx;
    I2S_InitStructure.I2S_Standard = I2S_Standard_MSB;

    I2S_Init(SPI2, &I2S_InitStructure);
    SPI_I2S_DMACmd(SPI2, SPI_I2S_DMAReq_Rx, ENABLE);
}

static void MEMS_MIC_DMA_Init()
{
    DMA_InitTypeDef DMA_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);
    DMA_Cmd(DMA1_Stream3, DISABLE);
    DMA_DeInit(DMA1_Stream3);
    
    DMA_InitStructure.DMA_Channel = DMA_Channel_0;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)(&(SPI2->DR));
    DMA_InitStructure.DMA_Memory0BaseAddr = 0;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralToMemory;
    DMA_InitStructure.DMA_BufferSize = 0;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_FIFOMode = DMA_FIFOMode_Disable;
    DMA_InitStructure.DMA_FIFOThreshold = DMA_FIFOThreshold_1QuarterFull;
    DMA_InitStructure.DMA_MemoryBurst = DMA_MemoryBurst_Single;
    DMA_InitStructure.DMA_PeripheralBurst = DMA_PeripheralBurst_Single;
    DMA_Init(DMA1_Stream3, &DMA_InitStructure);

    DMA_ITConfig(DMA1_Stream3, DMA_IT_TC | DMA_IT_HT, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Stream3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

void MEMS_MIC_Init()
{
    MEMS_MIC_I2S_GPIO_init();
    MEMS_MIC_I2S_Init();
    MEMS_MIC_DMA_Init();
}

void MEMS_MIC_DMA_Start(void *addr, uint32_t size)
{
    DMA1_Stream3->M0AR = (uint32_t)addr;
    DMA1_Stream3->NDTR = size;
    DMA_ClearITPendingBit(DMA1_Stream3, DMA_IT_HTIF3 | DMA_IT_TCIF3);
    DMA_Cmd(DMA1_Stream3, ENABLE);
    I2S_Cmd(SPI2, ENABLE);
}

extern uint8_t data_buffer_idx;
void DMA1_Stream3_IRQHandler(void)
{
    if(DMA_GetITStatus(DMA1_Stream3, DMA_IT_HTIF3))
    {
        DMA_ClearITPendingBit(DMA1_Stream3, DMA_IT_HTIF3);
        if(data_buffer_idx != 0xFF)
        {
            while(1) {}
        }
        data_buffer_idx = 0;
    }
    if (DMA_GetITStatus(DMA1_Stream3, DMA_IT_TCIF3))
    {
        DMA_ClearITPendingBit(DMA1_Stream3, DMA_IT_TCIF3);
        if(data_buffer_idx != 0xFF)
        {
            while(1) {}
        }
        data_buffer_idx = 1;
    }
}