//
// TCPServer.cc
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#include "TCPServer.hh"
#include "TCPSocket.hh"
#include "UVInternal.hh"
#include <unistd.h>

namespace crouton {
    using namespace std;


    TCPServer::TCPServer(uint16_t port)
    :_tcpHandle(new uv_tcp_s)
    {
        uv_tcp_init(curLoop(), _tcpHandle);
        _tcpHandle->data = this;
        sockaddr_in addr = {};
        uv_ip4_addr("0.0.0.0", port, &addr);
        check(uv_tcp_bind(_tcpHandle, (sockaddr*)&addr, 0), "initializing server");
    }

    TCPServer::~TCPServer() {
        close();
    }


    void TCPServer::listen(std::function<void(std::shared_ptr<TCPSocket>)> acceptor) {
        _acceptor = std::move(acceptor);
        check(uv_listen((uv_stream_t*)_tcpHandle, 2, [](uv_stream_t *server, int status) noexcept {
            try {
                ((TCPServer*)server->data)->accept(status);
            } catch (...) {
                fprintf(stderr, "*** Caught unexpected exception in TCPServer::accept ***\n");
            }
        }), "starting server");
    }


    void TCPServer::close() {
        closeHandle(_tcpHandle);
    }


    void TCPServer::accept(int status) {
        if (status < 0) {
            fprintf(stderr, "TCPServer::listen failed: error %d", status);
            //TODO: Notify the app somehow
        } else {
            //OnEventLoop([&]{
                auto client = make_shared<TCPSocket>();
                client->acceptFrom(_tcpHandle);
                _acceptor(std::move(client));
            //});
        }
    }

}
