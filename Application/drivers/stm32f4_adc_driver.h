#ifndef __ADC_DRIVER__
#define __ADC_DRIVER__

void adc_init(void);

void adc_on(void);
void adc_start(uint16_t *samples_buffer, uint32_t samples_number);

void adc_sampling_wrapper(int16_t *samples, uint16_t size, void (*finish_cbk)(void *arg), void *args);

#endif
