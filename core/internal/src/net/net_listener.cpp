#include <userver/internal/net/net_listener.hpp>

#include <userver/engine/async.hpp>
#include <userver/utils/assert.hpp>

USERVER_NAMESPACE_BEGIN

namespace internal::net {
namespace {

auto IpVersionToDomain(IpVersion ipv) {
    switch (ipv) {
        case IpVersion::kV6:
            return engine::io::AddrDomain::kInet6;
        case IpVersion::kV4:
            return engine::io::AddrDomain::kInet;
    }
    UINVARIANT(false, "Unexpected ip version");
}

void ListenerCtor(engine::io::Sockaddr& addr, engine::io::Socket& socket, IpVersion ipv) {
    switch (ipv) {
        case IpVersion::kV6:
            addr = engine::io::Sockaddr::MakeLoopbackAddress();
            break;
        case IpVersion::kV4:
            addr = engine::io::Sockaddr::MakeIPv4LoopbackAddress();
            break;
    }

    addr.SetPort(0);
    socket.Bind(addr);
    addr.SetPort(socket.Getsockname().Port());
}

}  // namespace

TcpListener::TcpListener(IpVersion ipv) : socket{IpVersionToDomain(ipv), kType} {
    ListenerCtor(addr, socket, ipv);
    socket.Listen();
}

std::pair<engine::io::Socket, engine::io::Socket> TcpListener::MakeSocketPair(engine::Deadline deadline) {
    auto connect_task = engine::AsyncNoSpan([this, deadline] {
        engine::io::Socket peer_socket{addr.Domain(), kType};
        peer_socket.Connect(addr, deadline);
        return peer_socket;
    });
    std::pair<engine::io::Socket, engine::io::Socket> socket_pair;
    socket_pair.first = socket.Accept(deadline);
    socket_pair.second = connect_task.Get();
    return socket_pair;
}

UdpListener::UdpListener(IpVersion ipv) : socket{IpVersionToDomain(ipv), kType} { ListenerCtor(addr, socket, ipv); }

}  // namespace internal::net

USERVER_NAMESPACE_END
