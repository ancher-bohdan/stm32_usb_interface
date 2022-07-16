#include "audio_buffer.h"
#include "dsp.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static void usb_data_ready(void *arg)
{
    struct um_buffer_handle *handle = (struct um_buffer_handle *)arg;
    uint32_t node_subbuf_count = handle->um_usb_frame_in_node * handle->um_number_of_nodes;

    handle->cur_um_node_for_usb->um_node_state = UM_NODE_STATE_USB_FINISHED;
    handle->um_abs_offset = (handle->um_abs_offset + handle->um_usb_frame_in_node) % node_subbuf_count;
    handle->cur_um_node_for_usb = handle->cur_um_node_for_usb->next;

    if(handle->cur_um_node_for_usb->um_node_state != UM_NODE_STATE_HW_FINISHED)
    {
        //buffer overflow
        um_handle_pause(handle);
        um_handle_trigger_resume(handle);
    }
}

uint8_t *um_handle_in_resume(struct um_buffer_handle *handle)
{
    if(handle->um_buffer_state != UM_BUFFER_STATE_PLAY)
    {
        struct um_node *node = handle->start_um_node;

        do{
            node->um_node_offset = 0;
            node->um_node_state = UM_NODE_STATE_HW_FINISHED;
            node = node->next;
        }while(node != handle->start_um_node);

        handle->cur_um_node_for_hw = handle->cur_um_node_for_usb = handle->start_um_node;
        handle->um_abs_offset = 0;

        handle->cur_um_node_for_usb->um_node_state = UM_NODE_STATE_UNDER_USB;
        
        handle->um_play((uint32_t)handle->start_um_node->um_buf, (handle->um_number_of_nodes * handle->um_usb_frame_in_node * handle->um_usb_packet_size) >> 1);
    }

    return handle->start_um_node->um_buf + (handle->um_number_of_nodes * handle->um_usb_frame_in_node * handle->um_usb_packet_size) - handle->um_usb_packet_size;
}

uint8_t *um_handle_in_dequeue(struct um_buffer_handle *handle)
{
    if(handle->um_buffer_state != UM_BUFFER_STATE_PLAY)
    {
        if(handle->um_abs_offset < ((uint16_t)(handle->um_number_of_nodes * handle->um_usb_frame_in_node) >> 2))
        {
            return handle->start_um_node->um_buf + (handle->um_number_of_nodes * handle->um_usb_frame_in_node * handle->um_usb_packet_size) - handle->um_usb_packet_size;
        }

        handle->um_buffer_state = UM_BUFFER_STATE_PLAY;
        handle->cur_um_node_for_hw->um_node_state = UM_NODE_STATE_UNDER_HW;
        return handle->cur_um_node_for_hw->um_buf;
    }
    
    handle->cur_um_node_for_hw->um_node_offset++;
    if(handle->cur_um_node_for_hw->um_node_offset == handle->um_usb_frame_in_node)
    {
        if(handle->cur_um_node_for_hw->next->um_node_state != UM_NODE_STATE_USB_FINISHED)
        {
            //buffer underflow
            return (uint8_t *)0xFFFFFFFF;
        }
        handle->cur_um_node_for_hw->um_node_offset = 0;
        handle->cur_um_node_for_hw->um_node_state = UM_NODE_STATE_HW_FINISHED;
        handle->cur_um_node_for_hw = handle->cur_um_node_for_hw->next;
    }

    return handle->cur_um_node_for_hw->um_buf + (handle->cur_um_node_for_hw->um_node_offset * handle->um_usb_packet_size);
}