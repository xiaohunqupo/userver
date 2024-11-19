#include <server/http/http_request_impl_builder.hpp>

#include <userver/http/common_headers.hpp>

USERVER_NAMESPACE_BEGIN

namespace server::http {

namespace {

inline void Strip(const char*& begin, const char*& end) {
    while (begin < end && isspace(*begin)) ++begin;
    while (begin < end && isspace(end[-1])) --end;
}

}  // namespace

HttpRequestImplBuilder::HttpRequestImplBuilder(
    request::ResponseDataAccounter& data_accounter,
    engine::io::Sockaddr remote_address
)
    : request_(std::make_shared<HttpRequestImpl>(data_accounter, std::move(remote_address))) {}

HttpRequestImplBuilder& HttpRequestImplBuilder::SetMethod(HttpMethod method) {
    request_->method_ = method;
    return *this;
}

HttpRequestImplBuilder& HttpRequestImplBuilder::SetHttpMajor(unsigned short http_major) {
    request_->http_major_ = http_major;
    return *this;
}

HttpRequestImplBuilder& HttpRequestImplBuilder::SetHttpMinor(unsigned short http_minor) {
    request_->http_minor_ = http_minor;
    return *this;
}

HttpRequestImplBuilder& HttpRequestImplBuilder::SetBody(std::string&& body) {
    request_->SetRequestBody(std::move(body));
    return *this;
}

HttpRequestImplBuilder& HttpRequestImplBuilder::AddHeader(std::string&& header, std::string&& value) {
    request_->headers_.InsertOrAppend(std::move(header), std::move(value));
    return *this;
}

HttpRequestImplBuilder& HttpRequestImplBuilder::AddRequestArg(std::string&& key, std::string&& value) {
    request_->request_args_[std::move(key)].push_back(std::move(value));
    return *this;
}

HttpRequestImplBuilder& HttpRequestImplBuilder::SetPathArgs(std::vector<std::pair<std::string, std::string>> args) {
    request_->SetPathArgs(std::move(args));
    return *this;
}

HttpRequestImplBuilder& HttpRequestImplBuilder::SetUrl(std::string&& url) {
    request_->url_ = std::move(url);
    return *this;
}

HttpRequestImplBuilder& HttpRequestImplBuilder::SetRequestPath(std::string&& path) {
    request_->request_path_ = std::move(path);
    return *this;
}

HttpRequestImplBuilder& HttpRequestImplBuilder::SetIsFinal(bool is_final) {
    request_->is_final_ = is_final;
    return *this;
}

HttpRequestImplBuilder& HttpRequestImplBuilder::SetFormDataArgs(
    utils::impl::TransparentMap<std::string, std::vector<FormDataArg>, utils::StrCaseHash>&& form_data_args
) {
    request_->form_data_args_ = std::move(form_data_args);
    return *this;
}

HttpRequestImplBuilder& HttpRequestImplBuilder::SetHttpHandler(const handlers::HttpHandlerBase& handler) {
    request_->SetHttpHandler(handler);
    return *this;
}

HttpRequestImplBuilder& HttpRequestImplBuilder::SetTaskProcessor(engine::TaskProcessor& task_processor) {
    request_->SetTaskProcessor(task_processor);
    return *this;
}

HttpRequestImplBuilder& HttpRequestImplBuilder::SetHttpHandlerStatistics(handlers::HttpRequestStatistics& stats) {
    request_->SetHttpHandlerStatistics(stats);
    return *this;
}

HttpRequestImplBuilder& HttpRequestImplBuilder::SetStreamProducer(impl::Http2StreamEventProducer&& producer) {
    request_->SetStreamProducer(std::move(producer));
    return *this;
}

HttpRequestImplBuilder& HttpRequestImplBuilder::SetResponseStreamId(std::int32_t stream_id) {
    request_->SetResponseStreamId(stream_id);
    return *this;
}

HttpRequestImplBuilder& HttpRequestImplBuilder::SetResponseStatus(HttpStatus status) {
    request_->SetResponseStatus(status);
    return *this;
}

const HttpRequestImpl& HttpRequestImplBuilder::GetRef() const {
    UASSERT(request_);
    return *request_;
}

HttpResponse& HttpRequestImplBuilder::GetHttpResponse() { return request_->GetHttpResponse(); }

std::shared_ptr<HttpRequestImpl> HttpRequestImplBuilder::Build() {
    ParseCookies();

    UASSERT(std::all_of(request_->request_args_.begin(), request_->request_args_.end(), [](const auto& arg) {
        return !arg.second.empty();
    }));

    LOG_TRACE() << "method=" << request_->GetMethodStr();
    LOG_TRACE() << "request_args:" << request_->request_args_;
    LOG_TRACE() << "headers:" << request_->headers_;
    LOG_TRACE() << "cookies:" << request_->cookies_;

    return request_;
}

void HttpRequestImplBuilder::ParseCookies() {
    const std::string& cookie = request_->GetHeader(USERVER_NAMESPACE::http::headers::kCookie);
    const char* data = cookie.data();
    size_t size = cookie.size();
    const char* end = data + size;
    const char* key_begin = data;
    const char* key_end = data;
    bool parse_key = true;
    for (const char* ptr = data; ptr <= end; ++ptr) {
        if (ptr == end || *ptr == ';') {
            const char* value_begin = nullptr;
            const char* value_end = nullptr;
            if (parse_key) {
                key_end = ptr;
                value_begin = value_end = ptr;
            } else {
                value_begin = key_end + 1;
                value_end = ptr;
                Strip(value_begin, value_end);
                if (value_begin + 2 <= value_end && *value_begin == '"' && value_end[-1] == '"') {
                    ++value_begin;
                    --value_end;
                }
            }
            Strip(key_begin, key_end);
            if (key_begin < key_end) {
                request_->cookies_.emplace(
                    std::piecewise_construct, std::tie(key_begin, key_end), std::tie(value_begin, value_end)
                );
            }
            parse_key = true;
            key_begin = ptr + 1;
            continue;
        }
        if (*ptr == '=' && parse_key) {
            parse_key = false;
            key_end = ptr;
            continue;
        }
    }
}

}  // namespace server::http

USERVER_NAMESPACE_END
