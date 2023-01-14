#include "audio_buffer.h"

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

    (*prev)->um_node_state = UM_NODE_STATE_INITIAL;
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
    return um_node_write->um_node_state != UM_NODE_STATE_HW_FINISHED ? 1 : 1 + get_congestion_window(um_node_write->next);
}

static void reset_nodes_states_to_default(struct um_buffer_handle *handle)
{
    struct um_node *node = handle->start_um_node;

    do{
        node->um_node_offset = 0;
        node->um_node_state = UM_NODE_STATE_INITIAL;
        node = node->next;
    }while(node != handle->start_um_node);

    handle->cur_um_node_for_hw = handle->cur_um_node_for_usb = handle->start_um_node;
    handle->um_abs_offset = 0;
    handle->um_buffer_state = UM_BUFFER_STATE_READY;
}

int um_handle_init( struct um_buffer_handle *handle,
                    uint32_t usb_packet_size,
                    uint32_t usb_frame_in_um_node_count,
                    uint32_t um_node_count,
                    uint8_t config,
                    um_play_fnc play, um_pause_resume_fnc pause_resume )
{
    if(handle == NULL)
    {
        return UM_EARGS;
    }

    if(
       GET_CONFIG_CA_ALGORITM(config) != UM_BUFFER_CONFIG_CA_NONE
    && GET_CONFIG_CA_ALGORITM(config) != UM_BUFFER_CONFIG_CA_DROP_HALF_PKT
    && GET_CONFIG_CA_ALGORITM(config) != UM_BUFFER_CONFIG_CA_FEEDBACK
    )
    {
        return UM_EARGS;
    }

    handle->um_usb_packet_size = usb_packet_size;
    handle->um_usb_frame_in_node = usb_frame_in_um_node_count;
    handle->um_number_of_nodes = um_node_count;

    // allocate memory for whole internal buffer; store pointer to it here temporary
    if(GET_CONFIG_CA_ALGORITM(config) != UM_BUFFER_CONFIG_CA_NONE)
    {
        handle->congestion_avoidance_bucket = (uint8_t *)malloc((usb_packet_size * usb_frame_in_um_node_count * um_node_count) + usb_packet_size);
    }
    else
    {
        handle->congestion_avoidance_bucket = (uint8_t *)malloc((usb_packet_size * usb_frame_in_um_node_count * um_node_count));
    }

    if(handle->congestion_avoidance_bucket == NULL)
    {
        return UM_ENOMEM;
    }

    handle->cur_um_node_for_hw = *_alloc_um_nodes(handle, &(handle->start_um_node));
    if(handle->cur_um_node_for_hw == NULL)
    {
        return UM_ENOMEM;
    }

    handle->cur_um_node_for_hw->next = handle->start_um_node;
    handle->cur_um_node_for_hw = handle->start_um_node;
    handle->cur_um_node_for_usb = handle->start_um_node;

    //main buffer pointer was copied inside start_um_node struct; allocate new memory for CA algorithm (if it is nessesary)
    if(GET_CONFIG_CA_ALGORITM(config) != UM_BUFFER_CONFIG_CA_NONE)
    {
        handle->congestion_avoidance_bucket += (usb_packet_size * usb_frame_in_um_node_count * um_node_count);
    }
    else
    {
        handle->congestion_avoidance_bucket = NULL;
    }

    handle->um_buffer_state = UM_BUFFER_STATE_INIT;
    handle->um_abs_offset = 0;
    handle->um_buffer_flags = 0;
    handle->um_buffer_config = config;

    handle->um_buffer_size_in_one_node =
        GET_CONFIG_CA_ALGORITM(config) == UM_BUFFER_CONFIG_CA_FEEDBACK ?
        handle->um_usb_frame_in_node * handle->um_usb_packet_size :
        handle->um_usb_frame_in_node;
    handle->total_buffer_size = handle->um_buffer_size_in_one_node * handle->um_number_of_nodes;

    handle->um_play = play;
    handle->um_pause_resume = pause_resume;

    handle->listeners = NULL;

    return UM_EOK;
}

uint8_t test_flag_work = 0;
uint8_t *um_handle_enqueue(struct um_buffer_handle *handle, uint16_t pkt_size)
{
    uint8_t cw;
    uint8_t *result = NULL;

    switch(GET_CONFIG_CA_ALGORITM(handle->um_buffer_config))
    {
        case UM_BUFFER_CONFIG_CA_NONE:
            if(handle->cur_um_node_for_usb->um_node_offset == 0)
            {
                /* Check for buffer overflow */
                UM_ASSERT((handle->cur_um_node_for_usb->um_node_state == UM_NODE_STATE_HW_FINISHED) || (handle->cur_um_node_for_usb->um_node_state == UM_NODE_STATE_INITIAL), result);

                handle->cur_um_node_for_usb->um_node_state = UM_NODE_STATE_UNDER_USB;
            }

            handle->um_abs_offset = (handle->um_abs_offset + 1) % handle->total_buffer_size;

            if(++(handle->cur_um_node_for_usb->um_node_offset) == handle->um_usb_frame_in_node)
            {
                handle->cur_um_node_for_usb->um_node_offset = 0;
                handle->cur_um_node_for_usb->um_node_state = UM_NODE_STATE_USB_FINISHED;
                handle->cur_um_node_for_usb = handle->cur_um_node_for_usb->next;
            }
            result = handle->cur_um_node_for_usb->um_buf + (handle->cur_um_node_for_usb->um_node_offset * handle->um_usb_packet_size);
        break;/* UM_BUFFER_CONFIG_CA_NONE */

        case UM_BUFFER_CONFIG_CA_DROP_HALF_PKT:
            if((handle->cur_um_node_for_usb->um_node_offset == 0) && !GET_HALF_USB_FRAME_FLAG(handle->um_buffer_flags))
            {
                /* Check for buffer overflow */
                UM_ASSERT((handle->cur_um_node_for_usb->um_node_state == UM_NODE_STATE_HW_FINISHED) || (handle->cur_um_node_for_usb->um_node_state == UM_NODE_STATE_INITIAL), result);
    
                handle->cur_um_node_for_usb->um_node_state = UM_NODE_STATE_UNDER_USB;
            }

            if(!GET_CONGESTION_AVOIDANCE_FLAG(handle->um_buffer_flags))
            {
                handle->um_abs_offset = (handle->um_abs_offset + 1) % handle->total_buffer_size;

                if(++(handle->cur_um_node_for_usb->um_node_offset) == handle->um_usb_frame_in_node)
                {
                    handle->cur_um_node_for_usb->um_node_offset = 0;
                    handle->cur_um_node_for_usb->um_node_state = UM_NODE_STATE_USB_FINISHED;
                    handle->cur_um_node_for_usb = handle->cur_um_node_for_usb->next;
                }
                result = handle->cur_um_node_for_usb->um_buf + (handle->cur_um_node_for_usb->um_node_offset * handle->um_usb_packet_size);
            }
            else /* CONGESTION AVOIDANCE in progress... */
            {
                uint8_t i = 0, j = 0;

                for(i = 0; i < handle->um_usb_packet_size; i+=8)
                {
                    memcpy(handle->start_um_node->um_buf + (handle->um_abs_offset * handle->um_usb_packet_size) + ((handle->um_usb_packet_size >> 1) * GET_HALF_USB_FRAME_FLAG(handle->um_buffer_flags)) + j, handle->congestion_avoidance_bucket + i, 4);
                    j+=4;
                }

                if(GET_HALF_USB_FRAME_FLAG(handle->um_buffer_flags))
                {
                    handle->um_abs_offset = (handle->um_abs_offset + 1) % handle->total_buffer_size;

                    if(++(handle->cur_um_node_for_usb->um_node_offset) == handle->um_usb_frame_in_node)
                    {
                        handle->cur_um_node_for_usb->um_node_offset = 0;
                        handle->cur_um_node_for_usb->um_node_state = UM_NODE_STATE_USB_FINISHED;
                        handle->cur_um_node_for_usb = handle->cur_um_node_for_usb->next;
                    }
                }

                TOGGLE_HALF_USB_FRAME_FLAG(handle->um_buffer_flags);
                result = handle->congestion_avoidance_bucket;
            }

            cw = get_congestion_window(handle->cur_um_node_for_usb->next);

            if(GET_CONGESTION_AVOIDANCE_FLAG(handle->um_buffer_flags))
            {
                if((cw == CW_UPPER_BOUND) && !GET_HALF_USB_FRAME_FLAG(handle->um_buffer_flags))
                {
                    TOGGLE_CONGESTION_AVOIDANCE_FLAG(handle->um_buffer_flags);
                    result = handle->cur_um_node_for_usb->um_buf + (handle->cur_um_node_for_usb->um_node_offset * handle->um_usb_packet_size);
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
        break; /* UM_BUFFER_CONFIG_CA_DROP_HALF_PKT */

        case UM_BUFFER_CONFIG_CA_FEEDBACK:
            handle->cur_um_node_for_usb->um_node_offset += pkt_size;
            handle->um_abs_offset += pkt_size;

            if(handle->cur_um_node_for_usb->um_node_offset >= handle->um_buffer_size_in_one_node)
            {
                if(handle->cur_um_node_for_usb->next->um_node_state != UM_NODE_STATE_HW_FINISHED
                && handle->cur_um_node_for_usb->next->um_node_state != UM_NODE_STATE_INITIAL)
                {
                    /* buffer overflow; shouldn`t be here.... */
                    /* reset offsets; in case of user deside to drop this packet */
                    handle->cur_um_node_for_usb->um_node_offset -= pkt_size;
                    handle->um_abs_offset -= pkt_size;
                    return result;
                }
                handle->cur_um_node_for_usb->next->um_node_offset = handle->cur_um_node_for_usb->um_node_offset % handle->um_buffer_size_in_one_node;
                handle->cur_um_node_for_usb->um_node_offset = 0;
                handle->cur_um_node_for_usb->um_node_state = UM_NODE_STATE_USB_FINISHED;
                handle->cur_um_node_for_usb = handle->cur_um_node_for_usb->next;
                handle->cur_um_node_for_usb->um_node_state = UM_NODE_STATE_UNDER_USB;
            }

            if(handle->um_abs_offset > handle->total_buffer_size)
            {
                memcpy(handle->cur_um_node_for_usb->um_buf, handle->congestion_avoidance_bucket, handle->um_abs_offset - handle->total_buffer_size);
            }

            handle->um_abs_offset %= handle->total_buffer_size;

            result = handle->cur_um_node_for_usb->um_buf + handle->cur_um_node_for_usb->um_node_offset;

            if(handle->um_buffer_state != UM_BUFFER_STATE_PLAY)
            {
                handle->um_buffer_flags &= (~UM_BUFFER_FLAG_CONGESTION_AVIODANCE);
                break;
            }

            cw = get_congestion_window(handle->cur_um_node_for_usb->next);

            if(GET_CONGESTION_AVOIDANCE_FLAG(handle->um_buffer_flags))
            {
                if((cw == CW_LOWER_BOUND))
                {
                    TOGGLE_CONGESTION_AVOIDANCE_FLAG(handle->um_buffer_flags);
                    test_flag_work = 0;
                }
            }
            else
            {
                if(cw == CW_UPPER_BOUND)
                {
                    TOGGLE_CONGESTION_AVOIDANCE_FLAG(handle->um_buffer_flags);
                    test_flag_work = 1;
                }
            }
        break; /* UM_BUFFER_CONFIG_CA_FEEDBACK */
        default:
            /* failed args validation during buffer initialisation */
            /* should not be here.... */
            UM_ASSERT(0, NULL);

    }

    if(handle->um_buffer_state != UM_BUFFER_STATE_PLAY)
    {
        if(handle->um_abs_offset >= (handle->total_buffer_size >> 1))
        {
            handle->start_um_node->um_node_state = UM_NODE_STATE_UNDER_HW;
            if(handle->um_buffer_state == UM_BUFFER_STATE_INIT)
            {
                handle->um_play((uint32_t)handle->start_um_node->um_buf, (handle->um_usb_frame_in_node * handle->um_number_of_nodes * handle->um_usb_packet_size) >> 1);
            }
            else /* UM_BUFFER_STATE_READY */
            {
                handle->um_pause_resume(1, (uint32_t)handle->start_um_node->um_buf, (handle->um_usb_frame_in_node * handle->um_number_of_nodes * handle->um_usb_packet_size) >> 1);
            }
            handle->um_buffer_state = UM_BUFFER_STATE_PLAY;
        }
    }
    return result;
}

uint8_t *um_handle_dequeue(struct um_buffer_handle *handle, uint16_t pkt_size)
{
    uint8_t *result = NULL;

    if(handle->um_buffer_state != UM_BUFFER_STATE_PLAY)
    {
        struct um_node *threshold = handle->start_um_node->next->next;

        switch (threshold->um_node_state)
        {
        case UM_NODE_STATE_INITIAL:
            threshold->um_node_state = UM_NODE_STATE_USB_FINISHED;
            handle->um_play((uint32_t)handle->start_um_node->um_buf, (handle->um_number_of_nodes * handle->um_usb_frame_in_node * handle->um_usb_packet_size) >> 1);
            return threshold->next->um_buf;

        case UM_NODE_STATE_UNDER_HW:
            handle->um_buffer_state = UM_BUFFER_STATE_PLAY;
            handle->cur_um_node_for_usb->um_node_state = UM_NODE_STATE_UNDER_USB;
            handle->cur_um_node_for_usb->um_node_offset += pkt_size;
            return handle->cur_um_node_for_usb->um_buf;

        case UM_NODE_STATE_USB_FINISHED:
            return threshold->next->um_buf;

        case UM_NODE_STATE_HW_FINISHED:
        case UM_NODE_STATE_UNDER_USB:
        default:
            return result;
        }
    }

    if(handle->cur_um_node_for_usb->um_node_state != UM_NODE_STATE_UNDER_USB)
    {
        /* state machine error */
        return result;
    }

    if(handle->cur_um_node_for_usb->um_node_offset >= (handle->um_usb_frame_in_node * handle->um_usb_packet_size))
    {
        if(handle->cur_um_node_for_usb->next->um_node_state != UM_NODE_STATE_HW_FINISHED)
        {
            /* buffer underflow */
            return result;
        }

        handle->cur_um_node_for_usb->next->um_node_offset = handle->cur_um_node_for_usb->um_node_offset % (handle->um_usb_frame_in_node * handle->um_usb_packet_size);
        handle->cur_um_node_for_usb->um_node_offset = 0;
        handle->cur_um_node_for_usb->um_node_state = UM_NODE_STATE_USB_FINISHED;

        handle->cur_um_node_for_usb = handle->cur_um_node_for_usb->next;
        handle->cur_um_node_for_usb->um_node_state = UM_NODE_STATE_UNDER_USB;
    }

    result = handle->cur_um_node_for_usb->um_buf + handle->cur_um_node_for_usb->um_node_offset;

    handle->cur_um_node_for_usb->um_node_offset += pkt_size;

    return result;
}

void um_handle_pause(struct um_buffer_handle *handle)
{
    handle->um_pause_resume(0, (uint32_t)handle->start_um_node->um_buf, 0);

    reset_nodes_states_to_default(handle);
}

#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
void audio_dma_complete_cb(struct um_buffer_handle *handle)
{
    /* State machine verification */
    UM_ASSERT(handle->cur_um_node_for_hw->um_node_state == UM_NODE_STATE_UNDER_HW || handle->cur_um_node_for_hw->um_node_state == UM_NODE_STATE_INITIAL, );

    handle->cur_um_node_for_hw->um_node_state = UM_NODE_STATE_HW_FINISHED;
    handle->cur_um_node_for_hw = handle->cur_um_node_for_hw->next;

    switch (handle->cur_um_node_for_hw->um_node_state)
    {
    case UM_NODE_STATE_UNDER_USB:
        /* This state may occure, if new buffer node is under 
        USB packet filling, but not finish and no more packet
        is coming from USB endpoint. */

        /* Verify. that cur_um_node_for_hw equal to cur_um_node_for_usb */
        UM_ASSERT(handle->cur_um_node_for_hw == handle->cur_um_node_for_usb, );

        /* keep handling it as for UM_NODE_STATE_HW_FINISHED state */
    case UM_NODE_STATE_HW_FINISHED:
        um_handle_pause(handle);
        test_flag_work = 0;
        break;
    case UM_NODE_STATE_INITIAL:
    case UM_NODE_STATE_USB_FINISHED:
        handle->cur_um_node_for_hw->um_node_state = UM_NODE_STATE_UNDER_HW;
        break;
    case UM_NODE_STATE_UNDER_HW:
    default:
        /* State machine error */
        UM_ASSERT(0, );
        break;
    }
}

void free_um_buffer_handle(struct um_buffer_handle *handle)
{
    uint32_t timeout = 10000;

    while (handle->um_buffer_state == UM_BUFFER_STATE_PLAY && timeout--);

    free(handle->start_um_node->um_buf);
    _free_um_nodes(handle->start_um_node->next, handle->start_um_node);
    free(handle);
}