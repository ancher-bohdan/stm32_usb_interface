#include "audio_buffer.h"
#include "usbd_audio_core.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static struct um_node** _alloc_um_nodes(struct um_buffer_handle *handle, struct um_node **prev)
{
    static int recursion_count = 0;

    *prev = (struct um_node *)malloc(sizeof(struct um_node));

    if(*prev == NULL)
        return NULL;

    (*prev)->um_buf = handle->congestion_avoidance_bucket + (handle->um_usb_frame_in_node * handle->um_usb_packet_size * recursion_count);

    (*prev)->um_node_state = UM_NODE_STATE_FREE;
    (*prev)->um_node_offset = 0;

    if(recursion_count != (handle->um_number_of_nodes - 1)) {
        recursion_count++;
        return _alloc_um_nodes(handle, &((*prev)->next));
    } else {
        recursion_count = 0;
        return prev;
    }
}

static void _free_um_nodes(struct um_node *current, struct um_node *start)
{
    if(current != start)
    {
        _free_um_nodes(current->next, start);
    }
    free(current);
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

static void send_sample_to_registered_listeners(struct um_buffer_handle *handle, struct usb_sample_struct *usb_samples, uint8_t size)
{
    uint8_t i = 0;
    for(i = 0; i < UM_BUFFER_LISTENER_COUNT; i++)
    {
        if(handle->listeners[i].samples_required != 0)
        {
            copy_samples_to_listener(&(handle->listeners[i]), usb_samples, size);
        }
    }
}

static void flush_all_listeners(struct um_buffer_handle *handle)
{
    uint8_t i = 0;
    for(i = 0; i < UM_BUFFER_LISTENER_COUNT; i++)
    {
        if(handle->listeners[i].samples_required != 0)
        {
            memset(handle->listeners[i].dst + handle->listeners[i].dst_offset, 0, handle->listeners[i].samples_required);
            handle->listeners[i].listener_finish(handle->listeners[i].args);

            handle->listeners[i].dst = NULL;
            handle->listeners[i].dst_offset = 0;
            handle->listeners[i].listener_finish = NULL;
            handle->listeners[i].args = NULL;
            handle->listeners[i].samples_required = 0;
        }
    }
}

int um_handle_init( struct um_buffer_handle *handle,
                    uint32_t usb_packet_size,
                    uint32_t usb_frame_in_um_node_count,
                    uint32_t um_node_count,
                    um_play_fnc play, um_pause_resume_fnc pause_resume )
{
    int i = 0;

    if(handle == NULL)
    {
        return UM_EARGS;
    }

    handle->um_usb_packet_size = usb_packet_size;
    handle->um_usb_frame_in_node = usb_frame_in_um_node_count;
    handle->um_number_of_nodes = um_node_count;

    // allocate memory for whole internal buffer; store pointer to it here temporary
    handle->congestion_avoidance_bucket = (uint8_t *)malloc(usb_packet_size * usb_frame_in_um_node_count * um_node_count);
    if(handle->congestion_avoidance_bucket == NULL)
    {
        return UM_ENOMEM;
    }

    handle->um_read = *_alloc_um_nodes(handle, &(handle->um_start));
    if(handle->um_read == NULL)
    {
        return UM_ENOMEM;
    }

    handle->um_read->next = handle->um_start;
    handle->um_read = handle->um_start;
    handle->um_write = handle->um_start;

    //main buffer pointer was copied inside um_start struct; allocate new memory for CA algorithm
    handle->congestion_avoidance_bucket = (uint8_t *)malloc(handle->um_usb_packet_size);
    if(handle->congestion_avoidance_bucket == NULL)
    {
        return UM_ENOMEM;
    }

    handle->um_buffer_state = UM_BUFFER_STATE_INIT;
    handle->um_abs_offset = 0;
    handle->um_buffer_flags = 0;

    handle->um_play = play;
    handle->um_pause_resume = pause_resume;

    for(i = 0; i < UM_BUFFER_LISTENER_COUNT; i++)
    {
        handle->listeners[i].dst = NULL;
        handle->listeners[i].dst_offset = 0;
        handle->listeners[i].listener_finish = NULL;
        handle->listeners[i].args = NULL;
        handle->listeners[i].samples_required = 0;
    }

    return UM_EOK;
}

uint8_t *um_handle_enqueue(struct um_buffer_handle *handle)
{
    uint8_t cw;
    uint8_t *result = NULL;
    uint32_t node_subbuf_count = handle->um_usb_frame_in_node * handle->um_number_of_nodes;

    if((handle->um_write->um_node_offset == 0) && !GET_HALF_USB_FRAME_FLAG(handle->um_buffer_flags))
    {
        if(handle->um_write->um_node_state != UM_NODE_STATE_FREE)
        {
            /* Buffer overflow
            shouldn`t be here
            */
           return result;
        }
        handle->um_write->um_node_state = UM_NODE_STATE_WRITER;
    }

    if(!GET_CONGESTION_AVOIDANCE_FLAG(handle->um_buffer_flags))
    {
        send_sample_to_registered_listeners(handle, (struct usb_sample_struct *)(handle->um_start->um_buf + (handle->um_abs_offset * handle->um_usb_packet_size)), handle->um_usb_packet_size >> 2);
        handle->um_abs_offset = (handle->um_abs_offset + 1) % node_subbuf_count;

        if(++(handle->um_write->um_node_offset) == handle->um_usb_frame_in_node)
        {
            handle->um_write->um_node_offset = 0;
            handle->um_write->um_node_state = UM_NODE_STATE_READY;
            handle->um_write = handle->um_write->next;
        }
        result = handle->um_write->um_buf + (handle->um_write->um_node_offset * handle->um_usb_packet_size);
    }
    else /*CONGESTION AVOIDANCE in progress... */
    {
        uint8_t i = 0, j = 0;

        send_sample_to_registered_listeners(handle, (struct usb_sample_struct *)(handle->congestion_avoidance_bucket), handle->um_usb_packet_size >> 2);

        for(i = 0; i < handle->um_usb_packet_size; i+=8)
        {
            memcpy(handle->um_start->um_buf + (handle->um_abs_offset * handle->um_usb_packet_size) + ((handle->um_usb_packet_size >> 1) * GET_HALF_USB_FRAME_FLAG(handle->um_buffer_flags)) + j, handle->congestion_avoidance_bucket + i, 4);
            j+=4;
        }

        if(GET_HALF_USB_FRAME_FLAG(handle->um_buffer_flags))
        {
            handle->um_abs_offset = (handle->um_abs_offset + 1) % node_subbuf_count;

            if(++(handle->um_write->um_node_offset) == handle->um_usb_frame_in_node)
            {
                handle->um_write->um_node_offset = 0;
                handle->um_write->um_node_state = UM_NODE_STATE_READY;
                handle->um_write = handle->um_write->next;
            }
        }

        TOGGLE_HALF_USB_FRAME_FLAG(handle->um_buffer_flags);
        result = handle->congestion_avoidance_bucket;
    }

    cw = get_congestion_window(handle->um_write->next);

    if(GET_CONGESTION_AVOIDANCE_FLAG(handle->um_buffer_flags))
    {
        if((cw == CW_UPPER_BOUND) && !GET_HALF_USB_FRAME_FLAG(handle->um_buffer_flags))
        {
            TOGGLE_CONGESTION_AVOIDANCE_FLAG(handle->um_buffer_flags);
            result = handle->um_write->um_buf + (handle->um_write->um_node_offset * handle->um_usb_packet_size);
        }
    }
    else
    {
        if(cw == CW_LOWER_BOUND)
        {
            TOGGLE_CONGESTION_AVOIDANCE_FLAG(handle->um_buffer_flags);
            result = handle->congestion_avoidance_bucket;
        }
    }

    if(handle->um_buffer_state != UM_BUFFER_STATE_PLAY)
    {
        if(handle->um_abs_offset == (node_subbuf_count >> 1))
        {
            handle->um_start->um_node_state = UM_NODE_STATE_READER;
            if(handle->um_buffer_state == UM_BUFFER_STATE_INIT)
            {
                handle->um_play((uint32_t)handle->um_start->um_buf, (node_subbuf_count * handle->um_usb_packet_size) >> 1);
            }
            else /* UM_BUFFER_STATE_READY */
            {
                handle->um_pause_resume(1, (uint32_t)handle->um_start->um_buf, (node_subbuf_count * handle->um_usb_packet_size) >> 1);
            }
            handle->um_buffer_state = UM_BUFFER_STATE_PLAY;
        }
    }
    return result;
}

void audio_dma_complete_cb(struct um_buffer_handle *handle)
{
    uint32_t node_subbuf_count = handle->um_usb_frame_in_node * handle->um_number_of_nodes;

    if(handle->um_read->um_node_state != UM_NODE_STATE_READER)
    {
        /* State machine error */
        while(1) {}
    }
    handle->um_read->um_node_state = UM_NODE_STATE_FREE;
    handle->um_read = handle->um_read->next;

    switch (handle->um_read->um_node_state)
    {
    case UM_NODE_STATE_WRITER:
        if(handle->um_read != handle->um_write)
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
        handle->um_pause_resume(0, (uint32_t)handle->um_start->um_buf, (node_subbuf_count * handle->um_usb_packet_size) >> 1);
        handle->um_write->um_node_offset = 0;
        handle->um_write->um_node_state = UM_NODE_STATE_FREE;
        handle->um_write = handle->um_read = handle->um_start;
        handle->um_buffer_state = UM_BUFFER_STATE_READY;
        handle->um_abs_offset = 0;

        flush_all_listeners(handle);
        break;

    case UM_NODE_STATE_READY:
        handle->um_read->um_node_state = UM_NODE_STATE_READER;
        break;
    case UM_NODE_STATE_READER:
    default:
        /* State machine error */
        while(1) {}
        break;
    }
}

void um_buffer_handle_register_listener(struct um_buffer_handle *handle, int16_t *sample, uint16_t size, listener_job_finish job_finish_cbk, void *arg)
{
    uint8_t i = 0;
    for(i = 0; i < UM_BUFFER_LISTENER_COUNT; i++)
    {
        if(handle->listeners[i].samples_required == 0)
        {
            handle->listeners[i].samples_required = size;
            handle->listeners[i].listener_finish = job_finish_cbk;
            handle->listeners[i].args = arg;
            handle->listeners[i].dst_offset = 0;
            handle->listeners[i].dst = sample;
            return;
        }
    }
    //not enought space for register listener. As a handle - endless loop
    while(1) { }
}

void free_um_buffer_handle(struct um_buffer_handle *handle)
{
    uint32_t timeout = 10000;

    while (handle->um_buffer_state == UM_BUFFER_STATE_PLAY && timeout--);

    free(handle->congestion_avoidance_bucket);
    free(handle->um_start->um_buf);
    _free_um_nodes(handle->um_start->next, handle->um_start);
    free(handle);
}