//
// TCPServer.hh
//
// 
//

#pragma once
#include "UVBase.hh"
#include <functional>

struct uv_tcp_s;

namespace snej::coro::uv {
    class TCPSocket;

    class TCPServer {
    public:
        TCPServer(uint16_t port);
        ~TCPServer();

        using Acceptor = std::function<void(std::shared_ptr<TCPSocket>)>;

        void listen(Acceptor);

        void close();

    private:
        void accept(int status);
        
        uv_tcp_s*       _tcpHandle;         // Handle for TCP operations
        Acceptor        _acceptor;
    };

}
