#include "audio_buffer.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define UM_BUFFER_LISTENER_COUNT            4

struct um_buffer_listener
{
    uint32_t id;

    void (*listener_handle)(void *args);
    struct um_buffer_listener *next;
};

static struct um_buffer_listener _listener_pool[UM_LISTENER_TYPE_COUNT][UM_BUFFER_LISTENER_COUNT] =
{
    /* UM_LISTENER_TYPE_CA */
    {
        {.id = 0, .listener_handle = NULL, .next = NULL},
        {.id = 1, .listener_handle = NULL, .next = NULL},
        {.id = 2, .listener_handle = NULL, .next = NULL},
        {.id = 3, .listener_handle = NULL, .next = NULL}
    }
};

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

static struct um_buffer_listener* _get_last_listener(struct um_buffer_listener* current)
{
    if(current->next != NULL)
        return _get_last_listener(current->next);
    else
        return current;
}

static uint32_t allocate_listeners_from_pool(enum um_buffer_listener_type type)
{
    uint32_t i = 0;

    for(i = 0; i < UM_BUFFER_LISTENER_COUNT; i++)
    {
        if(_listener_pool[type][i].listener_handle == NULL)
        {
            return i;
        }
    }
    return UM_LISTENERS_WRONG_ID;
}

static uint8_t get_congestion_window(struct um_node *um_node_write)
{
    return um_node_write->um_node_state != UM_NODE_STATE_HW_FINISHED ? 1 : 1 + get_congestion_window(um_node_write->next);
}

static uint32_t get_free_buffer_persentage(struct um_buffer_handle *handle)
{
    uint8_t i;
    struct um_node *curr;
    uint32_t result = 0;

    for(i = 0, curr = handle->cur_um_node_for_usb;
        i < handle->um_number_of_nodes;
        i++, curr = curr->next)
    {
        if(curr->um_node_state == UM_NODE_STATE_UNDER_HW)
        {
            return (result * 100) / handle->um_number_of_nodes;
        }

        result++;
    }

    return (result * 100) / handle->um_number_of_nodes;
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
                    uint32_t usb_max_packet_size,
                    uint32_t usb_frame_in_um_node_count,
                    uint32_t um_node_count,
                    uint8_t config,
                    um_play_fnc play, um_pause_resume_fnc pause_resume )
{
    uint8_t i = 0;

    UM_RET_IF_FALSE(handle != NULL, UM_EARGS);

    UM_RET_IF_FALSE(
        GET_CONFIG_CA_ALGORITM(config) == UM_BUFFER_CONFIG_CA_NONE ||
        GET_CONFIG_CA_ALGORITM(config) == UM_BUFFER_CONFIG_CA_DROP_HALF_PKT ||
        GET_CONFIG_CA_ALGORITM(config) == UM_BUFFER_CONFIG_CA_FEEDBACK, UM_EARGS);

    handle->um_usb_max_packet_size = usb_max_packet_size;

    /* Consider maximum possible usb packet size, */
    /* that current instance of the um_buffer can handle, as current */
    handle->um_usb_packet_size = usb_max_packet_size;
    handle->um_usb_frame_in_node = usb_frame_in_um_node_count;
    handle->um_number_of_nodes = um_node_count;

    // allocate memory for whole internal buffer; store pointer to it here temporary
    if(GET_CONFIG_CA_ALGORITM(config) != UM_BUFFER_CONFIG_CA_NONE)
    {
        handle->congestion_avoidance_bucket = (uint8_t *)malloc((usb_max_packet_size * usb_frame_in_um_node_count * um_node_count) + usb_max_packet_size);
    }
    else
    {
        handle->congestion_avoidance_bucket = (uint8_t *)malloc((usb_max_packet_size * usb_frame_in_um_node_count * um_node_count));
    }

    UM_RET_IF_FALSE(handle->congestion_avoidance_bucket != NULL, UM_ENOMEM);

    handle->cur_um_node_for_hw = *_alloc_um_nodes(handle, &(handle->start_um_node));
    UM_RET_IF_FALSE(handle->cur_um_node_for_hw != NULL, UM_ENOMEM);

    handle->cur_um_node_for_hw->next = handle->start_um_node;
    handle->cur_um_node_for_hw = handle->start_um_node;
    handle->cur_um_node_for_usb = handle->start_um_node;

    /* main buffer pointer was copied inside start_um_node struct; allocate new memory for CA algorithm (if it is nessesary) */
    if(GET_CONFIG_CA_ALGORITM(config) != UM_BUFFER_CONFIG_CA_NONE)
    {
        handle->congestion_avoidance_bucket += (usb_max_packet_size * usb_frame_in_um_node_count * um_node_count);
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

    for(i = 0; i < UM_LISTENER_TYPE_COUNT; i++)
        handle->listeners[i] = NULL;

    return UM_EOK;
}

uint8_t *um_handle_enqueue(struct um_buffer_handle *handle, uint16_t pkt_size)
{
    uint8_t cw;
    uint8_t *result = NULL;
    
    struct um_buffer_listener *ca_listener = handle->listeners[UM_LISTENER_TYPE_CA];
    uint32_t free_buffer_size = 0;

    switch(GET_CONFIG_CA_ALGORITM(handle->um_buffer_config))
    {
        case UM_BUFFER_CONFIG_CA_NONE:
            if(handle->cur_um_node_for_usb->um_node_offset == 0)
            {
                /* Check for buffer overflow */
                UM_VERIFY((handle->cur_um_node_for_usb->um_node_state == UM_NODE_STATE_HW_FINISHED) || (handle->cur_um_node_for_usb->um_node_state == UM_NODE_STATE_INITIAL));

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
                UM_VERIFY((handle->cur_um_node_for_usb->um_node_state == UM_NODE_STATE_HW_FINISHED) || (handle->cur_um_node_for_usb->um_node_state == UM_NODE_STATE_INITIAL));
    
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
                    UM_RET_IF_FALSE(0, result);
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

        break; /* UM_BUFFER_CONFIG_CA_FEEDBACK */
        default:
            /* failed args validation during buffer initialisation */
            /* should not be here.... */
            UM_VERIFY(0);

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
        else
        {
            /* no need to notify listeners until buffer is not in PLAY state */
            return result;
        }
    }

    while(ca_listener != NULL)
    {
        free_buffer_size = free_buffer_size == 0 ? get_free_buffer_persentage(handle) : free_buffer_size;
        ca_listener->listener_handle((void *)&free_buffer_size);
        ca_listener = ca_listener->next;
    }
   
    return result;
}

uint8_t *um_handle_dequeue(struct um_buffer_handle *handle, uint16_t pkt_size)
{
    uint8_t *result = NULL;
    struct um_buffer_listener *ca_listener = handle->listeners[UM_LISTENER_TYPE_CA];
    uint32_t free_buffer_size = 0;

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
            /* state machine error; shouldn`t be here... */
            UM_VERIFY(0);
        }
    }

    UM_VERIFY(handle->cur_um_node_for_usb->um_node_state == UM_NODE_STATE_UNDER_USB);

    if(handle->cur_um_node_for_usb->um_node_offset >= (handle->um_usb_frame_in_node * handle->um_usb_packet_size))
    {
        /* check for buffer underflow */
        UM_RET_IF_FALSE(handle->cur_um_node_for_usb->next->um_node_state == UM_NODE_STATE_HW_FINISHED, result);

        handle->cur_um_node_for_usb->next->um_node_offset = handle->cur_um_node_for_usb->um_node_offset % (handle->um_usb_frame_in_node * handle->um_usb_packet_size);
        handle->cur_um_node_for_usb->um_node_offset = 0;
        handle->cur_um_node_for_usb->um_node_state = UM_NODE_STATE_USB_FINISHED;

        handle->cur_um_node_for_usb = handle->cur_um_node_for_usb->next;
        handle->cur_um_node_for_usb->um_node_state = UM_NODE_STATE_UNDER_USB;
    }

    result = handle->cur_um_node_for_usb->um_buf + handle->cur_um_node_for_usb->um_node_offset;

    handle->cur_um_node_for_usb->um_node_offset += pkt_size;

    while(ca_listener != NULL)
    {
        free_buffer_size = free_buffer_size == 0 ? get_free_buffer_persentage(handle) : free_buffer_size;
        ca_listener->listener_handle((void *)&free_buffer_size);
        ca_listener = ca_listener->next;
    }

    return result;
}

int um_handle_set_driver(struct um_buffer_handle *handle, uint32_t usb_packet_size,
                    um_play_fnc play, um_pause_resume_fnc pause_resume)
{
    UM_RET_IF_FALSE(usb_packet_size <= handle->um_usb_max_packet_size, UM_ENOMEM);

    if(handle->um_buffer_state == UM_BUFFER_STATE_PLAY)
    {
        um_handle_pause(handle);
    }

    handle->um_usb_packet_size = usb_packet_size;
    handle->um_play = play;
    handle->um_pause_resume = pause_resume;

    return UM_EOK;
}

void um_handle_pause(struct um_buffer_handle *handle)
{
    handle->um_pause_resume(0, (uint32_t)handle->start_um_node->um_buf, 0);

    reset_nodes_states_to_default(handle);
}

uint32_t um_handle_register_listener(struct um_buffer_handle *handle, enum um_buffer_listener_type type, listener_callback clbk)
{
    uint32_t result;

    UM_RET_IF_FALSE(handle != NULL, UM_LISTENERS_WRONG_ID);
    UM_RET_IF_FALSE(type < UM_LISTENER_TYPE_COUNT, UM_LISTENERS_WRONG_ID);
    UM_RET_IF_FALSE(clbk != NULL, UM_LISTENERS_WRONG_ID);

    result = allocate_listeners_from_pool(type);

    UM_RET_IF_FALSE(result != UM_LISTENERS_WRONG_ID, UM_LISTENERS_WRONG_ID);

    if(handle->listeners[type] == NULL)
    {
        handle->listeners[type] = &(_listener_pool[type][result]);
        handle->listeners[type]->listener_handle = clbk;
    }
    else
    {
        struct um_buffer_listener *last = _get_last_listener(handle->listeners[type]);
        last->next = &(_listener_pool[type][result]);
        last->next->listener_handle = clbk;
    }

    return result;
}

void um_handle_unregister_listener(struct um_buffer_handle *handle, enum um_buffer_listener_type type, uint32_t listener_id)
{
    struct um_buffer_listener *curr;

    UM_RET_IF_FALSE(handle != NULL,);
    UM_RET_IF_FALSE(handle->listeners[type] != NULL,);
    UM_RET_IF_FALSE(type < UM_LISTENER_TYPE_COUNT,);
    UM_RET_IF_FALSE(listener_id < UM_BUFFER_LISTENER_COUNT,);

    curr = handle->listeners[type];

    if(curr->id == listener_id)
    {
        curr->listener_handle = NULL;
        handle->listeners[type] = curr->next;
        curr->next = NULL;
    }

    while(curr->next != NULL)
    {
        if(curr->next->id == listener_id)
        {
            curr->next->listener_handle = NULL;
            curr->next = curr->next->next;
            curr->next->next = NULL;
            return;
        }
    }
}

#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
void audio_dma_complete_cb(struct um_buffer_handle *handle)
{
    /* State machine verification */
    UM_VERIFY(handle->cur_um_node_for_hw->um_node_state == UM_NODE_STATE_UNDER_HW || handle->cur_um_node_for_hw->um_node_state == UM_NODE_STATE_INITIAL);

    handle->cur_um_node_for_hw->um_node_state = UM_NODE_STATE_HW_FINISHED;
    handle->cur_um_node_for_hw = handle->cur_um_node_for_hw->next;

    switch (handle->cur_um_node_for_hw->um_node_state)
    {
    case UM_NODE_STATE_UNDER_USB:
        /* This state may occure, if new buffer node is under 
        USB packet filling, but not finish and no more packet
        is coming from USB endpoint. */

        /* Verify. that cur_um_node_for_hw equal to cur_um_node_for_usb */
        UM_VERIFY(handle->cur_um_node_for_hw == handle->cur_um_node_for_usb);

        /* keep handling it as for UM_NODE_STATE_HW_FINISHED state */
    case UM_NODE_STATE_HW_FINISHED:
        um_handle_pause(handle);
        break;
    case UM_NODE_STATE_INITIAL:
    case UM_NODE_STATE_USB_FINISHED:
        handle->cur_um_node_for_hw->um_node_state = UM_NODE_STATE_UNDER_HW;
        break;
    case UM_NODE_STATE_UNDER_HW:
    default:
        /* State machine error */
        UM_VERIFY(0);
        break;
    }
}

void free_um_buffer_handle(struct um_buffer_handle *handle)
{
    uint32_t timeout = 10000;

    while (handle->um_buffer_state == UM_BUFFER_STATE_PLAY && timeout--);

    free(handle->start_um_node->um_buf);
    _free_um_nodes(handle->start_um_node->next, handle->start_um_node);
}