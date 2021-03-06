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

#include "aeron_driver_conductor_test.h"

TEST_F(DriverConductorTest, shouldBeAbleToAddSingleNetworkPublication)
{
    int64_t client_id = nextCorrelationId();
    int64_t pub_id = nextCorrelationId();

    ASSERT_EQ(addNetworkPublication(client_id, pub_id, CHANNEL_1, STREAM_ID_1, false), 0);

    doWork();

    aeron_send_channel_endpoint_t *endpoint =
        aeron_driver_conductor_find_send_channel_endpoint(&m_conductor.m_conductor, CHANNEL_1);

    ASSERT_NE(endpoint, (aeron_send_channel_endpoint_t *)NULL);

    aeron_network_publication_t *publication =
        aeron_driver_conductor_find_network_publication(&m_conductor.m_conductor, pub_id);

    ASSERT_NE(publication, (aeron_network_publication_t *)NULL);

    auto handler = [&](std::int32_t msgTypeId, AtomicBuffer& buffer, util::index_t offset, util::index_t length)
    {
        ASSERT_EQ(msgTypeId, AERON_RESPONSE_ON_PUBLICATION_READY);

        const command::PublicationBuffersReadyFlyweight response(buffer, offset);

        EXPECT_EQ(response.streamId(), STREAM_ID_1);
        EXPECT_EQ(response.correlationId(), pub_id);
        EXPECT_GT(response.logFileName().length(), 0u);
    };

    EXPECT_EQ(readAllBroadcastsFromConductor(handler), 1u);
}

TEST_F(DriverConductorTest, shouldBeAbleToAddAndRemoveSingleNetworkPublication)
{
    int64_t client_id = nextCorrelationId();
    int64_t pub_id = nextCorrelationId();
    int64_t remove_correlation_id = nextCorrelationId();

    ASSERT_EQ(addNetworkPublication(client_id, pub_id, CHANNEL_1, STREAM_ID_1, false), 0);
    doWork();
    EXPECT_EQ(aeron_driver_conductor_num_network_publications(&m_conductor.m_conductor), 1u);
    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 1u);

    ASSERT_EQ(removePublication(client_id, remove_correlation_id, pub_id), 0);
    doWork();
    auto handler = [&](std::int32_t msgTypeId, AtomicBuffer& buffer, util::index_t offset, util::index_t length)
    {
        ASSERT_EQ(msgTypeId, AERON_RESPONSE_ON_OPERATION_SUCCESS);

        const command::CorrelatedMessageFlyweight response(buffer, offset);

        EXPECT_EQ(response.correlationId(), remove_correlation_id);
    };

    EXPECT_EQ(readAllBroadcastsFromConductor(handler), 1u);
}

TEST_F(DriverConductorTest, shouldBeAbleToAddSingleNetworkSubscription)
{
    int64_t client_id = nextCorrelationId();
    int64_t sub_id = nextCorrelationId();

    ASSERT_EQ(addNetworkSubscription(client_id, sub_id, CHANNEL_1, STREAM_ID_1, -1), 0);

    doWork();

    aeron_receive_channel_endpoint_t *endpoint =
        aeron_driver_conductor_find_receive_channel_endpoint(&m_conductor.m_conductor, CHANNEL_1);

    ASSERT_NE(endpoint, (aeron_receive_channel_endpoint_t *)NULL);

    auto handler = [&](std::int32_t msgTypeId, AtomicBuffer& buffer, util::index_t offset, util::index_t length)
    {
        ASSERT_EQ(msgTypeId, AERON_RESPONSE_ON_OPERATION_SUCCESS);

        const command::CorrelatedMessageFlyweight response(buffer, offset);

        EXPECT_EQ(response.correlationId(), sub_id);
    };

    EXPECT_EQ(readAllBroadcastsFromConductor(handler), 1u);
}

TEST_F(DriverConductorTest, shouldBeAbleToAddAndRemoveSingleNetworkSubscription)
{
    int64_t client_id = nextCorrelationId();
    int64_t sub_id = nextCorrelationId();
    int64_t remove_correlation_id = nextCorrelationId();

    ASSERT_EQ(addNetworkSubscription(client_id, sub_id, CHANNEL_1, STREAM_ID_1, -1), 0);
    doWork();
    EXPECT_EQ(aeron_driver_conductor_num_network_subscriptions(&m_conductor.m_conductor), 1u);
    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 1u);

    ASSERT_EQ(removeSubscription(client_id, remove_correlation_id, sub_id), 0);
    doWork();
    auto handler = [&](std::int32_t msgTypeId, AtomicBuffer& buffer, util::index_t offset, util::index_t length)
    {
        ASSERT_EQ(msgTypeId, AERON_RESPONSE_ON_OPERATION_SUCCESS);

        const command::CorrelatedMessageFlyweight response(buffer, offset);

        EXPECT_EQ(response.correlationId(), remove_correlation_id);
    };

    EXPECT_EQ(aeron_driver_conductor_num_network_subscriptions(&m_conductor.m_conductor), 0u);
    EXPECT_EQ(readAllBroadcastsFromConductor(handler), 1u);
}

TEST_F(DriverConductorTest, shouldBeAbleToAddMultipleNetworkPublications)
{
    int64_t client_id = nextCorrelationId();
    int64_t pub_id_1 = nextCorrelationId();
    int64_t pub_id_2 = nextCorrelationId();
    int64_t pub_id_3 = nextCorrelationId();
    int64_t pub_id_4 = nextCorrelationId();

    ASSERT_EQ(addNetworkPublication(client_id, pub_id_1, CHANNEL_1, STREAM_ID_1, false), 0);
    ASSERT_EQ(addNetworkPublication(client_id, pub_id_2, CHANNEL_1, STREAM_ID_2, false), 0);
    ASSERT_EQ(addNetworkPublication(client_id, pub_id_3, CHANNEL_1, STREAM_ID_3, false), 0);
    ASSERT_EQ(addNetworkPublication(client_id, pub_id_4, CHANNEL_1, STREAM_ID_4, false), 0);
    doWork();

    aeron_send_channel_endpoint_t *endpoint =
        aeron_driver_conductor_find_send_channel_endpoint(&m_conductor.m_conductor, CHANNEL_1);

    ASSERT_NE(endpoint, (aeron_send_channel_endpoint_t *)NULL);
    ASSERT_EQ(aeron_driver_conductor_num_send_channel_endpoints(&m_conductor.m_conductor), 1u);

    aeron_network_publication_t *publication_1 =
        aeron_driver_conductor_find_network_publication(&m_conductor.m_conductor, pub_id_1);
    aeron_network_publication_t *publication_2 =
        aeron_driver_conductor_find_network_publication(&m_conductor.m_conductor, pub_id_2);
    aeron_network_publication_t *publication_3 =
        aeron_driver_conductor_find_network_publication(&m_conductor.m_conductor, pub_id_3);
    aeron_network_publication_t *publication_4 =
        aeron_driver_conductor_find_network_publication(&m_conductor.m_conductor, pub_id_4);

    ASSERT_NE(publication_1, (aeron_network_publication_t *)NULL);
    ASSERT_NE(publication_2, (aeron_network_publication_t *)NULL);
    ASSERT_NE(publication_3, (aeron_network_publication_t *)NULL);
    ASSERT_NE(publication_4, (aeron_network_publication_t *)NULL);

    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 4u);
}

TEST_F(DriverConductorTest, shouldBeAbleToAddMultipleNetworkPublicationsDifferentChannelsSameStreamId)
{
    int64_t client_id = nextCorrelationId();
    int64_t pub_id_1 = nextCorrelationId();
    int64_t pub_id_2 = nextCorrelationId();
    int64_t pub_id_3 = nextCorrelationId();
    int64_t pub_id_4 = nextCorrelationId();

    ASSERT_EQ(addNetworkPublication(client_id, pub_id_1, CHANNEL_1, STREAM_ID_1, false), 0);
    ASSERT_EQ(addNetworkPublication(client_id, pub_id_2, CHANNEL_2, STREAM_ID_1, false), 0);
    ASSERT_EQ(addNetworkPublication(client_id, pub_id_3, CHANNEL_3, STREAM_ID_1, false), 0);
    ASSERT_EQ(addNetworkPublication(client_id, pub_id_4, CHANNEL_4, STREAM_ID_1, false), 0);
    doWork();

    aeron_send_channel_endpoint_t *endpoint_1 =
        aeron_driver_conductor_find_send_channel_endpoint(&m_conductor.m_conductor, CHANNEL_1);
    aeron_send_channel_endpoint_t *endpoint_2 =
        aeron_driver_conductor_find_send_channel_endpoint(&m_conductor.m_conductor, CHANNEL_2);
    aeron_send_channel_endpoint_t *endpoint_3 =
        aeron_driver_conductor_find_send_channel_endpoint(&m_conductor.m_conductor, CHANNEL_3);
    aeron_send_channel_endpoint_t *endpoint_4 =
        aeron_driver_conductor_find_send_channel_endpoint(&m_conductor.m_conductor, CHANNEL_4);

    ASSERT_NE(endpoint_1, (aeron_send_channel_endpoint_t *)NULL);
    ASSERT_NE(endpoint_2, (aeron_send_channel_endpoint_t *)NULL);
    ASSERT_NE(endpoint_3, (aeron_send_channel_endpoint_t *)NULL);
    ASSERT_NE(endpoint_4, (aeron_send_channel_endpoint_t *)NULL);
    ASSERT_EQ(aeron_driver_conductor_num_send_channel_endpoints(&m_conductor.m_conductor), 4u);

    aeron_network_publication_t *publication_1 =
        aeron_driver_conductor_find_network_publication(&m_conductor.m_conductor, pub_id_1);
    aeron_network_publication_t *publication_2 =
        aeron_driver_conductor_find_network_publication(&m_conductor.m_conductor, pub_id_2);
    aeron_network_publication_t *publication_3 =
        aeron_driver_conductor_find_network_publication(&m_conductor.m_conductor, pub_id_3);
    aeron_network_publication_t *publication_4 =
        aeron_driver_conductor_find_network_publication(&m_conductor.m_conductor, pub_id_4);

    ASSERT_NE(publication_1, (aeron_network_publication_t *)NULL);
    ASSERT_NE(publication_2, (aeron_network_publication_t *)NULL);
    ASSERT_NE(publication_3, (aeron_network_publication_t *)NULL);
    ASSERT_NE(publication_4, (aeron_network_publication_t *)NULL);

    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 4u);
}

TEST_F(DriverConductorTest, shouldBeAbleToAddMultipleExclusiveNetworkPublicationsWithSameChannelSameStreamId)
{
    int64_t client_id = nextCorrelationId();
    int64_t pub_id_1 = nextCorrelationId();
    int64_t pub_id_2 = nextCorrelationId();
    int64_t pub_id_3 = nextCorrelationId();
    int64_t pub_id_4 = nextCorrelationId();

    ASSERT_EQ(addNetworkPublication(client_id, pub_id_1, CHANNEL_1, STREAM_ID_1, true), 0);
    ASSERT_EQ(addNetworkPublication(client_id, pub_id_2, CHANNEL_1, STREAM_ID_1, true), 0);
    ASSERT_EQ(addNetworkPublication(client_id, pub_id_3, CHANNEL_1, STREAM_ID_1, true), 0);
    ASSERT_EQ(addNetworkPublication(client_id, pub_id_4, CHANNEL_1, STREAM_ID_1, true), 0);
    doWork();

    aeron_send_channel_endpoint_t *endpoint =
        aeron_driver_conductor_find_send_channel_endpoint(&m_conductor.m_conductor, CHANNEL_1);

    ASSERT_NE(endpoint, (aeron_send_channel_endpoint_t *)NULL);
    ASSERT_EQ(aeron_driver_conductor_num_send_channel_endpoints(&m_conductor.m_conductor), 1u);

    aeron_network_publication_t *publication_1 =
        aeron_driver_conductor_find_network_publication(&m_conductor.m_conductor, pub_id_1);
    aeron_network_publication_t *publication_2 =
        aeron_driver_conductor_find_network_publication(&m_conductor.m_conductor, pub_id_2);
    aeron_network_publication_t *publication_3 =
        aeron_driver_conductor_find_network_publication(&m_conductor.m_conductor, pub_id_3);
    aeron_network_publication_t *publication_4 =
        aeron_driver_conductor_find_network_publication(&m_conductor.m_conductor, pub_id_4);

    ASSERT_NE(publication_1, (aeron_network_publication_t *)NULL);
    ASSERT_NE(publication_2, (aeron_network_publication_t *)NULL);
    ASSERT_NE(publication_3, (aeron_network_publication_t *)NULL);
    ASSERT_NE(publication_4, (aeron_network_publication_t *)NULL);

    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 4u);
}

TEST_F(DriverConductorTest, shouldBeAbleToAddMultipleNetworkSubscriptionsWithSameChannelSameStreamId)
{
    int64_t client_id = nextCorrelationId();
    int64_t sub_id_1 = nextCorrelationId();
    int64_t sub_id_2 = nextCorrelationId();
    int64_t sub_id_3 = nextCorrelationId();
    int64_t sub_id_4 = nextCorrelationId();

    ASSERT_EQ(addNetworkSubscription(client_id, sub_id_1, CHANNEL_1, STREAM_ID_1, -1), 0);
    ASSERT_EQ(addNetworkSubscription(client_id, sub_id_2, CHANNEL_1, STREAM_ID_1, -1), 0);
    ASSERT_EQ(addNetworkSubscription(client_id, sub_id_3, CHANNEL_1, STREAM_ID_1, -1), 0);
    ASSERT_EQ(addNetworkSubscription(client_id, sub_id_4, CHANNEL_1, STREAM_ID_1, -1), 0);

    doWork();

    aeron_receive_channel_endpoint_t *endpoint =
        aeron_driver_conductor_find_receive_channel_endpoint(&m_conductor.m_conductor, CHANNEL_1);

    ASSERT_NE(endpoint, (aeron_receive_channel_endpoint_t *)NULL);
    ASSERT_EQ(aeron_driver_conductor_num_receive_channel_endpoints(&m_conductor.m_conductor), 1u);
    ASSERT_EQ(aeron_driver_conductor_num_network_subscriptions(&m_conductor.m_conductor), 4u);

    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 4u);
}

TEST_F(DriverConductorTest, shouldBeAbleToAddMultipleNetworkSubscriptionsWithDifferentChannelSameStreamId)
{
    int64_t client_id = nextCorrelationId();
    int64_t sub_id_1 = nextCorrelationId();
    int64_t sub_id_2 = nextCorrelationId();
    int64_t sub_id_3 = nextCorrelationId();
    int64_t sub_id_4 = nextCorrelationId();

    ASSERT_EQ(addNetworkSubscription(client_id, sub_id_1, CHANNEL_1, STREAM_ID_1, -1), 0);
    ASSERT_EQ(addNetworkSubscription(client_id, sub_id_2, CHANNEL_2, STREAM_ID_1, -1), 0);
    ASSERT_EQ(addNetworkSubscription(client_id, sub_id_3, CHANNEL_3, STREAM_ID_1, -1), 0);
    ASSERT_EQ(addNetworkSubscription(client_id, sub_id_4, CHANNEL_4, STREAM_ID_1, -1), 0);

    doWork();

    aeron_receive_channel_endpoint_t *endpoint_1 =
        aeron_driver_conductor_find_receive_channel_endpoint(&m_conductor.m_conductor, CHANNEL_1);
    aeron_receive_channel_endpoint_t *endpoint_2 =
        aeron_driver_conductor_find_receive_channel_endpoint(&m_conductor.m_conductor, CHANNEL_2);
    aeron_receive_channel_endpoint_t *endpoint_3 =
        aeron_driver_conductor_find_receive_channel_endpoint(&m_conductor.m_conductor, CHANNEL_3);
    aeron_receive_channel_endpoint_t *endpoint_4 =
        aeron_driver_conductor_find_receive_channel_endpoint(&m_conductor.m_conductor, CHANNEL_4);

    ASSERT_NE(endpoint_1, (aeron_receive_channel_endpoint_t *)NULL);
    ASSERT_NE(endpoint_2, (aeron_receive_channel_endpoint_t *)NULL);
    ASSERT_NE(endpoint_3, (aeron_receive_channel_endpoint_t *)NULL);
    ASSERT_NE(endpoint_4, (aeron_receive_channel_endpoint_t *)NULL);
    ASSERT_EQ(aeron_driver_conductor_num_receive_channel_endpoints(&m_conductor.m_conductor), 4u);
    ASSERT_EQ(aeron_driver_conductor_num_network_subscriptions(&m_conductor.m_conductor), 4u);

    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 4u);
}

TEST_F(DriverConductorTest, shouldBeAbleToTimeoutNetworkPublication)
{
    int64_t client_id = nextCorrelationId();
    int64_t pub_id = nextCorrelationId();

    ASSERT_EQ(addNetworkPublication(client_id, pub_id, CHANNEL_1, STREAM_ID_1, false), 0);
    doWork();
    EXPECT_EQ(aeron_driver_conductor_num_send_channel_endpoints(&m_conductor.m_conductor), 1u);
    EXPECT_EQ(aeron_driver_conductor_num_network_publications(&m_conductor.m_conductor), 1u);
    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 1u);

    doWorkUntilTimeNs(
        m_context.m_context->publication_linger_timeout_ns +
            (m_context.m_context->client_liveness_timeout_ns * 2));
    EXPECT_EQ(aeron_driver_conductor_num_clients(&m_conductor.m_conductor), 0u);
    EXPECT_EQ(aeron_driver_conductor_num_network_publications(&m_conductor.m_conductor), 0u);
    EXPECT_EQ(aeron_driver_conductor_num_send_channel_endpoints(&m_conductor.m_conductor), 0u);
}

TEST_F(DriverConductorTest, shouldBeAbleToNotTimeoutNetworkPublicationOnKeepalive)
{
    int64_t client_id = nextCorrelationId();
    int64_t pub_id = nextCorrelationId();

    ASSERT_EQ(addNetworkPublication(client_id, pub_id, CHANNEL_1, STREAM_ID_1, false), 0);
    doWork();
    EXPECT_EQ(aeron_driver_conductor_num_network_publications(&m_conductor.m_conductor), 1u);
    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 1u);

    int64_t timeout =
        m_context.m_context->publication_linger_timeout_ns +
            (m_context.m_context->client_liveness_timeout_ns * 2);

    doWorkUntilTimeNs(
        timeout,
        100,
        [&]()
        {
            clientKeepalive(client_id);
        });

    EXPECT_EQ(aeron_driver_conductor_num_clients(&m_conductor.m_conductor), 1u);
    EXPECT_EQ(aeron_driver_conductor_num_network_publications(&m_conductor.m_conductor), 1u);
}

TEST_F(DriverConductorTest, shouldBeAbleToTimeoutNetworkSubscription)
{
    int64_t client_id = nextCorrelationId();
    int64_t sub_id = nextCorrelationId();

    ASSERT_EQ(addNetworkSubscription(client_id, sub_id, CHANNEL_1, STREAM_ID_1, false), 0);
    doWork();
    EXPECT_EQ(aeron_driver_conductor_num_receive_channel_endpoints(&m_conductor.m_conductor), 1u);
    EXPECT_EQ(aeron_driver_conductor_num_network_subscriptions(&m_conductor.m_conductor), 1u);
    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 1u);

    doWorkUntilTimeNs(
        m_context.m_context->publication_linger_timeout_ns +
            (m_context.m_context->client_liveness_timeout_ns * 2));
    EXPECT_EQ(aeron_driver_conductor_num_clients(&m_conductor.m_conductor), 0u);
    EXPECT_EQ(aeron_driver_conductor_num_network_subscriptions(&m_conductor.m_conductor), 0u);
    EXPECT_EQ(aeron_driver_conductor_num_receive_channel_endpoints(&m_conductor.m_conductor), 0u);
}

TEST_F(DriverConductorTest, shouldBeAbleToNotTimeoutNetworkSubscriptionOnKeepalive)
{
    int64_t client_id = nextCorrelationId();
    int64_t sub_id = nextCorrelationId();

    ASSERT_EQ(addNetworkSubscription(client_id, sub_id, CHANNEL_1, STREAM_ID_1, false), 0);
    doWork();
    EXPECT_EQ(aeron_driver_conductor_num_network_subscriptions(&m_conductor.m_conductor), 1u);
    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 1u);

    int64_t timeout =
        m_context.m_context->publication_linger_timeout_ns +
            (m_context.m_context->client_liveness_timeout_ns * 2);

    doWorkUntilTimeNs(
        timeout,
        100,
        [&]()
        {
            clientKeepalive(client_id);
        });

    EXPECT_EQ(aeron_driver_conductor_num_clients(&m_conductor.m_conductor), 1u);
    EXPECT_EQ(aeron_driver_conductor_num_network_subscriptions(&m_conductor.m_conductor), 1u);
}

TEST_F(DriverConductorTest, shouldBeAbleToTimeoutSendChannelEndpointWithClientKeepaliveAfterRemovePublication)
{
    int64_t client_id = nextCorrelationId();
    int64_t pub_id = nextCorrelationId();
    int64_t remove_correlation_id = nextCorrelationId();

    ASSERT_EQ(addNetworkPublication(client_id, pub_id, CHANNEL_1, STREAM_ID_1, false), 0);
    doWork();
    EXPECT_EQ(aeron_driver_conductor_num_network_publications(&m_conductor.m_conductor), 1u);
    ASSERT_EQ(removePublication(client_id, remove_correlation_id, pub_id), 0);
    doWork();
    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 2u);

    int64_t timeout =
        m_context.m_context->publication_linger_timeout_ns +
            (m_context.m_context->client_liveness_timeout_ns * 2);

    doWorkUntilTimeNs(
        timeout,
        100,
        [&]()
        {
            clientKeepalive(client_id);
        });

    EXPECT_EQ(aeron_driver_conductor_num_clients(&m_conductor.m_conductor), 1u);
    EXPECT_EQ(aeron_driver_conductor_num_network_publications(&m_conductor.m_conductor), 0u);
    EXPECT_EQ(aeron_driver_conductor_num_send_channel_endpoints(&m_conductor.m_conductor), 0u);
}

TEST_F(DriverConductorTest, shouldBeAbleToTimeoutReceiveChannelEndpointWithClientKeepaliveAfterRemoveSubscription)
{
    int64_t client_id = nextCorrelationId();
    int64_t sub_id = nextCorrelationId();
    int64_t remove_correlation_id = nextCorrelationId();

    ASSERT_EQ(addNetworkSubscription(client_id, sub_id, CHANNEL_1, STREAM_ID_1, false), 0);
    doWork();
    EXPECT_EQ(aeron_driver_conductor_num_network_subscriptions(&m_conductor.m_conductor), 1u);
    ASSERT_EQ(removeSubscription(client_id, remove_correlation_id, sub_id), 0);
    doWork();
    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 2u);

    int64_t timeout = m_context.m_context->client_liveness_timeout_ns;

    doWorkUntilTimeNs(
        timeout,
        100,
        [&]()
        {
            clientKeepalive(client_id);
        });

    EXPECT_EQ(aeron_driver_conductor_num_clients(&m_conductor.m_conductor), 1u);
    EXPECT_EQ(aeron_driver_conductor_num_network_subscriptions(&m_conductor.m_conductor), 0u);
    EXPECT_EQ(aeron_driver_conductor_num_receive_channel_endpoints(&m_conductor.m_conductor), 0u);
}

TEST_F(DriverConductorTest, shouldCreatePublicationImageForActiveNetworkSubscription)
{
    int64_t client_id = nextCorrelationId();
    int64_t sub_id = nextCorrelationId();

    ASSERT_EQ(addNetworkSubscription(client_id, sub_id, CHANNEL_1, STREAM_ID_1, -1), 0);
    doWork();
    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 1u);

    aeron_receive_channel_endpoint_t *endpoint =
        aeron_driver_conductor_find_receive_channel_endpoint(&m_conductor.m_conductor, CHANNEL_1);

    createPublicationImage(endpoint, STREAM_ID_1, 1000);

    EXPECT_EQ(aeron_driver_conductor_num_images(&m_conductor.m_conductor), 1u);

    aeron_publication_image_t *image =
        aeron_driver_conductor_find_publication_image(&m_conductor.m_conductor, endpoint, STREAM_ID_1);

    EXPECT_NE(image, (aeron_publication_image_t *)NULL);
    EXPECT_EQ(aeron_publication_image_num_subscriptions(image), 1u);

    auto handler = [&](std::int32_t msgTypeId, AtomicBuffer& buffer, util::index_t offset, util::index_t length)
    {
        ASSERT_EQ(msgTypeId, AERON_RESPONSE_ON_AVAILABLE_IMAGE);

        const command::ImageBuffersReadyFlyweight response(buffer, offset);

        EXPECT_EQ(response.sessionId(), SESSION_ID);
        EXPECT_EQ(response.streamId(), STREAM_ID_1);
        EXPECT_EQ(response.correlationId(), aeron_publication_image_registration_id(image));
        EXPECT_EQ(response.subscriberPositionCount(), 1);

        const command::ImageBuffersReadyDefn::SubscriberPosition position = response.subscriberPosition(0);

        EXPECT_EQ(position.registrationId, sub_id);

        EXPECT_EQ(std::string(aeron_publication_image_log_file_name(image)), response.logFileName());
        EXPECT_EQ(SOURCE_IDENTITY, response.sourceIdentity());
    };

    EXPECT_EQ(readAllBroadcastsFromConductor(handler), 1u);
}

TEST_F(DriverConductorTest, shouldNotCreatePublicationImageForNonActiveNetworkSubscription)
{
    int64_t client_id = nextCorrelationId();
    int64_t sub_id = nextCorrelationId();

    ASSERT_EQ(addNetworkSubscription(client_id, sub_id, CHANNEL_1, STREAM_ID_1, -1), 0);
    doWork();
    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 1u);

    aeron_receive_channel_endpoint_t *endpoint =
        aeron_driver_conductor_find_receive_channel_endpoint(&m_conductor.m_conductor, CHANNEL_1);

    createPublicationImage(endpoint, STREAM_ID_2, 1000);

    EXPECT_EQ(aeron_driver_conductor_num_images(&m_conductor.m_conductor), 0u);

    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 0u);
}

TEST_F(DriverConductorTest, shouldRemoveSubscriptionFromImageWhenRemoveSubscription)
{
    int64_t client_id = nextCorrelationId();
    int64_t sub_id = nextCorrelationId();

    ASSERT_EQ(addNetworkSubscription(client_id, sub_id, CHANNEL_1, STREAM_ID_1, -1), 0);
    doWork();
    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 1u);

    aeron_receive_channel_endpoint_t *endpoint =
        aeron_driver_conductor_find_receive_channel_endpoint(&m_conductor.m_conductor, CHANNEL_1);

    createPublicationImage(endpoint, STREAM_ID_1, 1000);

    EXPECT_EQ(aeron_driver_conductor_num_images(&m_conductor.m_conductor), 1u);

    aeron_publication_image_t *image =
        aeron_driver_conductor_find_publication_image(&m_conductor.m_conductor, endpoint, STREAM_ID_1);

    EXPECT_NE(image, (aeron_publication_image_t *)NULL);
    EXPECT_EQ(aeron_publication_image_num_subscriptions(image), 1u);

    int64_t remove_correlation_id = nextCorrelationId();
    ASSERT_EQ(removeSubscription(client_id, remove_correlation_id, sub_id), 0);
    doWork();

    EXPECT_EQ(aeron_publication_image_num_subscriptions(image), 0u);

    EXPECT_EQ(readAllBroadcastsFromConductor(null_handler), 2u);
}
