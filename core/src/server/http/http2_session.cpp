#include <server/http/http2_session.hpp>

#include <server/http/http_request_parser.hpp>
#include <server/net/connection_config.hpp>

#include <userver/crypto/base64.hpp>
#include <userver/engine/io/socket.hpp>
#include <userver/http/common_headers.hpp>
#include <userver/utils/fast_scope_guard.hpp>

USERVER_NAMESPACE_BEGIN

namespace server::http {

namespace {

constexpr std::size_t kFrameHeaderSize = 9;

void ThrowIfErr(int error_code, std::string_view msg) {
    if (error_code != 0) {
        throw std::runtime_error{fmt::format("{}: {}", msg, nghttp2_strerror(error_code))};
    }
}

Http2Session& GetParser(void* user_data) {
    UASSERT(user_data);
    return *static_cast<Http2Session*>(user_data);
}

void IncStat(utils::statistics::RateCounter& stat) { stat.AddAsSingleProducer(utils::statistics::Rate{1}); }

std::string_view ToStringView(const std::uint8_t* data, std::size_t size) {
    return {reinterpret_cast<const char*>(data), size};
}

}  // namespace

Http2Session::Http2Session(
    const HandlerInfoIndex& handler_info_index,
    const request::HttpRequestConfig& request_config,
    const net::Http2SessionConfig& config,
    OnNewRequestCb&& on_new_request_cb,
    net::ParserStats& stats,
    request::ResponseDataAccounter& data_accounter,
    engine::io::Sockaddr remote_address,
    engine::io::RwBase* socket
)
    : config_(config),
      streams_pool_(config_.max_concurrent_streams),
      handler_info_index_(handler_info_index),
      request_constructor_config_(request_config),
      on_new_request_cb_(std::move(on_new_request_cb)),
      data_accounter_(data_accounter),
      stats_(stats),
      remote_address_(remote_address),
      socket_(socket),
      streaming_queue_(impl::Http2StreamEventQueue::Create()),
      streaming_consumer_(streaming_queue_->GetConsumer()) {
    UASSERT(streaming_queue_);
    UASSERT(streaming_event_.IsAutoReset());

    nghttp2_session_callbacks* callbacks{nullptr};
    UINVARIANT(nghttp2_session_callbacks_new(&callbacks) == 0, "Failed to init callbacks for HTTP/2.0");

    utils::FastScopeGuard delete_guard{[&callbacks]() noexcept { nghttp2_session_callbacks_del(callbacks); }};

    nghttp2_session_callbacks_set_send_callback(callbacks, OnSend);
    nghttp2_session_callbacks_set_send_data_callback(callbacks, OnDataFrameSend);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, OnFrameRecv);
    nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, OnStreamClose);
    nghttp2_session_callbacks_set_on_header_callback(callbacks, OnHeader);
    nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, OnBeginHeaders);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, OnDataChunkRecv);

    nghttp2_session* session{nullptr};
    UINVARIANT(nghttp2_session_server_new(&session, callbacks, this) == 0, "Failed to init session for HTTP/2.0");
    UASSERT(session);
    session_ = SessionPtr(session, nghttp2_session_del);

    std::array<nghttp2_settings_entry, 3> settings{
        nghttp2_settings_entry{NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, config.max_concurrent_streams},
        nghttp2_settings_entry{NGHTTP2_SETTINGS_MAX_FRAME_SIZE, config.max_frame_size},
        nghttp2_settings_entry{NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, config.initial_window_size}};

    auto rv = nghttp2_submit_settings(session_.get(), NGHTTP2_FLAG_NONE, settings.data(), settings.size());
    ThrowIfErr(rv, "Error when submit settings");
    rv = nghttp2_session_send(session_.get());
    ThrowIfErr(rv, "Error when session send");
}

int Http2Session::OnFrameRecv(nghttp2_session* session, const nghttp2_frame* frame, void* user_data) {
    UASSERT(session);
    UASSERT(frame);
    auto& parser = GetParser(user_data);

    switch (frame->hd.type) {
        case NGHTTP2_DATA:
        case NGHTTP2_HEADERS: {
            if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                auto& stream = parser.GetStreamChecked(Stream::Id{frame->hd.stream_id});
                try {
                    stream.RequestConstructor().AppendHeaderField("", 0);
                } catch (const std::exception& e) {
                    IncStat(parser.stats_.http2_stats.streams_parse_error);
                    LOG_LIMITED_WARNING() << "can't append header field: " << e;
                }
                parser.FinalizeRequest(stream);
            }
        } break;
        case NGHTTP2_RST_STREAM: {
            IncStat(parser.stats_.http2_stats.reset_streams);
        } break;
        case NGHTTP2_PING: {
            nghttp2_submit_ping(parser.session_.get(), NGHTTP2_FLAG_NONE, nullptr);
        } break;
        case NGHTTP2_GOAWAY: {
            IncStat(parser.stats_.http2_stats.goaway);
        } break;
    }
    return 0;
}

int Http2Session::OnHeader(
    nghttp2_session*,
    const nghttp2_frame* frame,
    const uint8_t* name,
    size_t namelen,
    const uint8_t* value,
    size_t valuelen,
    uint8_t,
    void* user_data
) {
    UASSERT(frame);
    UASSERT(frame->hd.type == NGHTTP2_HEADERS);

    if (frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
        return 0;
    }
    UASSERT(name);
    UASSERT(value);
    const std::string_view hname{ToStringView(name, namelen)};
    const std::string_view hvalue{ToStringView(value, valuelen)};

    auto& parser = GetParser(user_data);
    auto& stream = parser.GetStreamChecked(Stream::Id{frame->hd.stream_id});
    auto& ctor = stream.RequestConstructor();
    if (hname == USERVER_NAMESPACE::http::headers::k2::kMethod) {
        ctor.SetMethod(HttpMethodFromString(hvalue));
    } else if (hname == USERVER_NAMESPACE::http::headers::k2::kPath) {
        try {
            ctor.AppendUrl(hvalue.data(), hvalue.size());
            stream.CheckUrlComplete();
        } catch (const std::exception& e) {
            LOG_LIMITED_WARNING() << "can't append url: " << e;
            IncStat(parser.stats_.http2_stats.streams_parse_error);
        }
    } else {
        try {
            ctor.AppendHeaderField(hname.data(), hname.size());
            ctor.AppendHeaderValue(hvalue.data(), hvalue.size());
        } catch (const std::exception& e) {
            LOG_LIMITED_WARNING() << "can't append header field: " << e;
            IncStat(parser.stats_.http2_stats.streams_parse_error);
        }
    }
    return 0;
}

int Http2Session::OnStreamClose(nghttp2_session*, int32_t id, uint32_t error_code, void* user_data) {
    auto& parser = GetParser(user_data);
    parser.RemoveStream(parser.GetStreamChecked(Stream::Id{id}));

    IncStat(parser.stats_.http2_stats.streams_close);
    LOG_LIMITED_TRACE() << fmt::format("The stream {} was closed with code {}", id, error_code);

    return 0;
}

int Http2Session::OnBeginHeaders(nghttp2_session*, const nghttp2_frame* frame, void* user_data) {
    UASSERT(frame);
    if (frame->hd.type != NGHTTP2_HEADERS || frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
        return 0;
    }

    auto& parser = GetParser(user_data);
    parser.RegisterStream(Stream::Id{frame->hd.stream_id});

    return 0;
}

int Http2Session::OnDataChunkRecv(
    nghttp2_session*,
    uint8_t,
    int32_t id,
    const uint8_t* data,
    size_t len,
    void* user_data
) {
    auto& parser = GetParser(user_data);
    auto& stream = parser.GetStreamChecked(Stream::Id{id});
    try {
        stream.RequestConstructor().AppendBody(reinterpret_cast<const char*>(data), len);
    } catch (const std::exception& e) {
        LOG_LIMITED_WARNING() << "can't append body: " << e;
    }
    return 0;
}

long Http2Session::OnSend(nghttp2_session* session, const uint8_t* data, size_t len, int, void* user_data) {
    UASSERT(session);
    UASSERT(data);
    auto& parser = GetParser(user_data);
    if (parser.socket_ != nullptr) {
        return parser.socket_->WriteAll(data, len, {});
    }
    return NGHTTP2_ERR_WOULDBLOCK;
}

int Http2Session::OnDataFrameSend(
    nghttp2_session*,
    nghttp2_frame* frame,
    const uint8_t* framehd,
    size_t max_len,
    nghttp2_data_source* source,
    void* user_data
) {
    UASSERT(frame);
    UASSERT(framehd);
    UASSERT(source);
    UASSERT(source->ptr);
    UASSERT(frame->data.padlen == 0);

    auto& parser = GetParser(user_data);
    UASSERT(parser.socket_);
    auto& stream = *static_cast<Stream*>(source->ptr);

    const auto frame_header{ToStringView(framehd, kFrameHeaderSize)};
    // TODO: doesn't work with TLS?!
    UASSERT(dynamic_cast<engine::io::Socket*>(parser.socket_));
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    stream.Send(*static_cast<engine::io::Socket*>(parser.socket_), frame_header, max_len);
    return 0;
}

void Http2Session::RegisterStream(Stream::Id id) {
    auto* stream_ptr = streams_pool_.malloc();
    if (stream_ptr == nullptr) {
        SubmitRstStream(id);
        return;
    }
    utils::FastScopeGuard guard_free{[this, stream_ptr]() noexcept { streams_pool_.free(stream_ptr); }};

    new (stream_ptr) Stream(request_constructor_config_, handler_info_index_, data_accounter_, remote_address_, id);
    guard_free.Release();

    utils::FastScopeGuard guard_destroy{[this, stream_ptr]() noexcept { streams_pool_.destroy(stream_ptr); }};

    const int res = nghttp2_session_set_stream_user_data(session_.get(), static_cast<std::int32_t>(id), &*stream_ptr);
    if (res != 0) {
        throw std::runtime_error{fmt::format("Cannot to set stream user data: {}", nghttp2_strerror(res))};
    }

    stats_.parsing_request_count.Add(1);
    IncStat(stats_.http2_stats.streams_count);

    guard_destroy.Release();
}

void Http2Session::RemoveStream(Stream& stream) {
    UASSERT(streams_pool_.is_from(&stream));
    const auto id = stream.GetId();
    streams_pool_.destroy(&stream);

    const int res = nghttp2_session_set_stream_user_data(session_.get(), static_cast<std::int32_t>(id), nullptr);
    // assert because the stream exists on the previous step
    UASSERT(res == 0);
    stats_.parsing_request_count.Subtract(1);
}

Stream& Http2Session::GetStreamChecked(Stream::Id id) {
    auto* stream =
        static_cast<Stream*>(nghttp2_session_get_stream_user_data(session_.get(), static_cast<std::int32_t>(id)));
    if (stream == nullptr) {
        throw std::runtime_error{fmt::format("The stream {} does not exist", id)};
    }
    return *stream;
}

void Http2Session::SubmitRstStream(Stream::Id id) {
    IncStat(stats_.http2_stats.reset_streams);
    UASSERT(id != Stream::Id{0});
    const auto res = nghttp2_submit_rst_stream(
        session_.get(), NGHTTP2_FLAG_NONE, static_cast<std::int32_t>(id), NGHTTP2_INTERNAL_ERROR
    );
    UASSERT(res == 0);
}

bool Http2Session::Parse(std::string_view req) {
    const int readlen =
        nghttp2_session_mem_recv(session_.get(), reinterpret_cast<const uint8_t*>(req.data()), req.size());
    if (readlen < 0) {
        LOG_LIMITED_ERROR() << fmt::format("Error in Parse: {}", nghttp2_strerror(readlen));
        return false;
    }
    if (static_cast<std::size_t>(readlen) != req.size()) {
        LOG_LIMITED_ERROR() << fmt::format("Parsed = {} but expected {}", readlen, req.size());
        return false;
    }
    if (socket_ != nullptr) {
        WriteWhileWant();
    }
    return ConnectionIsOk();
}

void Http2Session::UpgradeToHttp2(std::string_view client_magic) {
    const auto settings_payload = crypto::base64::Base64UrlDecode(client_magic);
    const auto rv = nghttp2_session_upgrade2(
        session_.get(), reinterpret_cast<const uint8_t*>(settings_payload.data()), settings_payload.size(), 0, nullptr
    );
    ThrowIfErr(rv, "Error during upgrade");
    RegisterStream(kStreamIdAfterUpgradeResponse);
}

void Http2Session::FinalizeRequest(Stream& stream) {
    if (!stream.CheckUrlComplete()) {
        IncStat(stats_.http2_stats.streams_parse_error);
        SubmitRstStream(stream.GetId());
        RemoveStream(stream);
        return;
    }
    stream.RequestConstructor().SetResponseStreamId(static_cast<std::int32_t>(stream.GetId()));
    stream.RequestConstructor().SetStreamProducer(impl::Http2StreamEventProducer{*streaming_queue_, streaming_event_});
    if (auto request = stream.RequestConstructor().Finalize()) {
        on_new_request_cb_(std::move(request));
    } else {
        IncStat(stats_.http2_stats.streams_parse_error);
        SubmitRstStream(stream.GetId());
        RemoveStream(stream);
    }
}

bool Http2Session::ConnectionIsOk() {
    return nghttp2_session_want_read(session_.get()) != 0 || nghttp2_session_want_write(session_.get()) != 0;
}

void Http2Session::WriteWhileWant() {
    auto* session = session_.get();
    while (nghttp2_session_want_write(session)) {
        const auto res = nghttp2_session_send(session);
        ThrowIfErr(res, "Error while nghttp2_session_send");
    }
}

engine::SingleConsumerEvent& Http2Session::GetStreamingEvent() { return streaming_event_; }

void Http2Session::HandleStreamingEvents() {
    impl::Http2StreamEvent event;
    while (streaming_consumer_.PopNoblock(event)) {
        UASSERT(event.stream_id != -1);
        auto& stream = GetStreamChecked(Stream::Id{event.stream_id});
        if (stream.IsDeferred()) {
            const auto res = nghttp2_session_resume_data(session_.get(), static_cast<std::int32_t>(stream.GetId()));
            ThrowIfErr(res, "Error while resume_data");
            stream.SetDeferred(false);
        }
        stream.PushChunk(std::move(event.body_part));
        stream.SetEnd(event.is_end);
        event = {};
    }
    WriteWhileWant();
}

}  // namespace server::http

USERVER_NAMESPACE_END
