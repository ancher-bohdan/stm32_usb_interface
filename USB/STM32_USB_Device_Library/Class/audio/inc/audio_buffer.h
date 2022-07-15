#ifndef __AUDIO_BUFFER_INIT___
#define __AUDIO_BUFFER_INIT___

#include <stdint.h>

#define UM_BUFFER_LISTENER_COUNT            4

#define CW_LOWER_BOUND                      1
#define CW_UPPER_BOUND                      3

#define UM_BUFFER_CONFIG_LISTENERS_EN       0x01
#define UM_BUFFER_CONFIG_CA_NONE            0x00
#define UM_BUFFER_CONFIG_CA_DROP_HALF_PKT   0x02
#define UM_BUFFER_CONFIG_CA_FEEDBACK        0x04

#define UM_BUFFER_FLAG_BUF_LISTENERS_NEMPTY 0x4
#define UM_BUFFER_FLAG_CONGESTION_AVIODANCE 0x2
#define UM_BUFFER_FLAG_HALF_USB_FRAME       0x1

#define GET_CONFIG_LISTENERS_EN(config)     ((config) & UM_BUFFER_CONFIG_LISTENERS_EN)
#define GET_CONFIG_CA_ALGORITM(config)      ((config) & (UM_BUFFER_CONFIG_CA_DROP_HALF_PKT | UM_BUFFER_CONFIG_CA_FEEDBACK))

#define GET_BUF_LISTENERS_NEMPTY_FLAG(flag) ((flag) & UM_BUFFER_FLAG_BUF_LISTENERS_NEMPTY)
#define GET_CONGESTION_AVOIDANCE_FLAG(flag) ((flag) & UM_BUFFER_FLAG_CONGESTION_AVIODANCE)
#define GET_HALF_USB_FRAME_FLAG(flag)       ((flag) & UM_BUFFER_FLAG_HALF_USB_FRAME)

#define TOGGLE_BUF_LISTENERS_NEMPTY_FLAG(flag)  (flag) = ((flag) ^ UM_BUFFER_FLAG_BUF_LISTENERS_NEMPTY)
#define TOGGLE_CONGESTION_AVOIDANCE_FLAG(flag)  (flag) = ((flag) ^ UM_BUFFER_FLAG_CONGESTION_AVIODANCE)
#define TOGGLE_HALF_USB_FRAME_FLAG(flag)        (flag) = ((flag) ^ UM_BUFFER_FLAG_HALF_USB_FRAME)

#define UM_EOK                              0
#define UM_ENOMEM                           -1
#define UM_ESATE                            -2
#define UM_EBUFOVERFLOW                     -3
#define UM_EARGS                            -4

enum um_node_state
{
    UM_NODE_STATE_HW_FINISHED = 0,
    UM_NODE_STATE_UNDER_USB,
    UM_NODE_STATE_USB_FINISHED,
    UM_NODE_STATE_UNDER_HW,
    UM_NODE_STATE_INITIAL = 0xFF
};

enum um_buffer_state
{
    UM_BUFFER_STATE_INIT = 0,
    UM_BUFFER_STATE_READY,
    UM_BUFFER_STATE_PLAY
};

struct um_node
{
    uint8_t *um_buf;
    struct um_node *next;
    uint32_t um_node_offset;
    enum um_node_state um_node_state;
};

struct um_buffer_listener
{
    int16_t *dst;
    uint16_t dst_offset;
    uint16_t samples_required;

    void (*listener_finish)(void *args);
    void *args;
};

struct um_buffer_handle
{
    struct um_node *cur_um_node_for_hw;
    struct um_node *cur_um_node_for_usb;
    struct um_node *start_um_node;
    uint8_t *congestion_avoidance_bucket;

    uint32_t um_usb_packet_size;
    uint16_t um_usb_frame_in_node;
    uint16_t um_number_of_nodes;
    uint32_t um_abs_offset;

    uint32_t um_buffer_size_in_one_node;
    uint32_t total_buffer_size;

    enum um_buffer_state um_buffer_state;
    uint8_t um_buffer_flags;
    uint8_t um_buffer_config;
    struct um_buffer_listener *listeners;

    void (*um_play)(uint32_t addr, uint32_t size);
    uint32_t (*um_pause_resume)(uint32_t Cmd, uint32_t Addr, uint32_t Size);
};

struct usb_sample_struct
{
    uint16_t left_channel;
    uint16_t right_channel;
};

typedef void (*listener_job_finish)(void *args);
typedef void (*um_play_fnc)(uint32_t addr, uint32_t size);
typedef uint32_t (*um_pause_resume_fnc)(uint32_t Cmd, uint32_t Addr, uint32_t Size);

int um_handle_init( struct um_buffer_handle *handle,
                    uint32_t usb_packet_size,
                    uint32_t usb_frame_in_um_node_count,
                    uint32_t um_node_count,
                    uint8_t configs,
                    um_play_fnc play, um_pause_resume_fnc pause_resume );
uint8_t *um_handle_enqueue(struct um_buffer_handle *handle, uint16_t pkt_size);
void audio_dma_complete_cb(struct um_buffer_handle *handle);

void um_buffer_handle_register_listener(struct um_buffer_handle *handle, int16_t *sample, uint16_t size, listener_job_finish job_finish_cbk, void *arg);

void um_handle_in_pause(struct um_buffer_handle *handle);
uint8_t *um_handle_in_resume(struct um_buffer_handle *handle);
uint8_t *um_handle_in_dequeue(struct um_buffer_handle *handle);
void um_handle_in_cbk(struct um_buffer_handle *handle);
void um_handle_in_trigger_resume(struct um_buffer_handle *handle);
uint8_t *um_handle_in_event_dispatcher(struct um_buffer_handle *handle);

void free_um_buffer_handle(struct um_buffer_handle *handle);

#endif