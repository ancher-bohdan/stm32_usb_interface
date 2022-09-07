#ifndef __STM32_AUDIO_CODEC_DRIVER_INIT__
#define __STM32_AUDIO_CODEC_DRIVER_INIT__

#define OUTPUT_DEVICE_SPEAKER         1
#define OUTPUT_DEVICE_HEADPHONE       2
#define OUTPUT_DEVICE_BOTH            3
#define OUTPUT_DEVICE_AUTO            4

#define DMA_DOUBLE_BUFFER_MODE_ENABLE   1
#define DMA_DOUBLE_BUFFER_MODE_DISABLE  0

/* Volume Levels values */
#define DEFAULT_VOLMIN                0x00
#define DEFAULT_VOLMAX                0xFF
#define DEFAULT_VOLSTEP               0x04

#define AUDIO_PAUSE                   0
#define AUDIO_RESUME                  1

/* Codec POWER DOWN modes */
#define CODEC_PDWN_HW                 1
#define CODEC_PDWN_SW                 2

/* MUTE commands */
#define AUDIO_MUTE_ON                 1
#define AUDIO_MUTE_OFF                0
/*----------------------------------------------------------------------------*/

/* Exported macro ------------------------------------------------------------*/
#define VOLUME_CONVERT(x)    ((Volume > 100)? 100:((uint8_t)((Volume * 255) / 100)))

uint32_t EVAL_AUDIO_Init(uint16_t OutputDevice, uint8_t Volume,
                         uint32_t AudioFreq);
uint32_t EVAL_AUDIO_DeInit(void);
uint32_t EVAL_AUDIO_Play(uint16_t * pBuffer, uint32_t Size, uint8_t Config);
uint32_t EVAL_AUDIO_PauseResume(uint32_t Cmd);
uint32_t EVAL_AUDIO_Stop(uint32_t Option);
uint32_t EVAL_AUDIO_VolumeCtl(uint8_t Volume);
uint32_t EVAL_AUDIO_Mute(uint32_t Cmd);

#endif /* __STM32_AUDIO_CODEC_DRIVER_INIT__ */