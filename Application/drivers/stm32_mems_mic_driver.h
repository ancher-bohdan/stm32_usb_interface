#ifndef __MEMS_MIC_DRIVER__
#define __MEMS_MIC_DRIVER__

void MEMS_MIC_Init(void);
void MEMS_MIC_Start(uint16_t *pBuffer, uint32_t Size, uint8_t Config);
void MEMS_MIC_PauseResume(uint32_t Cmd);
void MEMS_MIC_Stop(void);

#endif /* __MEMS_MIC_DRIVER__ */