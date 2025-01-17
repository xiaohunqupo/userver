#pragma once

#include <userver/components/component_context.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/engine/sleep.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/server/handlers/json_error_builder.hpp>
#include <userver/server/http/http_response_body_stream.hpp>
#include <userver/testsuite/testpoint.hpp>
#include <userver/utest/using_namespace_userver.hpp>

namespace chaos {

class HttpServerHandler final : public server::handlers::HttpHandlerBase {
public:
    static constexpr std::string_view kName = "handler-chaos-httpserver";

    static inline const std::string kDefaultAnswer = "OK!";

    HttpServerHandler(const components::ComponentConfig& config, const components::ComponentContext& context)
        : HttpHandlerBase(config, context) {}

    std::string HandleRequestThrow(const server::http::HttpRequest& request, server::request::RequestContext&)
        const override {
        const auto& type = request.GetArg("type");

        if (type == "common") {
            TESTPOINT("testpoint_request", {});
            return kDefaultAnswer;
        }

        if (type == "sleep") {
            TESTPOINT("testpoint_request", {});
            engine::SleepFor(std::chrono::milliseconds{200});
            return kDefaultAnswer;
        }

        if (type == "cancel") {
            engine::InterruptibleSleepFor(std::chrono::seconds(20));
            if (engine::current_task::IsCancelRequested()) {
                engine::TaskCancellationBlocker block_cancel;
                TESTPOINT("testpoint_cancel", {});
            }
            return kDefaultAnswer;
        }

        if (type == "echo") {
            UASSERT_MSG(!request.IsBodyCompressed(), "Body was not decompressed by userver");
            return request.RequestBody();
        }

        if (type == "echo-and-check-args") {
            UASSERT_MSG(!request.IsBodyCompressed(), "Body was not decompressed by userver");
            if (request.GetArg("srv") != "mt-dev") throw std::runtime_error("Failed arg 'srv'");
            if (request.GetArg("lang") != "en-ru") throw std::runtime_error("Failed arg 'lang'");
            return request.RequestBody();
        }

        UINVARIANT(false, "Unexpected request type");
    }

    server::handlers::FormattedErrorData GetFormattedExternalErrorBody(
        const server::handlers::CustomHandlerException& exc
    ) const override {
        server::handlers::FormattedErrorData result;
        result.external_body = server::handlers::JsonErrorBuilder{exc}.GetExternalBody();
        result.content_type = server::handlers::JsonErrorBuilder::GetContentType();
        return result;
    }
};

}  // namespace chaos
