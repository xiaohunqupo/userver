## Production configs and best practices

A good production ready service should have functionality for various cases:
* Overload
  * Service should respond with HTTP 429 codes to some requests while still being able to handle the rest
* Debugging of a running service
  * inspect logs
  * get more logs from the suspiciously behaving service and then turn the logging level back
  * profile memory usage
  * see requests in flight
* Experiments
  * Should be a way to turn on/off arbitrary functionality without restarting the service
* Metrics and Logs
* Functional testing

This tutorial shows a configuration of a typical production ready service. For
information about service interactions with other utilities and services in
container see @ref scripts/docs/en/userver/deploy_env.md.


## Before you start

Make sure that you can compile and run core tests and read a basic example @ref scripts/docs/en/userver/tutorial/hello_service.md.

## int main

utils::DaemonMain initializes and starts the component system with the provided command line arguments:

@snippet samples/production_service/production_service.cpp Production service sample - main

A path to the static config file should be passed from a command line to start the service:

```
bash
./samples/userver-samples-production_service --config /etc/production-service/static_config.yaml
```

## Static config

Full static config could be seen at @ref samples/production_service/static_config.yaml

Important parts are described down below.

### Variables

Static configs tend to become quite big, so it is a good idea to move changing parts of it into variables. To do that,
declare a `config_vars` field in the static config and point it to a file with variables.

@snippet samples/production_service/static_config.yaml Production service sample - static config - config vars

A file with config variables could look @ref samples/production_service/config_vars.yaml "like this". 

Now in static config you could use `$variable-name` to refer to a variable, 
`*#fallback` fields are used if there is no variable with such name in the config variables file:

@snippet samples/production_service/static_config.yaml Production service sample - static config variables usage


### Task processors

A good practice is to have at least 3 different task processors:

@snippet samples/production_service/static_config.yaml Production service sample - static config task processors

Moving blocking operations into a separate task processor improves responsiveness and CPU usage of your service. Monitor task processor
helps to get statistics and diagnostics from server under heavy load or from a server with a deadlocked threads in the main task processor.

@warning This setup is for an abstract service on an abstract 8 core machine. Benchmark your service on your hardware and hand-tune the
thread numbers to get optimal performance.


### Listeners/Monitors

Note the components::Server configuration:

@snippet samples/production_service/static_config.yaml Production service sample - static config server listen

In this example we have two listeners. it is done to separate clients and utility/diagnostic handlers to listen on different ports or even interfaces.


@anchor sample_prod_service_utility_handlers
### Utility handlers

Your server has the following utility handlers:
* to @ref scripts/docs/en/userver/requests_in_flight.md "inspect in-flight request" - server::handlers::InspectRequests
* to @ref scripts/docs/en/userver/memory_profile_running_service.md "profile memory usage" - server::handlers::Jemalloc
* to @ref scripts/docs/en/userver/log_level_running_service.md "change logging level at runtime" - server::handlers::LogLevel
  and server::handlers::DynamicDebugLog
* to reopen log files after log rotation (you can also use @ref scripts/docs/en/userver/os_signals.md "signals") - server::handlers::OnLogRotate 
* to @ref scripts/docs/en/userver/dns_control.md "control the DNS resolver" - server::handlers::DnsClientControl
* to @ref scripts/docs/en/userver/service_monitor.md "get statistics" from the service - server::handlers::ServerMonitor

@snippet samples/production_service/static_config.yaml Production service sample - static config utility handlers

All those handlers live on a separate `components.server.listener-monitor`
address, so you have to request them using the `listener-monitor` credentials:

```
bash
$ curl http://localhost:8085/service/log-level/
{"init-log-level":"info","current-log-level":"info"}

$ curl -X PUT 'http://localhost:8085/service/log-level/warning'
{"init-log-level":"info","current-log-level":"warning"}
```


### Ping

This is a server::handlers::Ping handle that returns 200 if the service is OK, 500 otherwise. Useful for balancers,
that would stop sending traffic to the server if it responds with codes other than 200.

@snippet samples/production_service/static_config.yaml Production service sample - static config ping

Note that the ping handler lives on the task processor of all the other handlers. Smart balancers may measure response
times and send less traffic to the heavy loaded services. 

```
bash
$ curl --unix-socket service.socket http://localhost/ping -i
HTTP/1.1 200 OK
Date: Thu, 01 Jul 2021 12:46:07 UTC
Content-Type: text/html; charset=utf-8
X-YaRequestId: 39e3f54b86984b8ca5235876dc566b27
Server: sample-production-service 1.0
X-YaTraceId: 4d7f8aa03e2d4e4d80a92a3ccecfbe6d
Connection: keep-alive
Content-Length: 0

```


### Dynamic configs of a sample production service

Here's a configuration of a dynamic config related components
components::DynamicConfigClient, components::DynamicConfig,
components::DynamicConfigClientUpdater.

Service starts with some dynamic config values from defaults and updates dynamic values from a
@ref scripts/docs/en/userver/tutorial/config_service.md "configs service"
at startup. If the first update fails, the values are retrieved from `dynamic-config.fs-cache-path`
file (if it exists).

@snippet samples/production_service/static_config.yaml Production service sample - static config dynamic configs

@note Dynamic configs is an essential part of a reliable service with high
      availability. Those could be used as an emergency switch for new
      functionality, selector for experiments, limits/timeouts/log-level setup,
      proxy setup. See @ref scripts/docs/en/schemas/dynamic_configs.md for
      more info and @ref scripts/docs/en/userver/tutorial/config_service.md for
      insights on how to implement such service.


### Congestion Control

See @ref scripts/docs/en/userver/congestion_control.md.

congestion_control::Component limits the active requests count. In case of overload it responds with HTTP 429 codes to
some requests, allowing your service to properly process handle the rest.

All the significant parts of the component are configured by dynamic config options @ref USERVER_RPS_CCONTROL and
@ref USERVER_RPS_CCONTROL_ENABLED.

@snippet samples/production_service/static_config.yaml Production service sample - static config congestion-control

It is a good idea to disable it in unit tests to avoid getting `HTTP 429` on an overloaded CI server.


@anchor tutorial_metrics
### Metrics

Metrics is a convenient way to monitor the health of your service.

Typical setup of components::SystemStatisticsCollector and components::StatisticsStorage is quite trivial:

@snippet samples/production_service/static_config.yaml Production service sample - static config metrics

With such setup you could poll the metrics from
handler server::handlers::ServerMonitor that we've configured in
@ref sample_prod_service_utility_handlers "previous section". However
a much more mature approach is to write a component that pushes the metrics
directly into the remote metrics aggregation service or to write
a handler that provides the metrics in the native aggregation service format.

To produce metrics in declarative style use the utils::statistics::MetricTag
or register your metrics writer in utils::statistics::Storage via RegisterWriter
to produce metrics on per component basis. To test metrics refer to
the @ref TESTSUITE_METRICS_TESTING "testsuite metrics testing".

List of userver built-in metrics could be found at
@ref scripts/docs/en/userver/service_monitor.md.

# Alerts

Alerts is a way to propagate critical errors from your service to a monitoring system.

When the code identifies that something bad happened and a user should be notified about that,
`alert_storage.FireAlert()` is called with the appropriate arguments. Then the alert subsystem
notifies an external monitoring system (or a user) about the alert event though the specific HTTP handler.


### Secdist - secrets distributor

Storing sensitive data aside from the configs is a good practice that allows you to set different access rights for the two files.

components::Secdist configuration is straightforward:

@snippet samples/production_service/static_config.yaml Production service sample - static config secdist

Refer to the storages::secdist::SecdistConfig config for more information on the data retrieval. 


@anchor prod_service_testsuite_related_components
### Testsuite related components

server::handlers::TestsControl is a handle that allows controlling the service
from test environments. That handle is used by the testsuite from
@ref scripts/docs/en/userver/functional_testing.md "functional tests" to mock time,
invalidate caches, testpoints and many other things. This component should be
disabled in production environments.

components::TestsuiteSupport is a lightweight storage to keep minor testsuite
data. This component is required by many high-level components and it is safe to
use this component in production environments.


### Build

This sample requires @ref scripts/docs/en/userver/tutorial/config_service.md "configs service", so we build and start one from our previous tutorials.

```
bash
mkdir build_release
cd build_release
cmake -DCMAKE_BUILD_TYPE=Release ..

make userver-samples-config_service
./samples/userver-samples-config_service &

make userver-samples-production_service
python3 ../samples/tests/prepare_production_configs.py
./samples/userver-samples-production_service --config /tmp/userver/production_service/static_config.yaml
```


### Functional testing
@ref scripts/docs/en/userver/functional_testing.md "Functional tests" are used to make sure
that the service is working fine and
implements the required functionality. A recommended practice is to build the
service in Debug and Release modes and tests both of them, then deploy the
Release build to the production, @ref "disabling all the tests related handlers".

Debug builds of the userver provide numerous assertions that validate the
framework usage and help to detect bugs at early stages.

Typical functional tests for a service consist of a `conftest.py` file with
mocks+configs for the sereffectivelyvice and a bunch of `test_*.py` files with actual
tests. Such approach allows to reuse mocks and configurations in different
tests.

## Full sources

See the full example at 
* @ref samples/production_service/production_service.cpp
* @ref samples/production_service/static_config.yaml
* @ref samples/production_service/config_vars.yaml
* @ref samples/production_service/CMakeLists.txt
* @ref samples/production_service/tests/conftest.py
* @ref samples/production_service/tests/test_ping.py
* @ref samples/production_service/tests/test_production.py

----------

@htmlonly <div class="bottom-nav"> @endhtmlonly
⇦ @ref scripts/docs/en/userver/tutorial/config_service.md | @ref scripts/docs/en/userver/tutorial/tcp_service.md ⇨
@htmlonly </div> @endhtmlonly

@example samples/production_service/production_service.cpp
@example samples/production_service/static_config.yaml
@example samples/production_service/config_vars.yaml
@example samples/production_service/CMakeLists.txt
@example samples/production_service/tests/conftest.py
@example samples/production_service/tests/test_ping.py
@example samples/production_service/tests/test_production.py
