/*
 * Copyright 2014-2017 Real Logic Ltd.
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
package io.aeron.driver;

import io.aeron.ErrorCode;
import io.aeron.command.*;
import org.agrona.DirectBuffer;
import org.agrona.concurrent.UnsafeBuffer;
import org.agrona.concurrent.broadcast.BroadcastTransmitter;

import java.nio.ByteBuffer;
import java.util.List;

import static io.aeron.command.ControlProtocolEvents.*;

/**
 * Proxy for communicating from the driver to the client conductor.
 */
public class ClientProxy
{
    private static final int WRITE_BUFFER_CAPACITY = 4096;

    private final UnsafeBuffer buffer = new UnsafeBuffer(ByteBuffer.allocateDirect(WRITE_BUFFER_CAPACITY));
    private final BroadcastTransmitter transmitter;

    private final ErrorResponseFlyweight errorResponse = new ErrorResponseFlyweight();
    private final PublicationBuffersReadyFlyweight publicationReady = new PublicationBuffersReadyFlyweight();
    private final ImageBuffersReadyFlyweight imageReady = new ImageBuffersReadyFlyweight();
    private final CorrelatedMessageFlyweight correlatedMessage = new CorrelatedMessageFlyweight();
    private final ImageMessageFlyweight imageMessage = new ImageMessageFlyweight();

    public ClientProxy(final BroadcastTransmitter transmitter)
    {
        this.transmitter = transmitter;

        errorResponse.wrap(buffer, 0);
        imageReady.wrap(buffer, 0);
        publicationReady.wrap(buffer, 0);
        correlatedMessage.wrap(buffer, 0);
        imageMessage.wrap(buffer, 0);
    }

    public void onError(final long correlationId, final ErrorCode errorCode, final String errorMessage)
    {
        final String msg = null == errorMessage ? "" : errorMessage;

        errorResponse
            .offendingCommandCorrelationId(correlationId)
            .errorCode(errorCode)
            .errorMessage(msg);

        transmit(ON_ERROR, buffer, 0, errorResponse.length());
    }

    public void onAvailableImage(
        final long correlationId,
        final int streamId,
        final int sessionId,
        final String logFileName,
        final List<SubscriberPosition> subscriberPositions,
        final String sourceIdentity)
    {
        imageReady
            .sessionId(sessionId)
            .streamId(streamId)
            .correlationId(correlationId);

        final int size = subscriberPositions.size();
        imageReady.subscriberPositionCount(size);
        for (int i = 0; i < size; i++)
        {
            final SubscriberPosition position = subscriberPositions.get(i);
            imageReady.subscriberPositionId(i, position.positionCounterId());
            imageReady.positionIndicatorRegistrationId(i, position.subscription().registrationId());
        }

        imageReady
            .logFileName(logFileName)
            .sourceIdentity(sourceIdentity);

        final int length = imageReady.length();
        transmit(ON_AVAILABLE_IMAGE, buffer, 0, length);
    }

    public void onPublicationReady(
        final long correlationId,
        final long registrationId,
        final int streamId,
        final int sessionId,
        final String logFileName,
        final int positionCounterId,
        final boolean isExclusive)
    {
        publicationReady
            .correlationId(correlationId)
            .registrationId(registrationId)
            .sessionId(sessionId)
            .streamId(streamId)
            .publicationLimitCounterId(positionCounterId)
            .logFileName(logFileName);

        final int length = publicationReady.length();
        final int msgTypeId = isExclusive ? ON_EXCLUSIVE_PUBLICATION_READY : ON_PUBLICATION_READY;
        transmit(msgTypeId, buffer, 0, length);
    }

    public void operationSucceeded(final long correlationId)
    {
        correlatedMessage.clientId(0).correlationId(correlationId);

        transmit(ON_OPERATION_SUCCESS, buffer, 0, CorrelatedMessageFlyweight.LENGTH);
    }

    public void onUnavailableImage(final long correlationId, final int streamId, final String channel)
    {
        imageMessage
            .correlationId(correlationId)
            .streamId(streamId)
            .channel(channel);

        final int length = imageMessage.length();
        transmit(ON_UNAVAILABLE_IMAGE, buffer, 0, length);
    }

    private void transmit(final int msgTypeId, final DirectBuffer buffer, final int index, final int length)
    {
        transmitter.transmit(msgTypeId, buffer, index, length);
    }
}
