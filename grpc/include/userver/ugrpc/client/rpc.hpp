#pragma once

/// @file userver/ugrpc/client/rpc.hpp
/// @brief Classes representing an outgoing RPC

#include <exception>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

#include <grpcpp/impl/codegen/proto_utils.h>

#include <userver/dynamic_config/snapshot.hpp>
#include <userver/engine/deadline.hpp>
#include <userver/engine/future_status.hpp>
#include <userver/utils/assert.hpp>
#include <userver/utils/function_ref.hpp>

#include <userver/ugrpc/client/exceptions.hpp>
#include <userver/ugrpc/client/impl/async_methods.hpp>
#include <userver/ugrpc/client/impl/call_params.hpp>
#include <userver/ugrpc/client/middlewares/fwd.hpp>
#include <userver/ugrpc/impl/deadline_timepoint.hpp>
#include <userver/ugrpc/impl/internal_tag_fwd.hpp>
#include <userver/ugrpc/impl/statistics_scope.hpp>

USERVER_NAMESPACE_BEGIN

namespace ugrpc::client {

namespace impl {

struct MiddlewarePipeline {
    static void PreStartCall(impl::RpcData& data);

    static void PreSendMessage(impl::RpcData& data, const google::protobuf::Message& message);
    static void PostRecvMessage(impl::RpcData& data, const google::protobuf::Message& message);

    static void PostFinish(impl::RpcData& data, const grpc::Status& status);
};

/// @brief UnaryFuture for waiting a single response RPC
class [[nodiscard]] UnaryFuture {
public:
    /// @cond
    UnaryFuture(
        impl::RpcData& data,
        std::function<void(impl::RpcData& data, const grpc::Status& status)> post_finish
    ) noexcept;
    /// @endcond

    UnaryFuture(UnaryFuture&&) noexcept;
    UnaryFuture& operator=(UnaryFuture&&) noexcept;
    UnaryFuture(const UnaryFuture&) = delete;
    UnaryFuture& operator=(const UnaryFuture&) = delete;

    ~UnaryFuture();

    /// @brief Checks if the asynchronous call has completed
    ///        Note, that once user gets result, IsReady should not be called
    /// @return true if result ready
    [[nodiscard]] bool IsReady() const noexcept;

    /// @brief Await response until the deadline is reached or until the task is cancelled.
    ///
    /// Upon completion result is available in `response` when initiating the
    /// asynchronous operation, e.g. FinishAsync.
    [[nodiscard]] engine::FutureStatus WaitUntil(engine::Deadline deadline) const noexcept;

    /// @brief Await response
    ///
    /// Upon completion result is available in `response` when initiating the
    /// asynchronous operation, e.g. FinishAsync.
    ///
    /// `Get` should not be called multiple times for the same UnaryFuture.
    ///
    /// @throws ugrpc::client::RpcError on an RPC error
    /// @throws ugrpc::client::RpcCancelledError on task cancellation
    void Get();

    /// @cond
    // For internal use only.
    engine::impl::ContextAccessor* TryGetContextAccessor() noexcept;
    /// @endcond

private:
    impl::RpcData* data_{};
    std::function<void(impl::RpcData& data, const grpc::Status& status)> post_finish_;
    mutable std::exception_ptr exception_;
};

}  // namespace impl

/// @brief StreamReadFuture for waiting a single read response from stream
template <typename RPC>
class [[nodiscard]] StreamReadFuture {
public:
    /// @cond
    explicit StreamReadFuture(
        impl::RpcData& data,
        typename RPC::RawStream& stream,
        std::function<void(impl::RpcData& data)> post_recv_message,
        std::function<void(impl::RpcData& data, const grpc::Status& status)> post_finish
    ) noexcept;
    /// @endcond

    StreamReadFuture(StreamReadFuture&& other) noexcept;
    StreamReadFuture& operator=(StreamReadFuture&& other) noexcept;
    StreamReadFuture(const StreamReadFuture&) = delete;
    StreamReadFuture& operator=(const StreamReadFuture&) = delete;

    ~StreamReadFuture();

    /// @brief Await response
    ///
    /// Upon completion the result is available in `response` that was
    /// specified when initiating the asynchronous read
    ///
    /// `Get` should not be called multiple times for the same StreamReadFuture.
    ///
    /// @throws ugrpc::client::RpcError on an RPC error
    /// @throws ugrpc::client::RpcCancelledError on task cancellation
    bool Get();

    /// @brief Checks if the asynchronous call has completed
    ///        Note, that once user gets result, IsReady should not be called
    /// @return true if result ready
    [[nodiscard]] bool IsReady() const noexcept;

private:
    impl::RpcData* data_{};
    typename RPC::RawStream* stream_{};
    std::function<void(impl::RpcData& data)> post_recv_message_;
    std::function<void(impl::RpcData& data, const grpc::Status& status)> post_finish_;
};

/// @brief Base class for any RPC
class CallAnyBase {
protected:
    /// @cond
    CallAnyBase(impl::CallParams&& params, CallKind call_kind)
        : data_(std::make_unique<impl::RpcData>(std::move(params), call_kind)) {}
    /// @endcond

public:
    /// @returns the `ClientContext` used for this RPC
    grpc::ClientContext& GetContext();

    /// @returns client name
    std::string_view GetClientName() const;

    /// @returns RPC name
    std::string_view GetCallName() const;

    /// @returns RPC span
    tracing::Span& GetSpan();

protected:
    impl::RpcData& GetData();

private:
    std::unique_ptr<impl::RpcData> data_;
};

namespace impl {

/// @brief Controls a single request -> single response RPC
///
/// This class is not thread-safe except for `GetContext`.
///
/// The RPC is cancelled on destruction unless `Finish` or `FinishAsync`. In
/// that case the connection is not closed (it will be reused for new RPCs), and
/// the server receives `RpcInterruptedError` immediately.
template <typename Response>
class [[nodiscard]] UnaryCall final : public CallAnyBase {
public:
    using ResponseType = Response;

    /// @brief Await and read the response
    ///
    /// `Finish` should not be called multiple times for the same RPC.
    ///
    /// The connection is not closed, it will be reused for new RPCs.
    ///
    /// @returns the response on success
    /// @throws ugrpc::client::RpcError on an RPC error
    /// @throws ugrpc::client::RpcCancelledError on task cancellation
    Response Finish();

    /// @brief Asynchronously finish the call
    ///
    /// `FinishAsync` should not be called multiple times for the same RPC.
    ///
    /// `Finish` and `FinishAsync` should not be called together for the same RPC.
    ///
    /// @returns the future for the single response
    UnaryFuture FinishAsync(Response& response);

    /// @cond
    // For internal use only
    template <typename PrepareFunc, typename Request>
    UnaryCall(impl::CallParams&& params, PrepareFunc prepare_func, const Request& req);
    /// @endcond

    UnaryCall(UnaryCall&&) noexcept = default;
    UnaryCall& operator=(UnaryCall&&) noexcept = default;
    ~UnaryCall() = default;

private:
    impl::RawResponseReader<Response> reader_;
};

}  // namespace impl

/// @brief Controls a single request -> response stream RPC
///
/// This class is not thread-safe except for `GetContext`.
///
/// The RPC is cancelled on destruction unless the stream is closed (`Read` has
/// returned `false`). In that case the connection is not closed (it will be
/// reused for new RPCs), and the server receives `RpcInterruptedError`
/// immediately. gRPC provides no way to early-close a server-streaming RPC
/// gracefully.
///
/// If any method throws, further methods must not be called on the same stream,
/// except for `GetContext`.
template <typename Response>
class [[nodiscard]] InputStream final : public CallAnyBase {
public:
    /// @brief Await and read the next incoming message
    ///
    /// On end-of-input, `Finish` is called automatically.
    ///
    /// @param response where to put response on success
    /// @returns `true` on success, `false` on end-of-input or task cancellation
    /// @throws ugrpc::client::RpcError on an RPC error
    [[nodiscard]] bool Read(Response& response);

    /// @cond
    // For internal use only
    using RawStream = grpc::ClientAsyncReader<Response>;

    template <typename PrepareFunc, typename Request>
    InputStream(impl::CallParams&& params, PrepareFunc prepare_func, const Request& req);
    /// @endcond

    InputStream(InputStream&&) noexcept = default;
    InputStream& operator=(InputStream&&) noexcept = default;
    ~InputStream() = default;

private:
    impl::RawReader<Response> stream_;
};

/// @brief Controls a request stream -> single response RPC
///
/// This class is not thread-safe except for `GetContext`.
///
/// The RPC is cancelled on destruction unless `Finish` has been called. In that
/// case the connection is not closed (it will be reused for new RPCs), and the
/// server receives `RpcInterruptedError` immediately.
///
/// If any method throws, further methods must not be called on the same stream,
/// except for `GetContext`.
template <typename Request, typename Response>
class [[nodiscard]] OutputStream final : public CallAnyBase {
public:
    /// @brief Write the next outgoing message
    ///
    /// `Write` doesn't store any references to `request`, so it can be
    /// deallocated right after the call.
    ///
    /// @param request the next message to write
    /// @return true if the data is going to the wire; false if the write
    ///         operation failed (including due to task cancellation),
    ///         in which case no more writes will be accepted,
    ///         and the error details can be fetched from Finish
    [[nodiscard]] bool Write(const Request& request);

    /// @brief Write the next outgoing message and check result
    ///
    /// `WriteAndCheck` doesn't store any references to `request`, so it can be
    /// deallocated right after the call.
    ///
    /// `WriteAndCheck` verifies result of the write and generates exception
    /// in case of issues.
    ///
    /// @param request the next message to write
    /// @throws ugrpc::client::RpcError on an RPC error
    /// @throws ugrpc::client::RpcCancelledError on task cancellation
    void WriteAndCheck(const Request& request);

    /// @brief Complete the RPC successfully
    ///
    /// Should be called once all the data is written. The server will then
    /// send a single `Response`.
    ///
    /// `Finish` should not be called multiple times.
    ///
    /// The connection is not closed, it will be reused for new RPCs.
    ///
    /// @returns the single `Response` received after finishing the writes
    /// @throws ugrpc::client::RpcError on an RPC error
    /// @throws ugrpc::client::RpcCancelledError on task cancellation
    Response Finish();

    /// @cond
    // For internal use only
    using RawStream = grpc::ClientAsyncWriter<Request>;

    template <typename PrepareFunc>
    OutputStream(impl::CallParams&& params, PrepareFunc prepare_func);
    /// @endcond

    OutputStream(OutputStream&&) noexcept = default;
    OutputStream& operator=(OutputStream&&) noexcept = default;
    ~OutputStream() = default;

private:
    std::unique_ptr<Response> final_response_;
    impl::RawWriter<Request> stream_;
};

/// @brief Controls a request stream -> response stream RPC
///
/// It is safe to call the following methods from different coroutines:
///
///   - `GetContext`;
///   - one of (`Read`, `ReadAsync`);
///   - one of (`Write`, `WritesDone`).
///
/// `WriteAndCheck` is NOT thread-safe.
///
/// The RPC is cancelled on destruction unless the stream is closed (`Read` has
/// returned `false`). In that case the connection is not closed (it will be
/// reused for new RPCs), and the server receives `RpcInterruptedError`
/// immediately. gRPC provides no way to early-close a server-streaming RPC
/// gracefully.
///
/// `Read` and `AsyncRead` can throw if error status is received from server.
/// User MUST NOT call `Read` or `AsyncRead` again after failure of any of these
/// operations.
///
/// `Write` and `WritesDone` methods do not throw, but indicate issues with
/// the RPC by returning `false`.
///
/// `WriteAndCheck` is intended for ping-pong scenarios, when after write
/// operation the user calls `Read` and vice versa.
///
/// If `Write` or `WritesDone` returns negative result, the user MUST NOT call
/// any of these methods anymore.
/// Instead the user SHOULD call `Read` method until the end of input. If
/// `Write` or `WritesDone` finishes with negative result, finally `Read`
/// will throw an exception.
/// ## Usage example:
///
/// @snippet grpc/tests/stream_test.cpp concurrent bidirectional stream
///
template <typename Request, typename Response>
class [[nodiscard]] BidirectionalStream final : public CallAnyBase {
public:
    /// @brief Await and read the next incoming message
    ///
    /// On end-of-input, `Finish` is called automatically.
    ///
    /// @param response where to put response on success
    /// @returns `true` on success, `false` on end-of-input or task cancellation
    /// @throws ugrpc::client::RpcError on an RPC error
    [[nodiscard]] bool Read(Response& response);

    /// @brief Return future to read next incoming result
    ///
    /// @param response where to put response on success
    /// @return StreamReadFuture future
    /// @throws ugrpc::client::RpcError on an RPC error
    StreamReadFuture<BidirectionalStream> ReadAsync(Response& response) noexcept;

    /// @brief Write the next outgoing message
    ///
    /// RPC will be performed immediately. No references to `request` are
    /// saved, so it can be deallocated right after the call.
    ///
    /// @param request the next message to write
    /// @return true if the data is going to the wire; false if the write
    ///         operation failed (including due to task cancellation),
    ///         in which case no more writes will be accepted,
    ///         but Read may still have some data and status code available
    [[nodiscard]] bool Write(const Request& request);

    /// @brief Write the next outgoing message and check result
    ///
    /// `WriteAndCheck` doesn't store any references to `request`, so it can be
    /// deallocated right after the call.
    ///
    /// `WriteAndCheck` verifies result of the write and generates exception
    /// in case of issues.
    ///
    /// @param request the next message to write
    /// @throws ugrpc::client::RpcError on an RPC error
    /// @throws ugrpc::client::RpcCancelledError on task cancellation
    void WriteAndCheck(const Request& request);

    /// @brief Announce end-of-output to the server
    ///
    /// Should be called to notify the server and receive the final response(s).
    ///
    /// @return true if the data is going to the wire; false if the operation
    ///         failed, but Read may still have some data and status code
    ///         available
    [[nodiscard]] bool WritesDone();

    /// @cond
    // For internal use only
    using RawStream = grpc::ClientAsyncReaderWriter<Request, Response>;

    template <typename PrepareFunc>
    BidirectionalStream(impl::CallParams&& params, PrepareFunc prepare_func);
    /// @endcond

    BidirectionalStream(BidirectionalStream&&) noexcept = default;
    BidirectionalStream& operator=(BidirectionalStream&&) noexcept = default;
    ~BidirectionalStream() = default;

private:
    impl::RawReaderWriter<Request, Response> stream_;
};

template <typename RPC>
StreamReadFuture<RPC>::StreamReadFuture(
    impl::RpcData& data,
    typename RPC::RawStream& stream,
    std::function<void(impl::RpcData& data)> post_recv_message,
    std::function<void(impl::RpcData& data, const grpc::Status& status)> post_finish
) noexcept
    : data_(&data),
      stream_(&stream),
      post_recv_message_(std::move(post_recv_message)),
      post_finish_(std::move(post_finish)) {}

template <typename RPC>
StreamReadFuture<RPC>::StreamReadFuture(StreamReadFuture&& other) noexcept
    : data_{std::exchange(other.data_, nullptr)},
      stream_{other.stream_},
      post_recv_message_{std::move(other.post_recv_message_)},
      post_finish_{std::move(other.post_finish_)} {}

template <typename RPC>
StreamReadFuture<RPC>& StreamReadFuture<RPC>::operator=(StreamReadFuture<RPC>&& other) noexcept {
    if (this == &other) return *this;
    [[maybe_unused]] auto for_destruction = std::move(*this);
    data_ = std::exchange(other.data_, nullptr);
    stream_ = other.stream_;
    post_recv_message_ = std::move(other.post_recv_message_);
    post_finish_ = std::move(other.post_finish_);
    return *this;
}

template <typename RPC>
StreamReadFuture<RPC>::~StreamReadFuture() {
    if (data_) {
        impl::RpcData::AsyncMethodInvocationGuard guard(*data_);
        const auto wait_status = impl::Wait(data_->GetAsyncMethodInvocation(), data_->GetContext());
        if (wait_status != impl::AsyncMethodInvocation::WaitStatus::kOk) {
            if (wait_status == impl::AsyncMethodInvocation::WaitStatus::kCancelled) {
                data_->GetStatsScope().OnCancelled();
            }
            impl::Finish(*stream_, *data_, post_finish_, false);
        } else {
            post_recv_message_(*data_);
        }
    }
}

template <typename RPC>
bool StreamReadFuture<RPC>::Get() {
    UINVARIANT(data_, "'Get' must be called only once");
    impl::RpcData::AsyncMethodInvocationGuard guard(*data_);
    auto* const data = std::exchange(data_, nullptr);
    const auto result = impl::Wait(data->GetAsyncMethodInvocation(), data->GetContext());
    if (result == impl::AsyncMethodInvocation::WaitStatus::kCancelled) {
        data->GetStatsScope().OnCancelled();
        data->GetStatsScope().Flush();
    } else if (result == impl::AsyncMethodInvocation::WaitStatus::kError) {
        // Finish can only be called once all the data is read, otherwise the
        // underlying gRPC driver hangs.
        impl::Finish(*stream_, *data, post_finish_, true);
    } else {
        post_recv_message_(*data);
    }
    return result == impl::AsyncMethodInvocation::WaitStatus::kOk;
}

template <typename RPC>
bool StreamReadFuture<RPC>::IsReady() const noexcept {
    UINVARIANT(data_, "IsReady should be called only before 'Get'");
    auto& method = data_->GetAsyncMethodInvocation();
    return method.IsReady();
}

namespace impl {

template <typename Response>
template <typename PrepareFunc, typename Request>
UnaryCall<Response>::UnaryCall(impl::CallParams&& params, PrepareFunc prepare_func, const Request& req)
    : CallAnyBase(std::move(params), CallKind::kUnaryCall) {
    impl::MiddlewarePipeline::PreStartCall(GetData());
    if constexpr (std::is_base_of_v<google::protobuf::Message, Request>) {
        impl::MiddlewarePipeline::PreSendMessage(GetData(), req);
    }

    reader_ = prepare_func(&GetData().GetContext(), req, &GetData().GetQueue());
    reader_->StartCall();

    GetData().SetWritesFinished();
}

template <typename Response>
Response UnaryCall<Response>::Finish() {
    Response response;
    UnaryFuture future = FinishAsync(response);
    future.Get();
    return response;
}

template <typename Response>
UnaryFuture UnaryCall<Response>::FinishAsync(Response& response) {
    UASSERT(reader_);
    PrepareFinish(GetData());
    GetData().EmplaceFinishAsyncMethodInvocation();
    auto& finish = GetData().GetFinishAsyncMethodInvocation();
    auto& status = GetData().GetStatus();
    reader_->Finish(&response, &status, finish.GetTag());
    auto post_finish = [&response](impl::RpcData& data, const grpc::Status& status) {
        if constexpr (std::is_base_of_v<google::protobuf::Message, Response>) {
            impl::MiddlewarePipeline::PostRecvMessage(data, response);
        } else {
            (void)response;  // unused by now
        }
        impl::MiddlewarePipeline::PostFinish(data, status);
    };
    return UnaryFuture{GetData(), post_finish};
}

}  // namespace impl

template <typename Response>
template <typename PrepareFunc, typename Request>
InputStream<Response>::InputStream(impl::CallParams&& params, PrepareFunc prepare_func, const Request& req)
    : CallAnyBase(std::move(params), CallKind::kInputStream) {
    impl::MiddlewarePipeline::PreStartCall(GetData());
    impl::MiddlewarePipeline::PreSendMessage(GetData(), req);

    // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
    stream_ = prepare_func(&GetData().GetContext(), req, &GetData().GetQueue());
    impl::StartCall(*stream_, GetData());

    GetData().SetWritesFinished();
}

template <typename Response>
bool InputStream<Response>::Read(Response& response) {
    if (impl::Read(*stream_, response, GetData())) {
        impl::MiddlewarePipeline::PostRecvMessage(GetData(), response);
        return true;
    } else {
        // Finish can only be called once all the data is read, otherwise the
        // underlying gRPC driver hangs.
        auto post_finish = [](impl::RpcData& data, const grpc::Status& status) {
            impl::MiddlewarePipeline::PostFinish(data, status);
        };
        impl::Finish(*stream_, GetData(), post_finish, true);
        return false;
    }
}

template <typename Request, typename Response>
template <typename PrepareFunc>
OutputStream<Request, Response>::OutputStream(impl::CallParams&& params, PrepareFunc prepare_func)
    : CallAnyBase(std::move(params), CallKind::kOutputStream), final_response_(std::make_unique<Response>()) {
    impl::MiddlewarePipeline::PreStartCall(GetData());

    // 'final_response_' will be filled upon successful 'Finish' async call
    // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
    stream_ = prepare_func(&GetData().GetContext(), final_response_.get(), &GetData().GetQueue());
    impl::StartCall(*stream_, GetData());
}

template <typename Request, typename Response>
bool OutputStream<Request, Response>::Write(const Request& request) {
    impl::MiddlewarePipeline::PreSendMessage(GetData(), request);

    // Don't buffer writes, otherwise in an event subscription scenario, events
    // may never actually be delivered
    grpc::WriteOptions write_options{};
    return impl::Write(*stream_, request, write_options, GetData());
}

template <typename Request, typename Response>
void OutputStream<Request, Response>::WriteAndCheck(const Request& request) {
    impl::MiddlewarePipeline::PreSendMessage(GetData(), request);

    // Don't buffer writes, otherwise in an event subscription scenario, events
    // may never actually be delivered
    grpc::WriteOptions write_options{};
    if (!impl::Write(*stream_, request, write_options, GetData())) {
        auto post_finish = [](impl::RpcData& data, const grpc::Status& status) {
            impl::MiddlewarePipeline::PostFinish(data, status);
        };
        impl::Finish(*stream_, GetData(), post_finish, true);
    }
}

template <typename Request, typename Response>
Response OutputStream<Request, Response>::Finish() {
    // gRPC does not implicitly call `WritesDone` in `Finish`,
    // contrary to the documentation
    if (!GetData().AreWritesFinished()) {
        impl::WritesDone(*stream_, GetData());
    }

    auto post_finish = [](impl::RpcData& data, const grpc::Status& status) {
        impl::MiddlewarePipeline::PostFinish(data, status);
    };
    impl::Finish(*stream_, GetData(), post_finish, true);

    return std::move(*final_response_);
}

template <typename Request, typename Response>
template <typename PrepareFunc>
BidirectionalStream<Request, Response>::BidirectionalStream(impl::CallParams&& params, PrepareFunc prepare_func)
    : CallAnyBase(std::move(params), CallKind::kBidirectionalStream) {
    impl::MiddlewarePipeline::PreStartCall(GetData());

    // NOLINTNEXTLINE(cppcoreguidelines-prefer-member-initializer)
    stream_ = prepare_func(&GetData().GetContext(), &GetData().GetQueue());
    impl::StartCall(*stream_, GetData());
}

template <typename Request, typename Response>
StreamReadFuture<BidirectionalStream<Request, Response>> BidirectionalStream<Request, Response>::ReadAsync(
    Response& response
) noexcept {
    impl::ReadAsync(*stream_, response, GetData());
    auto post_recv_message = [&response](impl::RpcData& data) {
        impl::MiddlewarePipeline::PostRecvMessage(data, response);
    };
    auto post_finish = [](impl::RpcData& data, const grpc::Status& status) {
        impl::MiddlewarePipeline::PostFinish(data, status);
    };
    return StreamReadFuture<BidirectionalStream<Request, Response>>{
        GetData(), *stream_, post_recv_message, post_finish};
}

template <typename Request, typename Response>
bool BidirectionalStream<Request, Response>::Read(Response& response) {
    auto future = ReadAsync(response);
    return future.Get();
}

template <typename Request, typename Response>
bool BidirectionalStream<Request, Response>::Write(const Request& request) {
    impl::MiddlewarePipeline::PreSendMessage(GetData(), request);

    // Don't buffer writes, optimize for ping-pong-style interaction
    grpc::WriteOptions write_options{};
    return impl::Write(*stream_, request, write_options, GetData());
}

template <typename Request, typename Response>
void BidirectionalStream<Request, Response>::WriteAndCheck(const Request& request) {
    impl::MiddlewarePipeline::PreSendMessage(GetData(), request);

    // Don't buffer writes, optimize for ping-pong-style interaction
    grpc::WriteOptions write_options{};
    impl::WriteAndCheck(*stream_, request, write_options, GetData());
}

template <typename Request, typename Response>
bool BidirectionalStream<Request, Response>::WritesDone() {
    return impl::WritesDone(*stream_, GetData());
}

}  // namespace ugrpc::client

USERVER_NAMESPACE_END
