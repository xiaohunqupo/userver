#pragma once

/// @file userver/rabbitmq.hpp
/// This file is mainly for documentation purposes and inclusion of all headers
/// that are required for working with RabbitMQ userver component.

#include <userver/urabbitmq/admin_channel.hpp>
#include <userver/urabbitmq/broker_interface.hpp>
#include <userver/urabbitmq/channel.hpp>
#include <userver/urabbitmq/client.hpp>
#include <userver/urabbitmq/client_settings.hpp>
#include <userver/urabbitmq/component.hpp>
#include <userver/urabbitmq/consumer_base.hpp>
#include <userver/urabbitmq/consumer_component_base.hpp>
#include <userver/urabbitmq/consumer_settings.hpp>
#include <userver/urabbitmq/typedefs.hpp>

/// @page rabbitmq_driver RabbitMQ (AMQP 0-9-1)
///
/// **Quality:** @ref QUALITY_TIERS "Golden Tier".
///
/// 🐙 **userver** provides access to RabbitMQ servers via
/// components::RabbitMQ. The uRabbitMQ driver is asynchronous, it suspends
/// current coroutine for carrying out network I/O.
///
/// @section feature Features
/// - Publishing messages;
/// - Consuming messages;
/// - Creating Exchanges, Queues and Bindings;
/// - Transport level security;
/// - Connections pooling;
/// - End-to-end logging for messages in publish->consume chain.
///
/// @section info More information
/// - For configuration see components::RabbitMQ
/// - For cluster operations see urabbitmq::Client, urabbitmq::AdminChannel,
///   urabbitmq::Channel, urabbitmq::ReliableChannel
/// - For consumers support see urabbitmq::ConsumerBase and
///   urabbitmq::ConsumerComponentBase
///
/// ----------
///
/// @htmlonly <div class="bottom-nav"> @endhtmlonly
/// ⇦ @ref scripts/docs/en/userver/grpc.md |
/// @ref scripts/docs/en/userver/dynamic_config.md ⇨
/// @htmlonly </div> @endhtmlonly

USERVER_NAMESPACE_BEGIN

namespace urabbitmq {
/// @brief top namespace for uRabbitMQ driver.
///
/// For more information see @ref rabbitmq_driver.
}

USERVER_NAMESPACE_END
