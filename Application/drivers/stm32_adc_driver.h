#ifndef __STM32_ADC_DRIVER_INIT__
#define __STM32_ADC_DRIVER_INIT__

void Analog_MIC_Init(void);
void Analog_MIC_Start(uint16_t *pBuffer, uint32_t Size, uint8_t Config);
void Analog_MIC_Pause(void);
void Analog_MIC_Resume(void);
void Analog_MIC_Stop(void);

#endif /* __STM32_ADC_DRIVER_INIT__ */
