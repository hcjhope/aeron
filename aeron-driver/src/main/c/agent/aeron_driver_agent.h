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

#ifndef AERON_AERON_DRIVER_AGENT_H
#define AERON_AERON_DRIVER_AGENT_H

#include "aeron_driver_conductor.h"
#include "command/aeron_control_protocol.h"

#define AERON_AGENT_MASK_ENV_VAR "AERON_EVENT_LOG"
#define RING_BUFFER_LENGTH (2 * 1024 * 1024)
#define MAX_CMD_LENGTH (512)
#define MAX_FRAME_LENGTH (512)

#define AERON_CMD_IN (0x01)
#define AERON_CMD_OUT (0x02)
#define AERON_FRAME_IN (0x04)
#define AERON_FRAME_OUT (0x08)

typedef struct aeron_driver_agent_cmd_log_header_stct
{
    int64_t time_ms;
    int64_t cmd_id;
}
aeron_driver_agent_cmd_log_header_t;

typedef struct aeron_driver_agent_frame_log_header_stct
{
    int64_t time_ms;
    int32_t result;
    int32_t sockaddr_len;
    int32_t message_len;
}
aeron_driver_agent_frame_log_header_t;

typedef int (*aeron_driver_context_init_t)(aeron_driver_context_t **);

void aeron_driver_agent_log_dissector(int32_t msg_type_id, const void *message, size_t length, void *clientd);

/* TODO: hook recvmsg, recvmmsg, to do FRAME_IN, FRAME_OUT */
/* TODO: hook aeron_driver_init to display options, etc. for instance. */

#endif //AERON_AERON_DRIVER_AGENT_H
