/*
 *  Copyright 2017 Real Logic Ltd.
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
package io.aeron;

import io.aeron.logbuffer.FrameDescriptor;
import io.aeron.logbuffer.LogBufferDescriptor;

import static io.aeron.CommonContext.*;
import static io.aeron.logbuffer.FrameDescriptor.FRAME_ALIGNMENT;

/**
 * Type safe means of building a channel URI associated with a publication or subscription.
 *
 * @see Aeron#addPublication(String, int)
 * @see Aeron#addSubscription(String, int)
 * @see AeronUri
 */
public class ChannelUriBuilder
{
    private StringBuilder sb = new StringBuilder(64);

    private String prefix;
    private String media;
    private String endpoint;
    private String networkInterface;
    private String controlEndpoint;
    private String controlMode;
    private Boolean reliable;
    private Integer ttl;
    private Integer mtu;
    private Integer termLength;
    private Integer initialTermId;
    private Integer termId;
    private Integer termOffset;

    /**
     * Clear out all the values thus setting back to the initial state.
     *
     * @return this for a fluent API.
     */
    public ChannelUriBuilder clear()
    {
        prefix = null;
        media = null;
        endpoint = null;
        networkInterface = null;
        controlEndpoint = null;
        controlMode = null;
        reliable = null;
        ttl  = null;
        mtu = null;
        termLength = null;
        initialTermId = null;
        termId = null;
        termOffset = null;

        return this;
    }

    /**
     * Validates that the collection of set parameters are valid together.
     *
     * @return this for a fluent API.
     * @throws IllegalStateException if the combination of params is invalid.
     */
    public ChannelUriBuilder validate()
    {
        if (null == media)
        {
            throw new IllegalStateException("media type is mandatory");
        }

        if ("udp".equals(media) && (null == endpoint && null == controlEndpoint))
        {
            throw new IllegalStateException("Either 'endpoint' or 'control' must be specified for UDP.");
        }

        int count = 0;
        count += null == initialTermId ? 0 : 1;
        count += null == termId ? 0 : 1;
        count += null == termOffset ? 0 : 1;

        if (count > 0 && count < 3)
        {
            throw new IllegalStateException(
                "If any of then a complete set of 'initialTermId', 'termId', and 'termOffset' must be provided");
        }

        return this;
    }

    /**
     * Set the prefix for taking an addition action such as spying on an outgoing publication with "aeron-spy".
     *
     * @param prefix to be applied to the URI before the the scheme.
     * @return this for a fluent API.
     */
    public ChannelUriBuilder prefix(final String prefix)
    {
        if (null != prefix && !prefix.equals("aeron-spy"))
        {
            throw new IllegalArgumentException("Invalid prefix: " + prefix);
        }

        this.prefix = prefix;
        return this;
    }

    /**
     * Get the prefix for the additional action to be taken on the request.
     *
     * @return the prefix for the additional action to be taken on the request.
     */
    public String prefix()
    {
        return prefix;
    }

    /**
     * Set the media for this channel. Valid values are "udp" and "ipc".
     *
     * @param media for this channel.
     * @return this for a fluent API.
     */
    public ChannelUriBuilder media(final String media)
    {
        switch (media)
        {
            case "udp":
            case "ipc":
                break;

            default:
                throw new IllegalArgumentException("Invalid media: " + media);
        }

        this.media = media;
        return this;
    }

    /**
     * The media over which the channel transmits.
     *
     * @return the media over which the channel transmits.
     */
    public String media()
    {
        return media;
    }

    /**
     * Set the endpoint address:port pairing for the channel. This is the address the publication sends to and the
     * address the subscription receives from.
     *
     * @param endpoint address and port for the channel.
     * @return this for a fluent API.
     */
    public ChannelUriBuilder endpoint(final String endpoint)
    {
        this.endpoint = endpoint;
        return this;
    }

    /**
     * Get the endpoint address:port pairing for the channel.
     *
     * @return the endpoint address:port pairing for the channel.
     */
    public String endpoint()
    {
        return endpoint;
    }

    /**
     * Set the address of the local interface in the form host:[port]/[subnet mask] for routing traffic.
     *
     * @param networkInterface for routing traffic.
     * @return this for a fluent API.
     */
    public ChannelUriBuilder networkInterface(final String networkInterface)
    {
        this.networkInterface = networkInterface;
        return this;
    }

    /**
     * Get the address of the local interface in the form host:[port]/[subnet mask] for routing traffic.
     *
     * @return the address of the local interface in the form host:[port]/[subnet mask] for routing traffic.
     */
    public String networkInterface()
    {
        return networkInterface;
    }

    /**
     * Set the control address:port pair for dynamically joining a multi-destination-cast publication.
     *
     * @param controlEndpoint for joining a MDC control socket.
     * @return this for a fluent API.
     */
    public ChannelUriBuilder controlEndpoint(final String controlEndpoint)
    {
        this.controlEndpoint = controlEndpoint;
        return this;
    }

    /**
     * Get the control address:port pair for dynamically joining a multi-destination-cast publication.
     *
     * @return the control address:port pair for dynamically joining a multi-destination-cast publication.
     */
    public String controlEndpoint()
    {
        return controlEndpoint;
    }

    /**
     * Set the control mode for multi-destination-cast. Set to "manual" for allowing control from the publication API.
     *
     * @param controlMode for taking control of MDC.
     * @return this for a fluent API.
     * @see Publication#addDestination(String)
     * @see Publication#removeDestination(String)
     */
    public ChannelUriBuilder controlMode(final String controlMode)
    {
        if (null != controlMode && !controlMode.equals(CommonContext.MDC_CONTROL_MODE_MANUAL))
        {
            throw new IllegalArgumentException("Invalid control mode: " + controlMode);
        }

        this.controlMode = controlMode;
        return this;
    }

    /**
     * Get the control mode for multi-destination-cast.
     *
     * @return the control mode for multi-destination-cast.
     */
    public String controlMode()
    {
        return controlMode;
    }

    /**
     * Set the subscription semantics for if loss is acceptable, or not, for a reliable message delivery.
     *
     * @param isReliable false if loss can be be gap filled.
     * @return this for a fluent API.
     */
    public ChannelUriBuilder reliable(final Boolean isReliable)
    {
        this.reliable = isReliable;
        return this;
    }

    /**
     * Get the subscription semantics for if loss is acceptable, or not, for a reliable message delivery.
     *
     * @return the subscription semantics for if loss is acceptable, or not, for a reliable message delivery.
     */
    public Boolean reliable()
    {
        return reliable;
    }

    /**
     * Set the Time To Live (TTL) for a multicast datagram. Valid values are 0-255 for the number of hops the datagram
     * can progress along.
     *
     * @param ttl value for a multicast datagram.
     * @return this for a fluent API.
     */
    public ChannelUriBuilder ttl(final Integer ttl)
    {
        if (null != ttl && (ttl < 0 || ttl > 255))
        {
            throw new IllegalArgumentException("TTL not in range 0-255: " + ttl);
        }

        this.ttl = ttl;
        return this;
    }

    /**
     * Get the Time To Live (TTL) for a multicast datagram.
     *
     * @return the Time To Live (TTL) for a multicast datagram.
     */
    public Integer ttl()
    {
        return ttl;
    }

    /**
     * Set the maximum transmission unit (MTU) including Aeron header for a datagram payload.
     *
     * @param mtu the maximum transmission unit including Aeron header for a datagram payload.
     * @return this for a fluent API.
     */
    public ChannelUriBuilder mtu(final Integer mtu)
    {
        if (null != mtu)
        {
            if (mtu < 32 || mtu > 65504)
            {
                throw new IllegalArgumentException("MTU not in range 32-65504: " + mtu);
            }

            if ((mtu & (FRAME_ALIGNMENT - 1)) != 0)
            {
                throw new IllegalArgumentException("MTU not a multiple of FRAME_ALIGNMENT: mtu=" + mtu);
            }
        }

        this.mtu = mtu;
        return this;
    }

    /**
     * Get the maximum transmission unit (MTU) including Aeron header for a datagram payload.
     *
     * @return the maximum transmission unit (MTU) including Aeron header for a datagram payload.
     */
    public Integer mtu()
    {
        return mtu;
    }

    /**
     * Set the length of buffer used for each term of the log. Valid values are powers of 2 in the 64K - 1G range.
     *
     * @param termLength of the buffer used for each term of the log.
     * @return this for a fluent API.
     */
    public ChannelUriBuilder termLength(final Integer termLength)
    {
        if (null != termLength)
        {
            LogBufferDescriptor.checkTermLength(termLength);
        }

        this.termLength = termLength;
        return this;
    }

    /**
     * Get the length of buffer used for each term of the log.
     *
     * @return the length of buffer used for each term of the log.
     */
    public Integer termLength()
    {
        return termLength;
    }

    /**
     * Set the initial term id at which a publication will start.
     *
     * @param initialTermId the initial term id at which a publication will start.
     * @return this for a fluent API.
     */
    public ChannelUriBuilder initialTermId(final Integer initialTermId)
    {
        this.initialTermId = initialTermId;
        return this;
    }

    /**
     * the initial term id at which a publication will start.
     *
     * @return the initial term id at which a publication will start.
     */
    public Integer initialTermId()
    {
        return initialTermId;
    }

    /**
     * Set the current term id at which a publication will start. This when combined with the initial term can
     * establish a starting position.
     *
     * @param termId at which a publication will start.
     * @return this for a fluent API.
     */
    public ChannelUriBuilder termId(final Integer termId)
    {
        this.termId = termId;
        return this;
    }

    /**
     * Get the current term id at which a publication will start.
     *
     * @return the current term id at which a publication will start.
     */
    public Integer termId()
    {
        return termId;
    }

    /**
     * Set the offset within a term at which a publication will start. This when combined with the term id can establish
     * a starting position.
     *
     * @param termOffset within a term at which a publication will start.
     * @return this for a fluent API.
     */
    public ChannelUriBuilder termOffset(final Integer termOffset)
    {
        if (null != termOffset)
        {
            if ((termOffset < 0 || termOffset > LogBufferDescriptor.TERM_MAX_LENGTH))
            {
                throw new IllegalArgumentException("Term offset not in range 0-1g: " + termOffset);
            }

            if (0 != (termOffset & (FrameDescriptor.FRAME_ALIGNMENT - 1)))
            {
                throw new IllegalArgumentException("Term offset not multiple of FRAME_ALIGNMENT: " + termOffset);
            }
        }

        this.termOffset = termOffset;
        return this;
    }

    /**
     * Get the offset within a term at which a publication will start.
     *
     * @return the offset within a term at which a publication will start.
     */
    public Integer termOffset()
    {
        return termOffset;
    }

    /**
     * Build a channel URI String for the given parameters.
     *
     * @return a channel URI String for the given parameters.
     */
    public String build()
    {
        sb.setLength(0);

        if (null != prefix)
        {
            sb.append(prefix).append(':');
        }

        sb.append(AeronUri.AERON_SCHEME).append(':').append(media).append('?');

        if (null != endpoint)
        {
            sb.append(ENDPOINT_PARAM_NAME).append('=').append(endpoint).append('|');
        }

        if (null != networkInterface)
        {
            sb.append(INTERFACE_PARAM_NAME).append('=').append(networkInterface).append('|');
        }

        if (null != controlEndpoint)
        {
            sb.append(MDC_CONTROL_PARAM_NAME).append('=').append(controlEndpoint).append('|');
        }

        if (null != controlMode)
        {
            sb.append(MDC_CONTROL_MODE_PARAM_NAME).append('=').append(controlMode).append('|');
        }

        if (null != reliable)
        {
            sb.append(RELIABLE_STREAM_PARAM_NAME).append('=').append(reliable).append('|');
        }

        if (null != ttl)
        {
            sb.append(TTL_PARAM_NAME).append('=').append(ttl.intValue()).append('|');
        }

        if (null != mtu)
        {
            sb.append(MTU_LENGTH_PARAM_NAME).append('=').append(mtu.intValue()).append('|');
        }

        if (null != termLength)
        {
            sb.append(TERM_LENGTH_PARAM_NAME).append('=').append(termLength.intValue()).append('|');
        }

        if (null != initialTermId)
        {
            sb.append(INITIAL_TERM_ID_PARAM_NAME).append('=').append(initialTermId.intValue()).append('|');
        }

        if (null != termId)
        {
            sb.append(TERM_ID_PARAM_NAME).append('=').append(termId.intValue()).append('|');
        }

        if (null != termOffset)
        {
            sb.append(TERM_OFFSET_PARAM_NAME).append('=').append(termOffset.intValue()).append('|');
        }

        final char lastChar = sb.charAt(sb.length() - 1);
        if (lastChar == '|' || lastChar == '?')
        {
            sb.setLength(sb.length() - 1);
        }

        return sb.toString();
    }
}
