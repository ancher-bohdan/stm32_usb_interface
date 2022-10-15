#ifndef __AUDIO_FEEDBACK_DRIVER__
#define __AUDIO_FEEDBACK_DRIVER__

#include <stdint.h>

void FBCK_Init(void);
void FBCK_Start(void);
void FBCK_Stop(void);
uint32_t FBCK_get_current_mclk_to_sof_ratio(void);

#endif /* __AUDIO_FEEDBACK_DRIVER__ */
