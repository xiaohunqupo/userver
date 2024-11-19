#pragma once

#include "http_request_impl.hpp"

#include <userver/logging/log.hpp>

USERVER_NAMESPACE_BEGIN

namespace server::http {

class HttpRequestImplBuilder final {
public:
    HttpRequestImplBuilder(request::ResponseDataAccounter& data_accounter, engine::io::Sockaddr remote_address);

    HttpRequestImplBuilder& SetMethod(HttpMethod method);

    HttpRequestImplBuilder& SetHttpMajor(unsigned short http_major);

    HttpRequestImplBuilder& SetHttpMinor(unsigned short http_minor);

    HttpRequestImplBuilder& SetBody(std::string&& body);

    HttpRequestImplBuilder& AddHeader(std::string&& header, std::string&& value);

    HttpRequestImplBuilder& AddRequestArg(std::string&& key, std::string&& value);

    HttpRequestImplBuilder& SetPathArgs(std::vector<std::pair<std::string, std::string>> args);

    HttpRequestImplBuilder& SetUrl(std::string&& url);

    HttpRequestImplBuilder& SetRequestPath(std::string&& path);

    HttpRequestImplBuilder& SetIsFinal(bool is_final);

    HttpRequestImplBuilder& SetFormDataArgs(
        utils::impl::TransparentMap<std::string, std::vector<FormDataArg>, utils::StrCaseHash>&& form_data_args
    );

    HttpRequestImplBuilder& SetHttpHandler(const handlers::HttpHandlerBase& handler);

    HttpRequestImplBuilder& SetTaskProcessor(engine::TaskProcessor& task_processor);

    HttpRequestImplBuilder& SetHttpHandlerStatistics(handlers::HttpRequestStatistics& stats);

    // TODO: remove
    HttpRequestImplBuilder& SetStreamProducer(impl::Http2StreamEventProducer&& producer);

    // TODO: remove
    HttpRequestImplBuilder& SetResponseStreamId(std::int32_t stream_id);

    HttpRequestImplBuilder& SetResponseStatus(HttpStatus status);

    const HttpRequestImpl& GetRef() const;

    HttpResponse& GetHttpResponse();

    std::shared_ptr<HttpRequestImpl> Build();

private:
    void ParseCookies();

    std::shared_ptr<HttpRequestImpl> request_;
};

}  // namespace server::http

USERVER_NAMESPACE_END
