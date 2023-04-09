#include "bsp/board.h"
#include "tusb.h"
#include "tusb_config.h"

#include "usb_descriptors.h"

#include "stm32_audio_codec_driver.h"
#include "stm32_mems_mic_driver.h"
#include "stm32_adc_driver.h"
#include "stm32_audio_feedback_driver.h"

#include "audio_buffer.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static void cs43l22_play(uint32_t addr, uint32_t size);
static uint32_t cs43l22_pause_resume(uint32_t cmd, uint32_t addr, uint32_t size);
static void max9814_play(uint32_t addr, uint32_t size);
static uint32_t max9814_pause_resume(uint32_t cmd, uint32_t addr, uint32_t size);
static void msm261s_play(uint32_t addr, uint32_t size);
static uint32_t msm261s_pause_resume(uint32_t cmd, uint32_t addr, uint32_t size);

enum
{
  VOLUME_CTRL_0_DB = 0,
  VOLUME_CTRL_10_DB = 2560,
  VOLUME_CTRL_20_DB = 5120,
  VOLUME_CTRL_30_DB = 7680,
  VOLUME_CTRL_40_DB = 10240,
  VOLUME_CTRL_50_DB = 12800,
  VOLUME_CTRL_60_DB = 15360,
  VOLUME_CTRL_70_DB = 17920,
  VOLUME_CTRL_80_DB = 20480,
  VOLUME_CTRL_90_DB = 23040,
  VOLUME_CTRL_100_DB = 25600,
  VOLUME_CTRL_SILENCE = 0x8000,
};

int8_t mute[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX + 1];
int16_t volume[CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_RX + 1];
int8_t selector = 1;

uint8_t active_alt_settings[ITF_NUM_TOTAL];

struct {
  uint8_t __terminal_id;
  uint8_t valid_alt_settings_bitmap;
  uint32_t usb_packet_size;
  um_play_fnc play_cb;
  um_pause_resume_fnc pause_resume_cb;
} in_terminal_lut[2] =
{
  {
    .__terminal_id = UAC2_ENTITY_MIC_INPUT_TERMINAL1,
    .usb_packet_size = 192,
    .play_cb = max9814_play,
    .pause_resume_cb = max9814_pause_resume,
    .valid_alt_settings_bitmap = TU_BIT(ALT_SETTING_MIC_TOTAL) - 1,
  },
  {
    .__terminal_id = UAC2_ENTITY_MIC_INPUT_TERMINAL2,
    .usb_packet_size = 384,
    .play_cb = msm261s_play,
    .pause_resume_cb = msm261s_pause_resume,
    .valid_alt_settings_bitmap = TU_BIT(ALT_SETTING_MIC_TOTAL) - 1,
  }
};

const uint32_t sample_rates[] = { 48000 };

uint32_t current_sample_rate  = 48000;

struct um_buffer_handle *um_out_buffer, *um_in_buffer;

#define N_SAMPLE_RATES  TU_ARRAY_SIZE(sample_rates)

#define FBCK_TASK_QUEUE_SIZE    2
OSAL_QUEUE_DEF(FBCK_int_set, __fbck_qdef, FBCK_TASK_QUEUE_SIZE, uint32_t);
static osal_queue_t __fbck_q;

void feedback_sender_task(void);

static void cs43l22_play(uint32_t addr, uint32_t size)
{
  EVAL_AUDIO_Play((uint16_t *)addr, size, DMA_DOUBLE_BUFFER_MODE_ENABLE);
}

static uint32_t cs43l22_pause_resume(uint32_t cmd, uint32_t addr, uint32_t size)
{
  (void) addr;
  (void) size;

  return EVAL_AUDIO_PauseResume(cmd);
}

static void max9814_play(uint32_t addr, uint32_t size)
{
  Analog_MIC_Start((uint16_t *)addr, size, DMA_DOUBLE_BUFFER_MODE_ENABLE);
}

static uint32_t max9814_pause_resume(uint32_t cmd, uint32_t addr, uint32_t size)
{
  (void) addr;
  (void) size;

  if(cmd == 0)
  {
    Analog_MIC_Pause();
  }
  else if(cmd == 1)
  {
    Analog_MIC_Resume();
  }
  return 0;
}

static void msm261s_play(uint32_t addr, uint32_t size)
{
  MEMS_MIC_Start((uint16_t *)addr, size, DMA_DOUBLE_BUFFER_MODE_ENABLE);
}

static uint32_t msm261s_pause_resume(uint32_t cmd, uint32_t addr, uint32_t size)
{
  (void) addr;
  (void) size;

  MEMS_MIC_PauseResume(cmd);

  return 0;
}

void audio_buffer_out_free_space_handle(void *free_space_persentage)
{
  uint32_t free_space = *(uint32_t *)free_space_persentage;
  FBCK_adjust_bitrate(free_space);
}

void audio_buffer_in_free_space_handle(void *free_space_persentage)
{
  uint32_t free_space = *(uint32_t *)free_space_persentage;
  Analog_MIC_adjust_bitrate(free_space);
}

int main(void)
{
  int result = 0;
  board_init();

  EVAL_AUDIO_Init(OUTPUT_DEVICE_AUTO, 100, 48000);
  MEMS_MIC_Init();
  Analog_MIC_Init();
  FBCK_Init(0x18000);

  __fbck_q = osal_queue_create(&__fbck_qdef);
  if(__fbck_q == NULL) while(1) {}

  um_out_buffer = (struct um_buffer_handle *) malloc(sizeof(struct um_buffer_handle));
  um_in_buffer = (struct um_buffer_handle *) malloc(sizeof(struct um_buffer_handle));

  result = um_handle_init(um_out_buffer, 192, 4, 4, UM_BUFFER_CONFIG_CA_FEEDBACK,
    cs43l22_play, cs43l22_pause_resume);
  result += um_handle_init(um_in_buffer, 384, 4, 4, UM_BUFFER_CONFIG_CA_NONE,
      msm261s_play, msm261s_pause_resume);

  result += um_handle_set_driver(um_in_buffer, in_terminal_lut[selector-1].usb_packet_size,
      in_terminal_lut[selector-1].play_cb, in_terminal_lut[selector-1].pause_resume_cb);

  if(result != UM_EOK)
  {
    while(1) {}
  }

  um_handle_register_listener(um_out_buffer, UM_LISTENER_TYPE_CA, audio_buffer_out_free_space_handle);
  um_handle_register_listener(um_in_buffer, UM_LISTENER_TYPE_CA, audio_buffer_in_free_space_handle);

  tusb_init();

  while(true)
  {
    feedback_sender_task();
    tud_task();
  }

  return 0;
}

// Helper for clock get requests
static bool tud_audio_clock_get_request(uint8_t rhport, audio_control_request_t const *request)
{
  TU_ASSERT(request->bEntityID == UAC2_ENTITY_CLOCK);

  if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ)
  {
    if (request->bRequest == AUDIO_CS_REQ_CUR)
    {
      TU_LOG1("Clock get current freq %lu\r\n", current_sample_rate);

      audio_control_cur_4_t curf = { (int32_t) tu_htole32(current_sample_rate) };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &curf, sizeof(curf));
    }
    else if (request->bRequest == AUDIO_CS_REQ_RANGE)
    {
      audio_control_range_4_n_t(N_SAMPLE_RATES) rangef =
      {
        .wNumSubRanges = tu_htole16(N_SAMPLE_RATES)
      };
      TU_LOG1("Clock get %d freq ranges\r\n", N_SAMPLE_RATES);
      for(uint8_t i = 0; i < N_SAMPLE_RATES; i++)
      {
        rangef.subrange[i].bMin = (int32_t) sample_rates[i];
        rangef.subrange[i].bMax = (int32_t) sample_rates[i];
        rangef.subrange[i].bRes = 0;
        TU_LOG1("Range %d (%d, %d, %d)\r\n", i, (int)rangef.subrange[i].bMin, (int)rangef.subrange[i].bMax, (int)rangef.subrange[i].bRes);
      }
      
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &rangef, sizeof(rangef));
    }
  }
  else if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID &&
           request->bRequest == AUDIO_CS_REQ_CUR)
  {
    audio_control_cur_1_t cur_valid = { .bCur = 1 };
    TU_LOG1("Clock get is valid %u\r\n", cur_valid.bCur);
    return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_valid, sizeof(cur_valid));
  }
  TU_LOG1("Clock get request not supported, entity = %u, selector = %u, request = %u\r\n",
          request->bEntityID, request->bControlSelector, request->bRequest);
  return false;
}

// Helper for clock set requests
static bool tud_audio_clock_set_request(uint8_t rhport, audio_control_request_t const *request, uint8_t const *buf)
{
  (void)rhport;

  TU_ASSERT(request->bEntityID == UAC2_ENTITY_CLOCK);
  TU_VERIFY(request->bRequest == AUDIO_CS_REQ_CUR);

  if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ)
  {
    TU_VERIFY(request->wLength == sizeof(audio_control_cur_4_t));

    current_sample_rate = (uint32_t) ((audio_control_cur_4_t const *)buf)->bCur;

    TU_LOG1("Clock set current freq: %ld\r\n", current_sample_rate);

    return true;
  }
  else
  {
    TU_LOG1("Clock set request not supported, entity = %u, selector = %u, request = %u\r\n",
            request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
  }
}

// Helper for feature unit get requests
static bool tud_audio_feature_unit_get_request(uint8_t rhport, audio_control_request_t const *request)
{
  TU_ASSERT(request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT);

  if (request->bControlSelector == AUDIO_FU_CTRL_MUTE && request->bRequest == AUDIO_CS_REQ_CUR)
  {
    audio_control_cur_1_t mute1 = { .bCur = mute[request->bChannelNumber] };
    TU_LOG1("Get channel %u mute %d\r\n", request->bChannelNumber, mute1.bCur);
    return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &mute1, sizeof(mute1));
  }
  else if (UAC2_ENTITY_SPK_FEATURE_UNIT && request->bControlSelector == AUDIO_FU_CTRL_VOLUME)
  {
    if (request->bRequest == AUDIO_CS_REQ_RANGE)
    {
      audio_control_range_2_n_t(1) range_vol = {
        .wNumSubRanges = tu_htole16(1),
        .subrange[0] = { .bMin = tu_htole16(-VOLUME_CTRL_50_DB), tu_htole16(VOLUME_CTRL_0_DB), tu_htole16(256) }
      };
      TU_LOG1("Get channel %u volume range (%d, %d, %u) dB\r\n", request->bChannelNumber,
              range_vol.subrange[0].bMin / 256, range_vol.subrange[0].bMax / 256, range_vol.subrange[0].bRes / 256);
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &range_vol, sizeof(range_vol));
    }
    else if (request->bRequest == AUDIO_CS_REQ_CUR)
    {
      audio_control_cur_2_t cur_vol = { .bCur = tu_htole16(volume[request->bChannelNumber]) };
      TU_LOG1("Get channel %u volume %d dB\r\n", request->bChannelNumber, cur_vol.bCur / 256);
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_vol, sizeof(cur_vol));
    }
  }
  TU_LOG1("Feature unit get request not supported, entity = %u, selector = %u, request = %u\r\n",
          request->bEntityID, request->bControlSelector, request->bRequest);

  return false;
}

// Helper for feature unit set requests
static bool tud_audio_feature_unit_set_request(uint8_t rhport, audio_control_request_t const *request, uint8_t const *buf)
{
  (void)rhport;

  TU_ASSERT(request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT);
  TU_VERIFY(request->bRequest == AUDIO_CS_REQ_CUR);

  if (request->bControlSelector == AUDIO_FU_CTRL_MUTE)
  {
    TU_VERIFY(request->wLength == sizeof(audio_control_cur_1_t));

    mute[request->bChannelNumber] = ((audio_control_cur_1_t const *)buf)->bCur;

    TU_LOG1("Set channel %d Mute: %d\r\n", request->bChannelNumber, mute[request->bChannelNumber]);

    return true;
  }
  else if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME)
  {
    TU_VERIFY(request->wLength == sizeof(audio_control_cur_2_t));

    volume[request->bChannelNumber] = ((audio_control_cur_2_t const *)buf)->bCur;

    TU_LOG1("Set channel %d volume: %d dB\r\n", request->bChannelNumber, volume[request->bChannelNumber] / 256);

    return true;
  }
  else
  {
    TU_LOG1("Feature unit set request not supported, entity = %u, selector = %u, request = %u\r\n",
            request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
  }
}

static bool tud_audio_selector_unit_get_request(uint8_t rhport, audio_control_request_t const *request)
{
  TU_ASSERT(request->bEntityID == UAC2_ENTYTY_MIC_SELECTOR_UNIT);

  if(request->bControlSelector == AUDIO_SU_CTRL_SELECTOR && request->bRequest == AUDIO_CS_REQ_CUR)
  {
    audio_control_cur_1_t ctrl_selector = { .bCur = selector };
    TU_LOG1("Get selector value %d\r\n", ctrl_selector.bCur);
    return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &ctrl_selector, sizeof(selector));
  }
  return false;
}

static bool tud_audio_selector_unit_set_request(uint8_t rhport, audio_control_request_t const *request, uint8_t const *buf)
{
  (void)rhport;

  TU_ASSERT(request->bEntityID == UAC2_ENTYTY_MIC_SELECTOR_UNIT);
  TU_VERIFY(request->bRequest == AUDIO_CS_REQ_CUR);

  if(request->bControlSelector == AUDIO_SU_CTRL_SELECTOR)
  {
    int8_t curr_data;
    TU_VERIFY(request->wLength == sizeof(audio_control_cur_1_t));

    curr_data = ((audio_control_cur_1_t const *)buf)->bCur;

    if((curr_data == 0) || (curr_data > 2)) return false;

    if(curr_data != selector)
    {
      if(um_handle_set_driver(um_in_buffer, in_terminal_lut[curr_data-1].usb_packet_size,
          in_terminal_lut[curr_data-1].play_cb, in_terminal_lut[curr_data-1].pause_resume_cb) != UM_EOK)
      return false;

      selector = curr_data;
    }

    TU_LOG1("Set selector value: %d\r\n", selector);

    return true;
  }

  return false;
}

static bool tud_audio_input_terminal_get_request(uint8_t rhport, audio_control_request_t const *request)
{
  TU_ASSERT((request->bEntityID == UAC2_ENTITY_MIC_INPUT_TERMINAL1) || (request->bEntityID == UAC2_ENTITY_MIC_INPUT_TERMINAL2));

  if(request->bControlSelector == AUDIO_TE_CTRL_CONNECTOR && request->bRequest == AUDIO_CS_REQ_CUR)
  {
    audio_desc_channel_cluster_t cluster_config = { .bmChannelConfig = AUDIO_CHANNEL_CONFIG_FRONT_CENTER, .iChannelNames = 0 };
    cluster_config.bNrChannels = in_terminal_lut[selector - 1].__terminal_id == request->bEntityID ? 1 : 0;

    TU_LOG1("Get IN terminal (term id: %d) connected logical channels %d\r\n", request->bEntityID, cluster_config.bNrChannels);

    return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cluster_config, sizeof(cluster_config));
  }

  return false;
}

static bool tud_audio_itf_active_alt_setting_get_request(uint8_t rhport, audio_control_request_t const *request)
{
  audio_control_cur_1_t ctrl_active_alt_setting;

  ctrl_active_alt_setting.bCur = active_alt_settings[request->bInterface];
  return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &ctrl_active_alt_setting, sizeof(ctrl_active_alt_setting));
}

static bool tud_audio_itf_valid_alt_setting_get_request(uint8_t rhport, audio_control_request_t const *request)
{
  uint8_t response[2];
  response[0] = 1;
  switch (request->bInterface)
  {
  case ITF_NUM_AUDIO_STREAMING_SPK:
    /* For the speaker, all alt settings are valid */
    response[1] = TU_BIT(ALT_SETTING_SPK_TOTAL) - 1;
    break;
  case ITF_NUM_AUDIO_STREAMING_MIC:
    response[1] = in_terminal_lut[selector-1].valid_alt_settings_bitmap;
    break;
  default:
    return false;
  }

  return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, response, sizeof(response));
}

//--------------------------------------------------------------------+
// Application Callback API Implementations
//--------------------------------------------------------------------+

// Invoked when audio class specific get request received for an entity
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
  audio_control_request_t const *request = (audio_control_request_t const *)p_request;

  if (request->bEntityID == UAC2_ENTITY_CLOCK)
    return tud_audio_clock_get_request(rhport, request);
  if (request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT)
    return tud_audio_feature_unit_get_request(rhport, request);
  if (request->bEntityID == UAC2_ENTYTY_MIC_SELECTOR_UNIT)
    return tud_audio_selector_unit_get_request(rhport, request);
  if (request->bEntityID == UAC2_ENTITY_MIC_INPUT_TERMINAL1 || request->bEntityID == UAC2_ENTITY_MIC_INPUT_TERMINAL2)
    return tud_audio_input_terminal_get_request(rhport, request);
  else
  {
    TU_LOG1("Get request not handled, entity = %d, selector = %d, request = %d\r\n",
            request->bEntityID, request->bControlSelector, request->bRequest);
  }
  return false;
}

// Invoked when audio class specific set request received for an entity
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf)
{
  audio_control_request_t const *request = (audio_control_request_t const *)p_request;

  if (request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT)
    return tud_audio_feature_unit_set_request(rhport, request, buf);
  if (request->bEntityID == UAC2_ENTITY_CLOCK)
    return tud_audio_clock_set_request(rhport, request, buf);
  if (request->bEntityID == UAC2_ENTYTY_MIC_SELECTOR_UNIT)
    return tud_audio_selector_unit_set_request(rhport, request, buf);
  TU_LOG1("Set request not handled, entity = %d, selector = %d, request = %d\r\n",
          request->bEntityID, request->bControlSelector, request->bRequest);

  return false;
}

// Invoked when audio class specific get request received for an interface
bool tud_audio_get_req_itf_cb(uint8_t rhport, tusb_control_request_t const * p_request)
{
  (void) rhport;
  audio_control_request_t const *request = (audio_control_request_t const *)p_request;

  TU_VERIFY(request->bInterface < ITF_NUM_TOTAL);
  /* Used controls support only CUR attr */
  TU_VERIFY(request->bRequest == AUDIO_CS_REQ_CUR);

  switch(request->bControlSelector)
  {
    case AUDIO_AS_CTRL_ACT_ALT_SETTING:
      return tud_audio_itf_active_alt_setting_get_request(rhport, request);
    case AUDIO_AS_CTRL_VAL_ALT_SETTINGS:
      return tud_audio_itf_valid_alt_setting_get_request(rhport, request);
  }

  return false;
}

bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const * p_request)
{
  (void)rhport;

  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

  if (ITF_NUM_AUDIO_STREAMING_SPK == itf && alt == 0)
    return true;

  return true;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const * p_request)
{
  (void)rhport;
  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

  TU_VERIFY(itf < ITF_NUM_TOTAL);

  if(itf == ITF_NUM_AUDIO_STREAMING_MIC)
  {
    if(alt == ALT_SETTING_MIC_ZERO_BANDWIDTH)
    {
      um_handle_pause(um_in_buffer);
    }
    else if(alt == 1)
    {
      um_handle_dequeue(um_in_buffer, um_in_buffer->um_usb_packet_size);
    }
  }
  else if (itf == ITF_NUM_AUDIO_STREAMING_SPK)
  {
    if(alt == ALT_SETTING_SPK_ZERO_BANDWIDTH)
    {
      FBCK_Stop();
    }
    else if(alt == 1)
    {
      FBCK_Start();
    }
  }

  active_alt_settings[itf] = alt;

  TU_LOG2("Set interface %d alt %d\r\n", itf, alt);
  return true;
}

bool tud_audio_rx_done_pre_read_cb(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting)
{
  uint16_t real_pkt_size = 0;
  (void)rhport;
  (void)func_id;
  (void)ep_out;
  (void)cur_alt_setting;

  real_pkt_size = tud_audio_read(um_out_buffer->cur_um_node_for_usb->um_buf + um_out_buffer->cur_um_node_for_usb->um_node_offset, n_bytes_received);

  um_handle_enqueue(um_out_buffer, real_pkt_size);

  return true;
}

bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t itf, uint8_t ep_in, uint8_t cur_alt_setting)
{
  (void)rhport;
  (void)itf;
  (void)cur_alt_setting;
  (void)ep_in;

  tud_audio_write(um_handle_dequeue(um_in_buffer, um_in_buffer->um_usb_packet_size), um_in_buffer->um_usb_packet_size);

  return true;
}

void tud_audio_feedback_params_cb(uint8_t func_id, uint8_t alt_itf, audio_feedback_params_t* feedback_param)
{
  (void)func_id;
  (void)alt_itf;

  feedback_param->method = AUDIO_FEEDBACK_METHOD_FREQUENCY_FIXED;
  feedback_param->frequency.mclk_freq = 256 * current_sample_rate;
  feedback_param->sample_freq = current_sample_rate;
}


void feedback_sender_task(void)
{
  while(1)
  {
    uint32_t new_feedback;
    if( !osal_queue_receive(__fbck_q, &new_feedback, UINT32_MAX) ) return;

    tud_audio_feedback_update(0, new_feedback);
  }
}

void FBCK_send_feedback(uint32_t feedback)
{
  uint32_t f = feedback;

  osal_queue_send(__fbck_q, &f, true);
}

void EVAL_AUDIO_HalfCpltCallback(void)
{
  audio_dma_complete_cb(um_out_buffer);
}

void EVAL_AUDIO_CpltCallback(void)
{
  audio_dma_complete_cb(um_out_buffer);
}

void MEMS_MIC_HalfCpltCallback(void)
{
  audio_dma_complete_cb(um_in_buffer);
}

void MEMS_MIC_CpltCallback(void)
{
  audio_dma_complete_cb(um_in_buffer);
}

void Analog_MIC_ConvCpltCallback(void)
{
  audio_dma_complete_cb(um_in_buffer);
}

void Analog_MIC_ConvHalfCpltCallback(void)
{
  audio_dma_complete_cb(um_in_buffer);
}
