#include "stm32f4xx_hal.h"

#include "stm32_audio_codec_driver.h"

I2C_HandleTypeDef hi2c1;
I2S_HandleTypeDef hi2s3;
DMA_HandleTypeDef hdma_spi2_rx;
DMA_HandleTypeDef hdma_spi3_tx;

static uint8_t OutputDev = 0;

#define CODEC_ADDRESS                   0x94  /* b00100111 */
#define CODEC_FLAG_TIMEOUT             ((uint32_t)0x1000)
#define CODEC_LONG_TIMEOUT             ((uint32_t)(300 * CODEC_FLAG_TIMEOUT))

#define AUDIO_RESET_GPIO                GPIOD
#define AUDIO_RESET_PIN                 GPIO_PIN_4

/* Delay for the Codec to be correctly reset */
#define CODEC_RESET_DELAY               0x4FFF

#define  CODEC_STANDARD                0x04

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 102;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  HAL_I2C_Init(&hi2c1);
}

/**
  * @brief I2S3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2S3_Init(uint32_t AudioFrequency)
{
  hi2s3.Instance = SPI3;
  hi2s3.Init.Mode = I2S_MODE_MASTER_TX;
  hi2s3.Init.Standard = I2S_STANDARD_PHILIPS;
  hi2s3.Init.DataFormat = I2S_DATAFORMAT_16B;
  hi2s3.Init.MCLKOutput = I2S_MCLKOUTPUT_ENABLE;
  hi2s3.Init.AudioFreq = AudioFrequency;
  hi2s3.Init.CPOL = I2S_CPOL_LOW;
  hi2s3.Init.ClockSource = I2S_CLOCK_PLL;
  hi2s3.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
  HAL_I2S_Init(&hi2s3);
}

static void Codec_ResetInterfaceInit(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  __HAL_RCC_GPIOD_CLK_ENABLE();

  HAL_GPIO_WritePin(AUDIO_RESET_GPIO, AUDIO_RESET_PIN, GPIO_PIN_RESET);

  /*Configure GPIO pins : PD4 */
  GPIO_InitStruct.Pin = AUDIO_RESET_PIN;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(AUDIO_RESET_GPIO, &GPIO_InitStruct);
}

static uint32_t Codec_WriteRegister(uint32_t RegisterAddr, uint32_t RegisterValue)
{
    uint8_t i2c_msg[2] = { RegisterAddr, RegisterValue };

    HAL_StatusTypeDef retval = HAL_I2C_Master_Transmit(&hi2c1, CODEC_ADDRESS, i2c_msg, 2, CODEC_LONG_TIMEOUT);

    return (uint32_t)retval;
}

/**
  * @brief  Inserts a delay time (not accurate timing).
  * @param  nCount: specifies the delay time length.
  * @retval None.
  */
static void Delay(__IO uint32_t nCount)
{
  for (; nCount != 0; nCount--);
}

/**
  * @brief Resets the audio codec. It restores the default configuration of the 
  *        codec (this function shall be called before initializing the codec).
  *
  * @note  This function calls an external driver function: The IO Expander driver.
  *
  * @param None.
  * @retval 0 if correct communication, else wrong communication
  */
static void Codec_Reset(void)
{
  /* Power Down the codec */
  HAL_GPIO_WritePin(AUDIO_RESET_GPIO, AUDIO_RESET_PIN, GPIO_PIN_RESET);

  /* wait for a delay to insure registers erasing */
  Delay(CODEC_RESET_DELAY); 
  
  /* Power on the codec */
  HAL_GPIO_WritePin(AUDIO_RESET_GPIO, AUDIO_RESET_PIN, GPIO_PIN_SET);
}

/**
  * @brief Highers or Lowers the codec volume level.
  * @param Volume: a byte value from 0 to 255 (refer to codec registers 
  *        description for more details).
  * @retval o if correct communication, else wrong communication
  */
static uint32_t Codec_VolumeCtrl(uint8_t Volume)
{
  uint32_t counter = 0;

  if (Volume > 0xE6)
  {
    /* Set the Master volume */
    counter += Codec_WriteRegister(0x20, Volume - 0xE7);
    counter += Codec_WriteRegister(0x21, Volume - 0xE7);
  }
  else
  {
    /* Set the Master volume */
    counter += Codec_WriteRegister(0x20, Volume + 0x19);
    counter += Codec_WriteRegister(0x21, Volume + 0x19);
  }

  return counter;
}

/**
  * @brief Enables or disables the mute feature on the audio codec.
  * @param Cmd: AUDIO_MUTE_ON to enable the mute or AUDIO_MUTE_OFF to disable the
  *             mute mode.
  * @retval o if correct communication, else wrong communication
  */
static uint32_t Codec_Mute(uint32_t Cmd)
{
  uint32_t counter = 0;

  /* Set the Mute mode */
  if (Cmd == AUDIO_MUTE_ON)
  {
    counter += Codec_WriteRegister(0x04, 0xFF);
  }
  else                          /* AUDIO_MUTE_OFF Disable the Mute */
  {
    counter += Codec_WriteRegister(0x04, OutputDev);
  }

  return counter;
}

/**
* @brief Initializes the audio codec and all related interfaces (control 
  *      interface: I2C and audio interface: I2S)
  * @param OutputDevice: can be OUTPUT_DEVICE_SPEAKER, OUTPUT_DEVICE_HEADPHONE,
  *                       OUTPUT_DEVICE_BOTH or OUTPUT_DEVICE_AUTO .
  * @param  Volume: Initial volume level (from 0 (Mute) to 100 (Max))
  * @param  AudioFreq: Audio frequency used to paly the audio stream.
  * @retval o if correct communication, else wrong communication
  */
static uint32_t Codec_Init(uint16_t OutputDevice, uint8_t Volume, uint32_t AudioFreq)
{
  uint32_t counter = 0;

  /* Configure the Codec related IOs */
  Codec_ResetInterfaceInit();

  /* Reset the Codec Registers */
  Codec_Reset();

  /* Initialize the Control interface of the Audio Codec */
  MX_I2C1_Init();

  /* Keep Codec powered OFF */
  counter += Codec_WriteRegister(0x02, 0x01);

  switch (OutputDevice)
  {
  case OUTPUT_DEVICE_SPEAKER:
    counter += Codec_WriteRegister(0x04, 0xFA); /* SPK always ON & HP always
                                                 * OFF */
    OutputDev = 0xFA;
    break;

  case OUTPUT_DEVICE_HEADPHONE:
    counter += Codec_WriteRegister(0x04, 0xAF); /* SPK always OFF & HP always
                                                 * ON */
    OutputDev = 0xAF;
    break;

  case OUTPUT_DEVICE_BOTH:
    counter += Codec_WriteRegister(0x04, 0xAA); /* SPK always ON & HP always ON 
                                                 */
    OutputDev = 0xAA;
    break;

  case OUTPUT_DEVICE_AUTO:
    counter += Codec_WriteRegister(0x04, 0x05); /* Detect the HP or the SPK
                                                 * automatically */
    OutputDev = 0x05;
    break;

  default:
    counter += Codec_WriteRegister(0x04, 0x05); /* Detect the HP or the SPK
                                                 * automatically */
    OutputDev = 0x05;
    break;

  }

  /* Clock configuration: Auto detection */
  counter += Codec_WriteRegister(0x05, 0x81);

  /* Set the Slave Mode and the audio Standard */
  counter += Codec_WriteRegister(0x06, CODEC_STANDARD);

  /* Set the Master volume */
  Codec_VolumeCtrl(Volume);

  /* If the Speaker is enabled, set the Mono mode and volume attenuation level */
  if (OutputDevice != OUTPUT_DEVICE_HEADPHONE)
  {
    /* Set the Speaker Mono mode */
    counter += Codec_WriteRegister(0x0F, 0x06);

    /* Set the Speaker attenuation level */
    counter += Codec_WriteRegister(0x24, 0x00);
    counter += Codec_WriteRegister(0x25, 0x00);
  }

  /* Power on the Codec */
  counter += Codec_WriteRegister(0x02, 0x9E);

  /* Additional configuration for the CODEC. These configurations are done to
   * reduce the time needed for the Codec to power off. If these configurations 
   * are removed, then a long delay should be added between powering off the
   * Codec and switching off the I2S peripheral MCLK clock (which is the
   * operating clock for Codec). If this delay is not inserted, then the codec
   * will not shut down properly and it results in high noise after shut down. */

  /* Disable the analog soft ramp */
  counter += Codec_WriteRegister(0x0A, 0x00);
  /* Disable the digital soft ramp */
  counter += Codec_WriteRegister(0x0E, 0x04);
  /* Disable the limiter attack level */
  counter += Codec_WriteRegister(0x27, 0x00);
  /* Adjust Bass and Treble levels */
  counter += Codec_WriteRegister(0x1F, 0x0F);
  /* Adjust PCM volume level */
  counter += Codec_WriteRegister(0x1A, 0x0A);
  counter += Codec_WriteRegister(0x1B, 0x0A);

  /* Configure the I2S peripheral */
  MX_I2S3_Init(AudioFreq);

  /* Return communication control value */
  return counter;
}

/**
  * @brief Restore the audio codec state to default state and free all used 
  *        resources.
  * @param None.
  * @retval o if correct communication, else wrong communication
  */
static uint32_t Codec_DeInit(void)
{
  uint32_t counter = 0;

  /* Reset the Codec Registers */
  Codec_Reset();

  /* Keep Codec powered OFF */
  counter += Codec_WriteRegister(0x02, 0x01);

  /* Disable the Codec control interface */
  counter += HAL_I2C_DeInit(&hi2c1);

  /* Deinitialize the Codec audio interface (I2S) */
  counter += HAL_I2S_DeInit(&hi2s3);

  /* Return communication control value */
  return counter;
}

/**
  * @brief Start the audio Codec play feature.
  *        For this codec no Play options are required.
  * @param None.
  * @retval o if correct communication, else wrong communication
  */
uint32_t Codec_Play(void)
{
  /* 
   * No actions required on Codec level for play command */

  /* Return communication control value */
  return 0;
}

/**
  * @brief Pauses and resumes playing on the audio codec.
  * @param Cmd: AUDIO_PAUSE (or 0) to pause, AUDIO_RESUME (or any value different
  *        from 0) to resume. 
  * @retval o if correct communication, else wrong communication
  */
uint32_t Codec_PauseResume(uint32_t Cmd)
{
  uint32_t counter = 0;

  /* Pause the audio file playing */
  if (Cmd == AUDIO_PAUSE)
  {
    /* Mute the output first */
    counter += Codec_Mute(AUDIO_MUTE_ON);

    /* Put the Codec in Power save mode */
    counter += Codec_WriteRegister(0x02, 0x01);
  }
  else                          /* AUDIO_RESUME */
  {
    /* Unmute the output first */
    counter += Codec_Mute(AUDIO_MUTE_OFF);

    counter += Codec_WriteRegister(0x04, OutputDev);

    /* Exit the Power save mode */
    counter += Codec_WriteRegister(0x02, 0x9E);
  }

  return counter;
}

/**
  * @brief Stops audio Codec playing. It powers down the codec.
  * @param CodecPdwnMode: selects the  power down mode.
  * @arg   CODEC_PDWN_SW: only mutes the audio codec. When resuming from this 
  *        mode the codec keeps the previous initialization (no need to re-Initialize
  *        the codec registers).
  * @arg   CODEC_PDWN_HW: Physically power down the codec. When resuming from this
  *        mode, the codec is set to default configuration (user should re-Initialize
  *        the codec in order to play again the audio stream).
  * @retval o if correct communication, else wrong communication
  */
uint32_t Codec_Stop(uint32_t CodecPdwnMode)
{
  uint32_t counter = 0;

  /* Mute the output first */
  Codec_Mute(AUDIO_MUTE_ON);

  if (CodecPdwnMode == CODEC_PDWN_SW)
  {
    /* Power down the DAC and the speaker (PMDAC and PMSPK bits) */
    counter += Codec_WriteRegister(0x02, 0x9F);
  }
  else                          /* CODEC_PDWN_HW */
  {
    /* Power down the DAC components */
    counter += Codec_WriteRegister(0x02, 0x9F);

    /* Wait at least 100s */
    Delay(0xFFF);

    /* Reset The pin */
    HAL_GPIO_WritePin(AUDIO_RESET_GPIO, AUDIO_RESET_PIN, GPIO_PIN_RESET);
  }

  return counter;
}

/**
  * @brief Switch dynamically (while audio file is played) the output target (speaker or headphone).
  *
  * @note  This function modifies a global variable of the audio codec driver: OutputDev.
  *
  * @param None.
  * @retval 0 if correct communication, else wrong communication
  */
uint32_t Codec_SwitchOutput(uint8_t Output)
{
  uint8_t counter = 0;

  switch (Output)
  {
  case OUTPUT_DEVICE_SPEAKER:
    counter += Codec_WriteRegister(0x04, 0xFA); /* SPK always ON & HP always
                                                 * OFF */
    OutputDev = 0xFA;
    break;

  case OUTPUT_DEVICE_HEADPHONE:
    counter += Codec_WriteRegister(0x04, 0xAF); /* SPK always OFF & HP always
                                                 * ON */
    OutputDev = 0xAF;
    break;

  case OUTPUT_DEVICE_BOTH:
    counter += Codec_WriteRegister(0x04, 0xAA); /* SPK always ON & HP always ON 
                                                 */
    OutputDev = 0xAA;
    break;

  case OUTPUT_DEVICE_AUTO:
    counter += Codec_WriteRegister(0x04, 0x05); /* Detect the HP or the SPK
                                                 * automatically */
    OutputDev = 0x05;
    break;

  default:
    counter += Codec_WriteRegister(0x04, 0x05); /* Detect the HP or the SPK
                                                 * automatically */
    OutputDev = 0x05;
    break;
  }

  return counter;
}

/**
  * @brief  Enable DMA interrupt in NVIC, Enable DMA1 clocking
  * @param  None.
  * @retval None.
  */
static void Audio_MAL_Init(void)
{
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA1_Stream7_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream7_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream7_IRQn);
}

/**
  * @brief  Starts playing audio stream from the audio Media.
  * @param  Addr: Pointer to the audio stream buffer
  * @param  Size: Number of data in the audio stream buffer
  * @param  Config: DMA_DOUBLE_BUFFER_MODE_ENABLE, DMA_DOUBLE_BUFFER_MODE_DISABLE
  * @retval None.
  */
static void Audio_MAL_Play(uint16_t *Addr, uint32_t Size, uint8_t Config)
{
  HAL_I2S_Transmit_DMA(&hi2s3, Addr, Size);

  if(Config == DMA_DOUBLE_BUFFER_MODE_ENABLE)
  {
    HAL_I2S_DMAPause(&hi2s3);
    HAL_DMA_Abort(hi2s3.hdmatx);

    hi2s3.hdmatx->XferM1CpltCallback = hi2s3.hdmatx->XferCpltCallback;
    hi2s3.hdmatx->XferM1HalfCpltCallback = hi2s3.hdmatx->XferHalfCpltCallback;

    HAL_DMAEx_MultiBufferStart_IT(hi2s3.hdmatx,
                                  (uint32_t)Addr,
                                  (uint32_t)&hi2s3.Instance->DR,
                                  (uint32_t)(Addr + (Size >> 1)),
                                  Size >> 1);

    HAL_I2S_DMAResume(&hi2s3);
  }
}

/**
  * @brief  Pauses or Resumes the audio stream playing from the Media.
  * @param Cmd: AUDIO_PAUSE (or 0) to pause, AUDIO_RESUME (or any value different
  *        from 0) to resume. 
  * @retval None.
  */
static void Audio_MAL_PauseResume(uint32_t Cmd)
{
  if(Cmd == AUDIO_PAUSE)
  {
    HAL_I2S_DMAPause(&hi2s3);
  }
  else /* AUDIO RESUME */
  {
    HAL_I2S_DMAResume(&hi2s3);
  }
}

/**
  * @brief  Stops audio stream playing on the used Media.
  * @param Option: could be one of the following parameters 
  * @arg   CODEC_PDWN_SW for software power off (by writing registers) Then no 
  *        need to reconfigure the Codec after power on.
  * @arg   CODEC_PDWN_HW completely shut down the codec (physically). Then need 
  *        to reconfigure the Codec after power on.
  * @retval None.
  */
static void Audio_MAL_Stop(uint32_t Option)
{
  HAL_I2S_DMAStop(&hi2s3);
  
  if(Option == CODEC_PDWN_HW)
  {
    HAL_I2S_DeInit(&hi2s3);
    HAL_I2C_DeInit(&hi2c1);
  }
}

/**
  * @brief  Configure the audio peripherals.
  * @param  OutputDevice: OUTPUT_DEVICE_SPEAKER, OUTPUT_DEVICE_HEADPHONE,
  *                       OUTPUT_DEVICE_BOTH or OUTPUT_DEVICE_AUTO .
  * @param  Volume: Initial volume level (from 0 (Mute) to 100 (Max))
  * @param  AudioFreq: Audio frequency used to play the audio stream.
  * @retval o if correct communication, else wrong communication
  */
uint32_t EVAL_AUDIO_Init(uint16_t OutputDevice, uint8_t Volume,
                         uint32_t AudioFreq)
{
  Audio_MAL_Init();

  /* Perform low layer Codec initialization */
  return Codec_Init(OutputDevice, VOLUME_CONVERT(Volume), AudioFreq);
}

/**
  * @brief Deinitializes all the resources used by the codec (those initialized 
  *        by EVAL_AUDIO_Init() function) EXCEPT the I2C resources since they are 
  *        used by the IOExpander as well (and eventually other modules). 
  * @param None.
  * @retval 0 if correct communication, else wrong communication
  */
uint32_t EVAL_AUDIO_DeInit(void)
{
  /* DeInitialize Codec */
  Codec_DeInit();

  return 0;
}

/**
  * @brief Starts playing audio stream from a data buffer for a determined size. 
  * @param pBuffer: Pointer to the buffer 
  * @param Size: Number of audio data BYTES.
  * @param Config: DMA_DOUBLE_BUFFER_MODE_ENABLE, DMA_DOUBLE_BUFFER_MODE_DISABLE
  * @retval 0 if correct communication, else wrong communication
  */
uint32_t EVAL_AUDIO_Play(uint16_t * pBuffer, uint32_t Size, uint8_t Config)
{
  /* Call the audio Codec Play function */
  Codec_Play();

  /* Update the Media layer and enable it for play */
  Audio_MAL_Play(pBuffer, Size, Config);

  return 0;
}

/**
  * @brief This function Pauses or Resumes the audio file stream. In case
  *        of using DMA, the DMA Pause feature is used. In all cases the I2S 
  *        peripheral is disabled. 
  * 
  * @WARNING When calling EVAL_AUDIO_PauseResume() function for pause, only
  *         this function should be called for resume (use of EVAL_AUDIO_Play() 
  *         function for resume could lead to unexpected behaviour).
  * 
  * @param Cmd: AUDIO_PAUSE (or 0) to pause, AUDIO_RESUME (or any value different
  *        from 0) to resume. 
  * @retval o if correct communication, else wrong communication
  */
uint32_t EVAL_AUDIO_PauseResume(uint32_t Cmd)
{
  if (Cmd != AUDIO_PAUSE)
  {
    /* Call the Media layer pause/resume function */
    Audio_MAL_PauseResume(Cmd);

    /* Call the Audio Codec Pause/Resume function */
    if (Codec_PauseResume(Cmd) != 0)
    {
      return 1;
    }
    else
    {
      return 0;
    }
  }
  else
  {
    /* Call the Audio Codec Pause/Resume function */
    if (Codec_PauseResume(Cmd) != 0)
    {
      return 1;
    }
    else
    {
      /* Call the Media layer pause/resume function */
      Audio_MAL_PauseResume(Cmd);

      /* Return 0 if all operations are OK */
      return 0;
    }
  }
}

/**
  * @brief Stops audio playing and Power down the Audio Codec. 
  * @param Option: could be one of the following parameters 
  * @arg   CODEC_PDWN_SW for software power off (by writing registers) Then no 
  *        need to reconfigure the Codec after power on.
  * @arg   CODEC_PDWN_HW completely shut down the codec (physically). Then need 
  *        to reconfigure the Codec after power on.  
  * @retval 0 if correct communication, else wrong communication
  */
uint32_t EVAL_AUDIO_Stop(uint32_t Option)
{
  /* Call Audio Codec Stop function */
  if (Codec_Stop(Option) != 0)
  {
    return 1;
  }
  else
  {
    /* Call Media layer Stop function */
    Audio_MAL_Stop(Option);

    /* Return 0 when all operations are correctly done */
    return 0;
  }
}

/**
  * @brief Controls the current audio volume level. 
  * @param Volume: Volume level to be set in percentage from 0% to 100% (0 for 
  *        Mute and 100 for Max volume level).
  * @retval o if correct communication, else wrong communication
  */
uint32_t EVAL_AUDIO_VolumeCtl(uint8_t Volume)
{
  /* Call the codec volume control function with converted volume value */
  return (Codec_VolumeCtrl(VOLUME_CONVERT(Volume)));
}

/**
  * @brief Enable or disable the MUTE mode by software 
  * @param Command: could be AUDIO_MUTE_ON to mute sound or AUDIO_MUTE_OFF to 
  *        Unmute the codec and restore previous volume level.
  * @retval o if correct communication, else wrong communication
  */
uint32_t EVAL_AUDIO_Mute(uint32_t Cmd)
{
  /* Call the Codec Mute function */
  return (Codec_Mute(Cmd));
}

__weak void EVAL_AUDIO_HalfCpltCallback(void)
{

}

__weak void EVAL_AUDIO_CpltCallback(void)
{

}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
  if(hi2s == &hi2s3)
  {
    EVAL_AUDIO_HalfCpltCallback();
  }
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
  if(hi2s == &hi2s3)
  {
    EVAL_AUDIO_CpltCallback();
  }
}
