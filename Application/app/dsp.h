#ifndef __DSP_INIT__
#define __DSP_INIT__

#include <stdint.h>

#define DSP_EOK     0
#define DSP_EBUSY  -1

void dsp_init(void);
int dsp_calculation_request(int16_t *p_samples, void (*dsp_cbk)(void * arg), void *args);
void dsp_process();
void dsp_mute(uint8_t cmd);

#endif /*__DSP_INIT__*/
