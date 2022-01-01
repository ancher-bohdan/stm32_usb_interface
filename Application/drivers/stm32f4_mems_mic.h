#ifndef __MEMS_MIC_DRV__
#define __MEMS_MIC_DRV__

#include <stdint.h>

void MEMS_MIC_Init();
void MEMS_MIC_DMA_Start(void *addr, uint32_t size);

#endif