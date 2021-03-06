/*
 * Copyright 2014 - 2017 Real Logic Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#if defined(__linux__)
#define _BSD_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include "concurrent/aeron_term_scanner.h"
#include "util/aeron_netutil.h"
#include "util/aeron_error.h"
#include "aeron_network_publication.h"
#include "aeron_alloc.h"
#include "media/aeron_send_channel_endpoint.h"
#include "aeron_driver_conductor.h"

#if !defined(HAVE_RECVMMSG)
struct mmsghdr
{
    struct msghdr msg_hdr;
    unsigned int msg_len;
};
#endif

int aeron_network_publication_create(
    aeron_network_publication_t **publication,
    aeron_send_channel_endpoint_t *endpoint,
    aeron_driver_context_t *context,
    int64_t registration_id,
    int32_t session_id,
    int32_t stream_id,
    int32_t initial_term_id,
    size_t mtu_length,
    aeron_position_t *pub_lmt_position,
    aeron_position_t *snd_pos_position,
    aeron_position_t *snd_lmt_position,
    aeron_flow_control_strategy_t *flow_control_strategy,
    size_t term_buffer_length,
    bool is_exclusive,
    aeron_system_counters_t *system_counters)
{
    char path[AERON_MAX_PATH];
    int path_length =
        aeron_network_publication_location(
            path,
            sizeof(path),
            context->aeron_dir,
            endpoint->conductor_fields.udp_channel->canonical_form,
            session_id,
            stream_id,
            registration_id);
    aeron_network_publication_t *_pub = NULL;
    const uint64_t usable_fs_space = context->usable_fs_space_func(context->aeron_dir);
    const uint64_t log_length = AERON_LOGBUFFER_COMPUTE_LOG_LENGTH(term_buffer_length);
    const int64_t now_ns = context->nano_clock();

    *publication = NULL;

    if (usable_fs_space < log_length)
    {
        aeron_set_err(ENOSPC, "Insufficient usable storage for new log of length=%d in %s", log_length, context->aeron_dir);
        return -1;
    }

    if (aeron_alloc((void **)&_pub, sizeof(aeron_network_publication_t)) < 0)
    {
        aeron_set_err(ENOMEM, "%s", "Could not allocate network publication");
        return -1;
    }

    _pub->log_file_name = NULL;
    if (aeron_alloc((void **)(&_pub->log_file_name), (size_t)path_length + 1) < 0)
    {
        aeron_free(_pub);
        aeron_set_err(ENOMEM, "%s", "Could not allocate network publication log_file_name");
        return -1;
    }

    if (aeron_retransmit_handler_init(
        &_pub->retransmit_handler,
        aeron_system_counter_addr(system_counters, AERON_SYSTEM_COUNTER_INVALID_PACKETS),
        AERON_RETRANSMIT_HANDLER_DEFAULT_LINGER_TIMEOUT_NS) < 0)
    {
        aeron_free(_pub->log_file_name);
        aeron_free(_pub);
        aeron_set_err(aeron_errcode(), "Could not init network publication retransmit handler: %s", aeron_errmsg());
        return -1;
    }

    if (context->map_raw_log_func(&_pub->mapped_raw_log, path, context->term_buffer_sparse_file, term_buffer_length) < 0)
    {
        aeron_free(_pub->log_file_name);
        aeron_free(_pub);
        aeron_set_err(aeron_errcode(), "error mapping network raw log %s: %s", path, aeron_errmsg());
        return -1;
    }
    _pub->map_raw_log_close_func = context->map_raw_log_close_func;

    strncpy(_pub->log_file_name, path, path_length);
    _pub->log_file_name[path_length] = '\0';
    _pub->log_file_name_length = (size_t)path_length;
    _pub->log_meta_data = (aeron_logbuffer_metadata_t *)(_pub->mapped_raw_log.log_meta_data.addr);

    _pub->log_meta_data->term_tail_counters[0] = (int64_t)initial_term_id << 32;
    _pub->log_meta_data->initialTerm_id = initial_term_id;
    _pub->log_meta_data->mtu_length = (int32_t)mtu_length;
    _pub->log_meta_data->correlation_id = registration_id;
    _pub->log_meta_data->time_of_last_status_message = 0;
    aeron_logbuffer_fill_default_header(
        _pub->mapped_raw_log.log_meta_data.addr, session_id, stream_id, initial_term_id);

    _pub->endpoint = endpoint;
    _pub->flow_control = flow_control_strategy;
    _pub->epoch_clock = context->epoch_clock;
    _pub->nano_clock = context->nano_clock;
    _pub->conductor_fields.subscribeable.array = NULL;
    _pub->conductor_fields.subscribeable.length = 0;
    _pub->conductor_fields.subscribeable.capacity = 0;
    _pub->conductor_fields.managed_resource.registration_id = registration_id;
    _pub->conductor_fields.managed_resource.clientd = _pub;
    _pub->conductor_fields.managed_resource.incref = aeron_network_publication_incref;
    _pub->conductor_fields.managed_resource.decref = aeron_network_publication_decref;
    _pub->conductor_fields.has_reached_end_of_life = false;
    _pub->conductor_fields.clean_position = 0;
    _pub->conductor_fields.status = AERON_NETWORK_PUBLICATION_STATUS_ACTIVE;
    _pub->conductor_fields.refcnt = 1;
    _pub->conductor_fields.time_of_last_activity_ns = 0;
    _pub->conductor_fields.last_snd_pos = 0;
    _pub->session_id = session_id;
    _pub->stream_id = stream_id;
    _pub->pub_lmt_position.counter_id = pub_lmt_position->counter_id;
    _pub->pub_lmt_position.value_addr = pub_lmt_position->value_addr;
    _pub->snd_pos_position.counter_id = snd_pos_position->counter_id;
    _pub->snd_pos_position.value_addr = snd_pos_position->value_addr;
    _pub->snd_lmt_position.counter_id = snd_lmt_position->counter_id;
    _pub->snd_lmt_position.value_addr = snd_lmt_position->value_addr;
    _pub->initial_term_id = initial_term_id;
    _pub->term_length_mask = (int32_t)term_buffer_length - 1;
    _pub->position_bits_to_shift = (size_t)aeron_number_of_trailing_zeroes((int32_t)term_buffer_length);
    _pub->mtu_length = mtu_length;
    _pub->term_window_length = (int64_t)aeron_network_publication_term_window_length(context, term_buffer_length);
    _pub->linger_timeout_ns = (int64_t)context->publication_linger_timeout_ns;
    _pub->time_of_last_send_or_heartbeat_ns = now_ns - AERON_NETWORK_PUBLICATION_HEARTBEAT_TIMEOUT_NS - 1;
    _pub->time_of_last_setup_ns = now_ns - AERON_NETWORK_PUBLICATION_SETUP_TIMEOUT_NS - 1;
    _pub->is_exclusive = is_exclusive;
    _pub->should_send_setup_frame = true;
    _pub->is_connected = false;
    _pub->is_complete = false;
    _pub->track_sender_limits = true;
    _pub->has_sender_released = false;

    _pub->short_sends_counter = aeron_system_counter_addr(system_counters, AERON_SYSTEM_COUNTER_SHORT_SENDS);
    _pub->heartbeats_sent_counter = aeron_system_counter_addr(system_counters, AERON_SYSTEM_COUNTER_HEARTBEATS_SENT);
    _pub->sender_flow_control_limits_counter =
        aeron_system_counter_addr(system_counters, AERON_SYSTEM_COUNTER_SENDER_FLOW_CONTROL_LIMITS);
    _pub->retransmits_sent_counter = aeron_system_counter_addr(system_counters, AERON_SYSTEM_COUNTER_RETRANSMITS_SENT);

    *publication = _pub;
    return 0;
}

void aeron_network_publication_close(aeron_counters_manager_t *counters_manager, aeron_network_publication_t *publication)
{
    if (NULL != publication)
    {
        aeron_subscribeable_t *subscribeable = &publication->conductor_fields.subscribeable;

        aeron_counters_manager_free(counters_manager, (int32_t)publication->pub_lmt_position.counter_id);
        aeron_counters_manager_free(counters_manager, (int32_t)publication->snd_pos_position.counter_id);
        aeron_counters_manager_free(counters_manager, (int32_t)publication->snd_lmt_position.counter_id);

        for (size_t i = 0, length = subscribeable->length; i < length; i++)
        {
            aeron_counters_manager_free(counters_manager, (int32_t)subscribeable->array[i].counter_id);
        }

        aeron_free(subscribeable->array);

        aeron_retransmit_handler_close(&publication->retransmit_handler);
        publication->map_raw_log_close_func(&publication->mapped_raw_log);
        publication->flow_control->fini(publication->flow_control);
        aeron_free(publication->log_file_name);
    }

    aeron_free(publication);
}

int aeron_network_publication_setup_message_check(
    aeron_network_publication_t *publication, int64_t now_ns, int32_t active_term_id, int32_t term_offset)
{
    int result = 0;

    if (now_ns > (publication->time_of_last_setup_ns + AERON_NETWORK_PUBLICATION_SETUP_TIMEOUT_NS))
    {
        uint8_t setup_buffer[sizeof(aeron_setup_header_t)];
        aeron_setup_header_t *setup_header = (aeron_setup_header_t *)setup_buffer;
        struct iovec iov[1];
        struct msghdr msghdr;

        setup_header->frame_header.frame_length = sizeof(aeron_setup_header_t);
        setup_header->frame_header.version = AERON_FRAME_HEADER_VERSION;
        setup_header->frame_header.flags = 0;
        setup_header->frame_header.type = AERON_HDR_TYPE_SETUP;
        setup_header->term_offset = term_offset;
        setup_header->session_id = publication->session_id;
        setup_header->stream_id = publication->stream_id;
        setup_header->initial_term_id = publication->initial_term_id;
        setup_header->active_term_id = active_term_id;
        setup_header->term_length = publication->term_length_mask + 1;
        setup_header->mtu = (int32_t)publication->mtu_length;
        setup_header->ttl = publication->endpoint->conductor_fields.udp_channel->multicast_ttl;

        iov[0].iov_base = setup_buffer;
        iov[0].iov_len = sizeof(aeron_setup_header_t);
        msghdr.msg_iov = iov;
        msghdr.msg_iovlen = 1;
        msghdr.msg_flags = 0;
        msghdr.msg_control = NULL;

        if ((result = aeron_send_channel_sendmsg(publication->endpoint, &msghdr)) != (int)iov[0].iov_len)
        {
            if (result >= 0)
            {
                aeron_counter_increment(publication->short_sends_counter, 1);
            }
        }

        publication->time_of_last_setup_ns = now_ns;
        publication->time_of_last_send_or_heartbeat_ns = now_ns;

        bool is_connected;
        AERON_GET_VOLATILE(is_connected, publication->is_connected);
        if (is_connected)
        {
            publication->should_send_setup_frame = false;
        }
    }

    return result;
}

int aeron_network_publication_heartbeat_message_check(
    aeron_network_publication_t *publication, int64_t now_ns, int32_t active_term_id, int32_t term_offset)
{
    int bytes_sent = 0;

    if (now_ns > (publication->time_of_last_send_or_heartbeat_ns + AERON_NETWORK_PUBLICATION_HEARTBEAT_TIMEOUT_NS))
    {
        uint8_t heartbeat_buffer[sizeof(aeron_data_header_t)];
        aeron_data_header_t *data_header = (aeron_data_header_t *)heartbeat_buffer;
        struct iovec iov[1];
        struct msghdr msghdr;

        data_header->frame_header.frame_length = 0;
        data_header->frame_header.version = AERON_FRAME_HEADER_VERSION;
        data_header->frame_header.flags = AERON_DATA_HEADER_BEGIN_FLAG | AERON_DATA_HEADER_END_FLAG;
        data_header->frame_header.type = AERON_HDR_TYPE_DATA;
        data_header->term_offset = term_offset;
        data_header->session_id = publication->session_id;
        data_header->stream_id = publication->stream_id;
        data_header->term_id = active_term_id;
        data_header->reserved_value = 0l;

        bool is_complete;
        AERON_GET_VOLATILE(is_complete, publication->is_complete);
        if (is_complete)
        {
            data_header->frame_header.flags =
                AERON_DATA_HEADER_BEGIN_FLAG | AERON_DATA_HEADER_END_FLAG | AERON_DATA_HEADER_EOS_FLAG;
        }

        iov[0].iov_base = heartbeat_buffer;
        iov[0].iov_len = sizeof(aeron_data_header_t);
        msghdr.msg_iov = iov;
        msghdr.msg_iovlen = 1;
        msghdr.msg_flags = 0;
        msghdr.msg_control = NULL;

        if ((bytes_sent = aeron_send_channel_sendmsg(publication->endpoint, &msghdr)) != (int)iov[0].iov_len)
        {
            if (bytes_sent >= 0)
            {
                aeron_counter_increment(publication->short_sends_counter, 1);
            }
        }

        aeron_counter_ordered_increment(publication->heartbeats_sent_counter, 1);
        publication->time_of_last_send_or_heartbeat_ns = now_ns;
    }

    return bytes_sent;
}

int aeron_network_publication_send_data(
    aeron_network_publication_t *publication, int64_t now_ns, int64_t snd_pos, int32_t term_offset)
{
    const size_t term_length = (size_t)publication->term_length_mask + 1;
    int result = 0, vlen = 0, bytes_sent = 0;
    int32_t available_window = (int32_t)(aeron_counter_get(publication->snd_lmt_position.value_addr) - snd_pos);
    int64_t highest_pos = snd_pos;
    struct iovec iov[AERON_NETWORK_PUBLICATION_MAX_MESSAGES_PER_SEND];
    struct mmsghdr mmsghdr[AERON_NETWORK_PUBLICATION_MAX_MESSAGES_PER_SEND];

    for (size_t i = 0; i < AERON_NETWORK_PUBLICATION_MAX_MESSAGES_PER_SEND && available_window > 0; i++)
    {
        size_t scan_limit =
            (size_t)available_window < publication->mtu_length ? (size_t)available_window : publication->mtu_length;
        size_t active_index = aeron_logbuffer_index_by_position(snd_pos, publication->position_bits_to_shift);
        size_t padding = 0;

        uint8_t *ptr = publication->mapped_raw_log.term_buffers[active_index].addr + term_offset;
        const size_t term_length_left = term_length - (size_t)term_offset;

        const size_t available = aeron_term_scanner_scan_for_availability(ptr, term_length_left, scan_limit, &padding);

        if (available > 0)
        {
            iov[i].iov_base = ptr;
            iov[i].iov_len = available;
            mmsghdr[i].msg_hdr.msg_iov = &iov[i];
            mmsghdr[i].msg_hdr.msg_iovlen = 1;
            mmsghdr[i].msg_hdr.msg_flags = 0;
            mmsghdr[i].msg_len = 0;
            mmsghdr[i].msg_hdr.msg_control = NULL;
            vlen++;

            bytes_sent += available;
            available_window -= available + padding;
            term_offset += available + padding;
            highest_pos += available + padding;
        }

        if (available == 0 || term_length == (size_t)term_offset)
        {
            break;
        }
    }

    if (vlen > 0)
    {
        if ((result = aeron_send_channel_sendmmsg(publication->endpoint, mmsghdr, (size_t)vlen)) != vlen)
        {
            if (result >= 0)
            {
                aeron_counter_increment(publication->short_sends_counter, 1);
            }
        }

        publication->time_of_last_send_or_heartbeat_ns = now_ns;
        publication->track_sender_limits = true;
        aeron_counter_set_ordered(publication->snd_pos_position.value_addr, highest_pos);
    }

    if (available_window <= 0)
    {
        aeron_counter_ordered_increment(publication->sender_flow_control_limits_counter, 1);
        publication->track_sender_limits = false;
    }

    return result < 0 ? result : bytes_sent;
}

int aeron_network_publication_send(aeron_network_publication_t *publication, int64_t now_ns)
{
    int64_t snd_pos = aeron_counter_get(publication->snd_pos_position.value_addr);
    int32_t active_term_id =
        aeron_logbuffer_compute_term_id_from_position(
            snd_pos, publication->position_bits_to_shift, publication->initial_term_id);
    int32_t term_offset = (int32_t)snd_pos & publication->term_length_mask;

    if (publication->should_send_setup_frame)
    {
        if (aeron_network_publication_setup_message_check(publication, now_ns, active_term_id, term_offset) < 0)
        {
            return -1;
        }
    }

    int bytes_sent = aeron_network_publication_send_data(publication, now_ns, snd_pos, term_offset);
    if (bytes_sent < 0)
    {
        return -1;
    }

    if (0 == bytes_sent)
    {
        bytes_sent = aeron_network_publication_heartbeat_message_check(publication, now_ns, active_term_id, term_offset);
        if (bytes_sent < 0)
        {
            return -1;
        }

        int64_t snd_lmt = aeron_counter_get(publication->snd_lmt_position.value_addr);
        int64_t flow_control_position =
            publication->flow_control->on_idle(publication->flow_control->state, now_ns, snd_lmt);
        aeron_counter_set_ordered(publication->snd_lmt_position.value_addr, flow_control_position);
    }

    aeron_retransmit_handler_process_timeouts(&publication->retransmit_handler, now_ns);

    return bytes_sent;
}

int aeron_network_publication_resend(void *clientd, int32_t term_id, int32_t term_offset, size_t length)
{
    aeron_network_publication_t *publication = (aeron_network_publication_t *)clientd;
    const int64_t sender_position = aeron_counter_get(publication->snd_pos_position.value_addr);
    const int64_t resend_position =
        aeron_logbuffer_compute_position(
            term_id, term_offset, publication->position_bits_to_shift, publication->initial_term_id);
    const size_t term_length = (size_t)(publication->term_length_mask + 1);
    int result = 0;

    if (resend_position < sender_position && resend_position >= (sender_position - (int32_t)term_length))
    {
        const size_t index = aeron_logbuffer_index_by_position(resend_position, publication->position_bits_to_shift);

        size_t remaining_bytes = length;
        int32_t bytes_sent = 0;
        int32_t offset = term_offset;

        do
        {
            offset += bytes_sent;

            uint8_t *ptr = publication->mapped_raw_log.term_buffers[index].addr + offset;
            const size_t term_length_left = term_length - (size_t)offset;
            size_t padding = 0;

            size_t available =
                aeron_term_scanner_scan_for_availability(ptr, term_length_left, publication->mtu_length, &padding);
            if (available <= 0)
            {
                break;
            }

            struct iovec iov[1];
            struct msghdr msghdr;

            iov[0].iov_base = ptr;
            iov[0].iov_len = available;
            msghdr.msg_iov = iov;
            msghdr.msg_iovlen = 1;
            msghdr.msg_control = NULL;
            msghdr.msg_flags = 0;

            int sendmsg_result;
            if ((sendmsg_result = aeron_send_channel_sendmsg(publication->endpoint, &msghdr)) != (int)iov[0].iov_len)
            {
                if (sendmsg_result >= 0)
                {
                    aeron_counter_increment(publication->short_sends_counter, 1);
                    break;
                }
                else
                {
                    result = -1;
                    break;
                }
            }

            bytes_sent = (int32_t)(available + padding);
            remaining_bytes -= bytes_sent;
        }
        while (remaining_bytes > 0);

        aeron_counter_ordered_increment(publication->retransmits_sent_counter, 1);
    }

    return result;
}

void aeron_network_publication_on_nak(
    aeron_network_publication_t *publication, int32_t term_id, int32_t term_offset, int32_t length)
{
    aeron_retransmit_handler_on_nak(
        &publication->retransmit_handler,
        term_id,
        term_offset,
        (size_t)length,
        (size_t)(publication->term_length_mask + 1),
        publication->nano_clock(),
        aeron_network_publication_resend,
        publication);
}

void aeron_network_publication_on_status_message(
    aeron_network_publication_t *publication, const uint8_t *buffer, size_t length, struct sockaddr_storage *addr)
{
    AERON_PUT_ORDERED(publication->log_meta_data->time_of_last_status_message, publication->epoch_clock());

    bool is_connected;
    AERON_GET_VOLATILE(is_connected, publication->is_connected);
    if (!is_connected)
    {
        AERON_PUT_ORDERED(publication->is_connected, true);
    }

    aeron_counter_set_ordered(
        publication->snd_lmt_position.value_addr,
        publication->flow_control->on_status_message(
            publication->flow_control->state,
            buffer,
            length,
            addr,
            *publication->snd_lmt_position.value_addr,
            publication->initial_term_id,
            publication->position_bits_to_shift,
            publication->nano_clock()));
}

void aeron_network_publication_on_rttm(
    aeron_network_publication_t *publication, const uint8_t *buffer, size_t length, struct sockaddr_storage *addr)
{
    aeron_rttm_header_t *rttm_in_header = (aeron_rttm_header_t *)buffer;

    if (rttm_in_header->frame_header.flags & AERON_RTTM_HEADER_REPLY_FLAG)
    {
        uint8_t rttm_reply_buffer[sizeof(aeron_rttm_header_t)];
        aeron_rttm_header_t *rttm_out_header = (aeron_rttm_header_t *)rttm_reply_buffer;
        struct iovec iov[1];
        struct msghdr msghdr;
        int result;

        rttm_out_header->frame_header.frame_length = sizeof(aeron_rttm_header_t);
        rttm_out_header->frame_header.version = AERON_FRAME_HEADER_VERSION;
        rttm_out_header->frame_header.flags = 0;
        rttm_out_header->frame_header.type = AERON_HDR_TYPE_RTTM;
        rttm_out_header->session_id = publication->session_id;
        rttm_out_header->stream_id = publication->stream_id;
        rttm_out_header->echo_timestamp = rttm_in_header->echo_timestamp;
        rttm_out_header->reception_delta = 0;
        rttm_out_header->receiver_id = rttm_in_header->receiver_id;

        iov[0].iov_base = rttm_reply_buffer;
        iov[0].iov_len = sizeof(aeron_rttm_header_t);
        msghdr.msg_iov = iov;
        msghdr.msg_iovlen = 1;
        msghdr.msg_flags = 0;
        msghdr.msg_control = NULL;

        if ((result = aeron_send_channel_sendmsg(publication->endpoint, &msghdr)) != (int)iov[0].iov_len)
        {
            if (result >= 0)
            {
                aeron_counter_increment(publication->short_sends_counter, 1);
            }
        }
    }
}

void aeron_network_publication_clean_buffer(aeron_network_publication_t *publication, int64_t pub_lmt)
{
    const int64_t clean_position = publication->conductor_fields.clean_position;
    const int64_t dirty_range = pub_lmt  - clean_position;
    const int32_t buffer_capacity = publication->term_length_mask + 1;
    const int32_t reserved_range = buffer_capacity * 2;

    if (dirty_range > reserved_range)
    {
        size_t dirty_index = aeron_logbuffer_index_by_position(clean_position, publication->position_bits_to_shift);
        int32_t term_offset = (int32_t)(clean_position & publication->term_length_mask);
        int32_t bytes_left_in_term = buffer_capacity - term_offset;
        int32_t bytes_for_cleaning = (int32_t)(dirty_range - reserved_range);
        int32_t length = bytes_for_cleaning < bytes_left_in_term ? bytes_for_cleaning : bytes_left_in_term;

        memset((uint8_t *)publication->mapped_raw_log.term_buffers[dirty_index].addr + term_offset, 0, length);
        publication->conductor_fields.clean_position = clean_position + length;
    }
}

int aeron_network_publication_update_pub_lmt(aeron_network_publication_t *publication)
{
    int work_count = 0;

    int64_t snd_pos;
    AERON_GET_VOLATILE(snd_pos, *publication->snd_pos_position.value_addr);
    bool is_connected;
    AERON_GET_VOLATILE(is_connected, publication->is_connected);
    if (is_connected)
    {
        int64_t min_consumer_position = snd_pos;
        if (publication->conductor_fields.subscribeable.length > 0)
        {
            for (size_t i = 0, length = publication->conductor_fields.subscribeable.length; i < length; i++)
            {
                int64_t position =
                    aeron_counter_get_volatile(publication->conductor_fields.subscribeable.array[i].value_addr);

                min_consumer_position = (position < min_consumer_position) ? (position) : (min_consumer_position);
            }
        }

        const int64_t proposed_pub_lmt = min_consumer_position + publication->term_window_length;
        if (aeron_counter_propose_max_ordered(publication->pub_lmt_position.value_addr, proposed_pub_lmt))
        {
            aeron_network_publication_clean_buffer(publication, proposed_pub_lmt);
            work_count = 1;
        }
    }
    else if (*publication->pub_lmt_position.value_addr > snd_pos)
    {
        aeron_counter_set_ordered(publication->pub_lmt_position.value_addr, snd_pos);
    }

    return work_count;
}

void aeron_network_publication_check_for_blocked_publisher(
    aeron_network_publication_t *publication, int64_t now_ns, int64_t snd_pos)
{
    /* TODO: */
}

void aeron_network_publication_incref(void *clientd)
{
    aeron_network_publication_t *publication = (aeron_network_publication_t *)clientd;

    publication->conductor_fields.refcnt++;
}

void aeron_network_publication_decref(void *clientd)
{
    aeron_network_publication_t *publication = (aeron_network_publication_t *)clientd;
    int32_t ref_count = --publication->conductor_fields.refcnt;

    if (0 == ref_count)
    {
        publication->conductor_fields.status = AERON_NETWORK_PUBLICATION_STATUS_DRAINING;
        publication->conductor_fields.managed_resource.time_of_last_status_change = publication->nano_clock();
    }
}

bool aeron_network_publication_spies_not_behind_sender(
    aeron_network_publication_t *publication, aeron_driver_conductor_t *conductor, int64_t snd_pos)
{
    if (publication->conductor_fields.subscribeable.length > 0)
    {
        for (size_t i = 0, length = publication->conductor_fields.subscribeable.length; i < length; i++)
        {
            if (aeron_counter_get_volatile(publication->conductor_fields.subscribeable.array[i].value_addr) < snd_pos)
            {
                return false;
            }
        }

        aeron_driver_conductor_cleanup_spies(conductor, publication);

        for (size_t i = 0, length = publication->conductor_fields.subscribeable.length; i < length; i++)
        {
            aeron_counters_manager_free(
                &conductor->counters_manager, (int32_t)publication->conductor_fields.subscribeable.array[i].counter_id);
        }

        aeron_free(publication->conductor_fields.subscribeable.array);
        publication->conductor_fields.subscribeable.array = NULL;
        publication->conductor_fields.subscribeable.length = 0;
        publication->conductor_fields.subscribeable.capacity = 0;
    }

    return true;
}

void aeron_network_publication_on_time_event(
    aeron_driver_conductor_t *conductor, aeron_network_publication_t *publication, int64_t now_ns, int64_t now_ms)
{
    switch (publication->conductor_fields.status)
    {
        case AERON_NETWORK_PUBLICATION_STATUS_ACTIVE:
        {
            aeron_network_publication_check_for_blocked_publisher(
                publication, now_ns, aeron_counter_get_volatile(publication->snd_pos_position.value_addr));

            bool is_connected;
            AERON_GET_VOLATILE(is_connected, publication->is_connected);
            if (is_connected)
            {
                int64_t time_of_last_status_message;
                AERON_GET_VOLATILE(time_of_last_status_message, publication->log_meta_data->time_of_last_status_message);
                if (now_ms > (time_of_last_status_message + AERON_NETWORK_PUBLICATION_CONNECTION_TIMEOUT_MS))
                {
                    AERON_PUT_ORDERED(publication->is_connected, false);

                }
            }
            break;
        }

        case AERON_NETWORK_PUBLICATION_STATUS_DRAINING:
        {
            const int64_t snd_pos = aeron_counter_get_volatile(publication->snd_pos_position.value_addr);
            if (snd_pos == publication->conductor_fields.last_snd_pos)
            {
                if (aeron_network_publication_spies_not_behind_sender(publication, conductor, snd_pos))
                {
                    AERON_PUT_ORDERED(publication->is_complete, true);
                    publication->conductor_fields.time_of_last_activity_ns = now_ns;
                    publication->conductor_fields.status = AERON_NETWORK_PUBLICATION_STATUS_LINGER;
                }
            }
            else
            {
                publication->conductor_fields.last_snd_pos = snd_pos;
                publication->conductor_fields.time_of_last_activity_ns = now_ns;
            }
            break;
        }

        case AERON_NETWORK_PUBLICATION_STATUS_LINGER:
        {
            if (now_ns > (publication->conductor_fields.time_of_last_activity_ns + publication->linger_timeout_ns))
            {
                aeron_driver_conductor_cleanup_network_publication(conductor, publication);
                publication->conductor_fields.status = AERON_NETWORK_PUBLICATION_STATUS_CLOSING;
            }
            break;
        }

        case AERON_NETWORK_PUBLICATION_STATUS_CLOSING:
            break;
    }
}

extern int64_t aeron_network_publication_producer_position(aeron_network_publication_t *publication);
extern int64_t aeron_network_publication_spy_join_position(aeron_network_publication_t *publication);
extern void aeron_network_publication_trigger_send_setup_frame(aeron_network_publication_t *publication);
extern void aeron_network_publication_sender_release(aeron_network_publication_t *publication);
extern bool aeron_network_publication_has_sender_released(aeron_network_publication_t *publication);
