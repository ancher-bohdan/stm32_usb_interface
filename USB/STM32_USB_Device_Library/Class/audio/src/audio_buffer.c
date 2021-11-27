#include "audio_buffer.h"
#include "usbd_audio_core.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define NODE_SUBBUF_COUNT   (UM_NODE_COUNT * NUMBER_OF_USB_FRAMES_IN_UM_NODE)

static uint8_t __um_buffer[UM_NODE_COUNT][AUDIO_OUT_PACKET * NUMBER_OF_USB_FRAMES_IN_UM_NODE];
static uint8_t congestion_avoidance_bucket[AUDIO_OUT_PACKET];

static struct um_buffer_handle um_handle;

static struct um_node** _alloc_um_nodes(struct um_node **prev)
{
    static int recursion_count = 0;

    *prev = (struct um_node *)malloc(sizeof(struct um_node));

    if(*prev == NULL)
        return NULL;

    (*prev)->um_buf = __um_buffer[recursion_count];

    (*prev)->um_node_state = UM_NODE_STATE_FREE;
    (*prev)->um_node_offset = 0;

    if(recursion_count != (UM_NODE_COUNT - 1)) {
        recursion_count++;
        return _alloc_um_nodes(&((*prev)->next));
    } else {
        recursion_count = 0;
        return prev;
    }
}

static uint8_t get_congestion_window(struct um_node *um_node_write)
{
    return um_node_write->um_node_state != UM_NODE_STATE_FREE ? 1 : 1 + get_congestion_window(um_node_write->next);
}

static void copy_samples_to_listener(struct um_buffer_listener *listener, struct usb_sample_struct *src, uint8_t size)
{
    uint8_t i = 0;
    for(i = 0; i < size; i++)
    {
        *(listener->dst + listener->dst_offset) = (src + i)->left_channel;

        listener->dst_offset++;
        listener->samples_required--;

        if(listener->samples_required == 0)
        {
            listener_job_finish finish = listener->listener_finish;
            void *tmp = listener->args;

            listener->dst = NULL;
            listener->dst_offset = 0;
            listener->listener_finish = NULL;
            listener->args = NULL;
            listener->samples_required = 0;

            finish(tmp);

            return;
        }
    }
}

static void send_sample_to_registered_listeners(struct usb_sample_struct *usb_samples, uint8_t size)
{
    uint8_t i = 0;
    for(i = 0; i < UM_BUFFER_LISTENER_COUNT; i++)
    {
        if(um_handle.listeners[i].samples_required != 0)
        {
            copy_samples_to_listener(&um_handle.listeners[i], usb_samples, size);
        }
    }
}

static void flush_all_listeners()
{
    uint8_t i = 0;
    for(i = 0; i < UM_BUFFER_LISTENER_COUNT; i++)
    {
        if(um_handle.listeners[i].samples_required != 0)
        {
            memset(um_handle.listeners[i].dst + um_handle.listeners[i].dst_offset, 0, um_handle.listeners[i].samples_required);
            um_handle.listeners[i].listener_finish(um_handle.listeners[i].args);

            um_handle.listeners[i].dst = NULL;
            um_handle.listeners[i].dst_offset = 0;
            um_handle.listeners[i].listener_finish = NULL;
            um_handle.listeners[i].args = NULL;
            um_handle.listeners[i].samples_required = 0;
        }
    }
}

void um_handle_init(um_play_fnc play, um_pause_resume_fnc pause_resume )
{
    um_handle.um_read = *_alloc_um_nodes(&(um_handle.um_start));
    if(um_handle.um_read == NULL)
    {
        /*
        Out of heap memory error
        As a handle it is endless loop here so far... 
          */
        while(1){ }
    }

    um_handle.um_read->next = um_handle.um_start;
    um_handle.um_read = um_handle.um_start;
    um_handle.um_write = um_handle.um_start;
    um_handle.um_buffer_state = UM_BUFFER_STATE_INIT;
    um_handle.um_abs_offset = 0;
    um_handle.um_buffer_flags = 0;

    um_handle.um_play = play;
    um_handle.um_pause_resume = pause_resume;
}

uint8_t *um_handle_enqueue()
{
    uint8_t cw;
    uint8_t *result = NULL;

    if((um_handle.um_write->um_node_offset == 0) && !GET_HALF_USB_FRAME_FLAG(um_handle.um_buffer_flags))
    {
        if(um_handle.um_write->um_node_state != UM_NODE_STATE_FREE)
        {
            /* Buffer overflow
            shouldn`t be here
            as a handle - endless loop
            */
           while(1) {}
        }
        um_handle.um_write->um_node_state = UM_NODE_STATE_USB;
    }

    if(!GET_CONGESTION_AVOIDANCE_FLAG(um_handle.um_buffer_flags))
    {
        send_sample_to_registered_listeners((struct usb_sample_struct *)(um_handle.um_start->um_buf + (um_handle.um_abs_offset * AUDIO_OUT_PACKET)), AUDIO_OUT_PACKET >> 2);
        um_handle.um_abs_offset = (um_handle.um_abs_offset + 1) % NODE_SUBBUF_COUNT;

        if(++(um_handle.um_write->um_node_offset) == NUMBER_OF_USB_FRAMES_IN_UM_NODE)
        {
            um_handle.um_write->um_node_offset = 0;
            um_handle.um_write->um_node_state = UM_NODE_STATE_READY;
            um_handle.um_write = um_handle.um_write->next;
        }
        result = um_handle.um_write->um_buf + (um_handle.um_write->um_node_offset * AUDIO_OUT_PACKET);
    }
    else /*CONGESTION AVOIDANCE in progress... */
    {
        uint8_t i = 0, j = 0;

        send_sample_to_registered_listeners((struct usb_sample_struct *)(congestion_avoidance_bucket), AUDIO_OUT_PACKET >> 2);

        for(i = 0; i < AUDIO_OUT_PACKET; i+=8)
        {
            memcpy(um_handle.um_start->um_buf + (um_handle.um_abs_offset * AUDIO_OUT_PACKET) + ((AUDIO_OUT_PACKET >> 1) * GET_HALF_USB_FRAME_FLAG(um_handle.um_buffer_flags)) + j, congestion_avoidance_bucket + i, 4);
            j+=4;
        }

        if(GET_HALF_USB_FRAME_FLAG(um_handle.um_buffer_flags))
        {
            um_handle.um_abs_offset = (um_handle.um_abs_offset + 1) % NODE_SUBBUF_COUNT;

            if(++(um_handle.um_write->um_node_offset) == NUMBER_OF_USB_FRAMES_IN_UM_NODE)
            {
                um_handle.um_write->um_node_offset = 0;
                um_handle.um_write->um_node_state = UM_NODE_STATE_READY;
                um_handle.um_write = um_handle.um_write->next;
            }
        }

        TOGGLE_HALF_USB_FRAME_FLAG(um_handle.um_buffer_flags);
        result = congestion_avoidance_bucket;
    }

    cw = get_congestion_window(um_handle.um_write->next);

    if(GET_CONGESTION_AVOIDANCE_FLAG(um_handle.um_buffer_flags))
    {
        if((cw == CW_UPPER_BOUND) && !GET_HALF_USB_FRAME_FLAG(um_handle.um_buffer_flags))
        {
            TOGGLE_CONGESTION_AVOIDANCE_FLAG(um_handle.um_buffer_flags);
            result = um_handle.um_write->um_buf + (um_handle.um_write->um_node_offset * AUDIO_OUT_PACKET);
        }
    }
    else
    {
        if(cw == CW_LOWER_BOUND)
        {
            TOGGLE_CONGESTION_AVOIDANCE_FLAG(um_handle.um_buffer_flags);
            result = congestion_avoidance_bucket;
        }
    }

    if(um_handle.um_buffer_state != UM_BUFFER_STATE_PLAY)
    {
        if(um_handle.um_abs_offset == (NODE_SUBBUF_COUNT >> 1))
        {
            um_handle.um_start->um_node_state = UM_NODE_STATE_I2S;
            if(um_handle.um_buffer_state == UM_BUFFER_STATE_INIT)
            {
                um_handle.um_play((uint32_t)um_handle.um_start->um_buf, (NODE_SUBBUF_COUNT * AUDIO_OUT_PACKET) >> 1);
            }
            else /* UM_BUFFER_STATE_READY */
            {
                um_handle.um_pause_resume(1, (uint32_t)um_handle.um_start->um_buf, (NODE_SUBBUF_COUNT * AUDIO_OUT_PACKET) >> 1);
            }
            um_handle.um_buffer_state = UM_BUFFER_STATE_PLAY;
        }
    }
    return result;
}

void audio_dma_complete_cb()
{
    if(um_handle.um_read->um_node_state != UM_NODE_STATE_I2S)
    {
        /* State machine error */
        while(1) {}
    }
    um_handle.um_read->um_node_state = UM_NODE_STATE_FREE;
    um_handle.um_read = um_handle.um_read->next;

    switch (um_handle.um_read->um_node_state)
    {
    case UM_NODE_STATE_USB:
        if(um_handle.um_read != um_handle.um_write)
        {
            /* This state may occure, if new buffer node is under 
            USB packet filling, but not finish and no more packet
            is coming from USB endpoint.

            In that case um_read should be equal to um_write.

            If it is not - state machine error */
            while(1) { }
        }
        // else, same handle as for UM_NODE_STATE_FREE state
    case UM_NODE_STATE_FREE:
        um_handle.um_pause_resume(0, (uint32_t)um_handle.um_start->um_buf, (NODE_SUBBUF_COUNT * AUDIO_OUT_PACKET) >> 1);
        um_handle.um_write->um_node_offset = 0;
        um_handle.um_write->um_node_state = UM_NODE_STATE_FREE;
        um_handle.um_write = um_handle.um_read = um_handle.um_start;
        um_handle.um_buffer_state = UM_BUFFER_STATE_READY;
        um_handle.um_abs_offset = 0;

        flush_all_listeners();
        break;

    case UM_NODE_STATE_READY:
        um_handle.um_read->um_node_state = UM_NODE_STATE_I2S;
        break;
    case UM_NODE_STATE_I2S:
    default:
        /* State machine error */
        while(1) {}
        break;
    }
}

void EVAL_AUDIO_TransferComplete_CallBack(uint32_t pBuffer, uint32_t Size)
{
    audio_dma_complete_cb();
}

void EVAL_AUDIO_HalfTransfer_CallBack(uint32_t pBuffer, uint32_t Size)
{
    audio_dma_complete_cb();
}

void um_buffer_handle_register_listener(int16_t *sample, uint16_t size, listener_job_finish job_finish_cbk, void *arg)
{
    uint8_t i = 0;
    for(i = 0; i < UM_BUFFER_LISTENER_COUNT; i++)
    {
        if(um_handle.listeners[i].samples_required == 0)
        {
            um_handle.listeners[i].samples_required = size;
            um_handle.listeners[i].listener_finish = job_finish_cbk;
            um_handle.listeners[i].args = arg;
            um_handle.listeners[i].dst_offset = 0;
            um_handle.listeners[i].dst = sample;
            return;
        }
    }
    //not enought space for register listener. As a handle - endless loop
    while(1) { }
}

struct um_buffer_handle *get_um_buffer_handle()
{
    return &um_handle;
}