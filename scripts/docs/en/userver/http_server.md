# HTTP, HTTPS, WebSocket

**Quality:**
* HTTP 1.x - @ref QUALITY_TIERS "Platinum Tier".
* HTTPS 1.x - @ref QUALITY_TIERS "Golden Tier".
* WebSocket - @ref QUALITY_TIERS "Golden Tier".
* HTTP 2.0 - @ref QUALITY_TIERS "Silver Tier".

## Introduction

🐙 **userver** implements HTTP/HTTPS 1.1, HTTP 2.0 and WebSocket server in
`userver-core` library using @ref components::Server component.

## Capabilities

* HTTP 1.1/1.0 support;
* HTTPS;
* @ref scripts/docs/en/userver/tutorial/websocket_service.md "WebSocket";
* Body decompression with "Content-Encoding: gzip";
* HTTP pipelining;
* Custom authorization @ref scripts/docs/en/userver/tutorial/auth_postgres.md ;
* Rate limiting via Congestion control and indiviadual handlers configuration;
* Requests-in-flight limiting;
* Requests-in-flight inspection via server::handlers::InspectRequests ;
* Body size / headers count / URL length / etc. limits;
* Streaming;
* @ref scripts/docs/en/userver/tutorial/multipart_service.md "File uploads and multipart/form-data"
* @ref scripts/docs/en/userver/deadline_propagation.md .
* @ref scripts/docs/en/userver/http_server_middlewares.md "Middlewares"

## Streaming API

Interactive clients (e.g. web browser) might want to get at least first bytes of
the HTTP response if the whole HTTP response is generated slowly. In this case
the HTTP handler might want to use Streaming API and return HTTP response body
as a byte stream rather than as a single-part blob.

To enable Streaming API in your handler:

1) define HandleStreamRequest() method:
```cpp
  #include <userver/server/http/http_response_body_stream_fwd.hpp>
  ...
    void HandleStreamRequest(server::http::HttpRequest&,
                             server::request::RequestContext&,
                             server::http::ResponseBodyStream&) const override;
```

2) Enable Streaming API in static config:
```yaml
components_manager:
    components:
        handler-stream-api:
            response-body-stream: true
```

3) Set dynamic config @ref USERVER_HANDLER_STREAM_API_ENABLED.

4) Write your handler code:

@snippet core/functional_tests/basic_chaos/httpclient_handlers.hpp HandleStreamRequest


### HTTP version

The HTTP server in userver supports versions `1.1` and `2.0`. The default version is `1.1`.
You can enable version `2.0` in the static config such as:
```
# yaml
components_manager:
    components:
        # ... other components
        server:
            listener:
                port: 8080
                task_processor: main-task-processor
                connection:
                    http-version: '2' # enum `1.1` or `2`
                    http2-session:
                        max_concurrent_streams: 100
                        max_frame_size: 16384
                        initial_window_size: 65536
```
You can set some options specific to `HTTP/2.0` in the `http2-session` section. See docs for these options in components::Server


## Components

* @ref components::Server "Server"
