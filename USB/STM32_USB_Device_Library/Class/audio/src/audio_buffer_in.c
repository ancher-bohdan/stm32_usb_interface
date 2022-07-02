#include "audio_buffer.h"
#include "dsp.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static void usb_data_ready(void *arg)
{
    struct um_buffer_handle *handle = (struct um_buffer_handle *)arg;
    uint32_t node_subbuf_count = handle->um_usb_frame_in_node * handle->um_number_of_nodes;

    handle->um_write->um_node_state = UM_NODE_STATE_READY;
    handle->um_abs_offset = (handle->um_abs_offset + handle->um_usb_frame_in_node) % node_subbuf_count;
    handle->um_write = handle->um_write->next;

    if(handle->um_write->um_node_state != UM_NODE_STATE_FREE)
    {
        //buffer overflow
        um_handle_in_pause(handle);
        um_handle_in_trigger_resume(handle);
    }
}

void um_handle_in_trigger_resume(struct um_buffer_handle *handle)
{
    um_buffer_handle_register_listener(handle, NULL, 1, NULL, NULL);
    TOGGLE_BUF_LISTENERS_NEMPTY_FLAG(handle->um_buffer_flags);
}

uint8_t *um_handle_in_event_dispatcher(struct um_buffer_handle *handle)
{
    if(GET_BUF_LISTENERS_NEMPTY_FLAG(handle->um_buffer_flags))
    {
        uint8_t i = 0;
        struct um_buffer_listener *listener;
        for(listener = handle->listeners, i = 0;
            i < UM_BUFFER_LISTENER_COUNT;
            i++, listener += i)
        {
            if(listener->samples_required != 0)
            {
                listener->samples_required = 0;
                TOGGLE_BUF_LISTENERS_NEMPTY_FLAG(handle->um_buffer_flags);
                return um_handle_in_resume(handle);
            }
        }
    }
    return NULL;
}

void um_handle_in_pause(struct um_buffer_handle *handle)
{
    struct um_node *node = handle->um_start;

    do{
        node->um_node_offset = 0;
        node->um_node_state = UM_NODE_STATE_FREE;
        node = node->next;
    }while(node != handle->um_start);

    handle->um_read = handle->um_write = handle->um_start;
    handle->um_abs_offset = 0;

    handle->um_pause_resume(0, (uint32_t)handle->um_start->um_buf, 0);

    handle->um_buffer_state = UM_BUFFER_STATE_READY;
}

uint8_t *um_handle_in_resume(struct um_buffer_handle *handle)
{
    if(handle->um_buffer_state != UM_BUFFER_STATE_PLAY)
    {
        struct um_node *node = handle->um_start;

        do{
            node->um_node_offset = 0;
            node->um_node_state = UM_NODE_STATE_FREE;
            node = node->next;
        }while(node != handle->um_start);

        handle->um_read = handle->um_write = handle->um_start;
        handle->um_abs_offset = 0;

        handle->um_write->um_node_state = UM_NODE_STATE_WRITER;
        
        handle->um_play((uint32_t)handle->um_start->um_buf, (handle->um_number_of_nodes * handle->um_usb_frame_in_node * handle->um_usb_packet_size) >> 1);
    }

    return handle->um_start->um_buf + (handle->um_number_of_nodes * handle->um_usb_frame_in_node * handle->um_usb_packet_size) - handle->um_usb_packet_size;
}

uint8_t *um_handle_in_dequeue(struct um_buffer_handle *handle)
{
    if(handle->um_buffer_state != UM_BUFFER_STATE_PLAY)
    {
        if(handle->um_abs_offset < ((uint16_t)(handle->um_number_of_nodes * handle->um_usb_frame_in_node) >> 2))
        {
            return handle->um_start->um_buf + (handle->um_number_of_nodes * handle->um_usb_frame_in_node * handle->um_usb_packet_size) - handle->um_usb_packet_size;
        }

        handle->um_buffer_state = UM_BUFFER_STATE_PLAY;
        handle->um_read->um_node_state = UM_NODE_STATE_READER;
        return handle->um_read->um_buf;
    }
    
    handle->um_read->um_node_offset++;
    if(handle->um_read->um_node_offset == handle->um_usb_frame_in_node)
    {
        if(handle->um_read->next->um_node_state != UM_NODE_STATE_READY)
        {
            //buffer underflow
            return (uint8_t *)0xFFFFFFFF;
        }
        handle->um_read->um_node_offset = 0;
        handle->um_read->um_node_state = UM_NODE_STATE_FREE;
        handle->um_read = handle->um_read->next;
    }

    return handle->um_read->um_buf + (handle->um_read->um_node_offset * handle->um_usb_packet_size);
}

void um_handle_in_cbk(struct um_buffer_handle *handle)
{
    int res = dsp_calculation_request((int16_t *)handle->um_write->um_buf, usb_data_ready, (void *)handle);

    if(res)
    {
        while(1) {}
    }
}