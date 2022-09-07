#include "bsp/board.h"
#include "stm32_audio_codec_driver.h"
#include "stm32_mems_mic_driver.h"

#define AUDIO_SAMPLE_TEST_SIZE  200

uint16_t data[4][AUDIO_SAMPLE_TEST_SIZE];
bool is_playing = false;
uint8_t r_idx = 0;

int main(void)
{
  board_init();

  EVAL_AUDIO_Init(OUTPUT_DEVICE_AUTO, 100, 48000);
  MEMS_MIC_Init();

  MEMS_MIC_Start(&(data[0][0]), sizeof(data)/sizeof(data[0][0]), DMA_DOUBLE_BUFFER_MODE_ENABLE);

  while(true)
  {
    if(!is_playing)
    {
      if(r_idx == 1)
      {
        is_playing = true;
        EVAL_AUDIO_Play(&(data[0][0]), sizeof(data)/sizeof(data[0][0]), DMA_DOUBLE_BUFFER_MODE_ENABLE);
      }
    }
  }

  return 0;
}

void MEMS_MIC_HalfCpltCallback(void)
{
  r_idx = 1;
}

void MEMS_MIC_CpltCallback(void)
{
  r_idx = 0;
}
