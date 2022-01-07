#ifndef __ADC_DRIVER__
#define __ADC_DRIVER__

void adc_init(void);

void adc_on(void);
void adc_start(uint16_t *samples_buffer, uint32_t samples_number);

void adc_sampling_wrapper(uint32_t samples, uint32_t size);
uint32_t adc_pause(uint32_t cmd, uint32_t addr, uint32_t size);

void ADC_DMAHalfTransfere_Complete();
void ADC_DMATransfere_Complete();

#endif
