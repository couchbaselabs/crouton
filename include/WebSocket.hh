//
// WebSocket.hh
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#pragma once
#include "HTTPClient.hh"
#include <deque>

struct tlsuv_websocket_s;
struct uv_buf_t;

namespace crouton {

    /** A WebSocket client connection. */
    class WebSocket {
    public:

        /// Constructs a WebSocket, but doesn't connect yet.
        explicit WebSocket(std::string urlStr);
        ~WebSocket() {close();}

        /// Adds an HTTP request header.
        /// @note You may need to add a `Sec-WebSocket-Protocol` header if the server requires one.
        void setHeader(const char* name, const char* value);

        /// Connects to the server.
        [[nodiscard]] Future<HTTPStatus> connect();

        /// Returns the next incoming WebSocket binary message, asynchronously.
        /// If there's a connection error, the Future will hold it (and throw when resolved.)
        [[nodiscard]] Future<std::string> receive();

        /// Sends a binary message, asynchronously.
        /// @warning The data buffer must remain valid until the call finishes.
        [[nodiscard]] Future<void> send(const void* data, size_t len);
        [[nodiscard]] Future<void> send(std::string_view str) {return send(str.data(), str.size());}

        /// Sends a binary message, asynchronously. The data is copied immediately.
        [[nodiscard]] Future<void> send(std::string);

        /// Closes the connection immediately.
        void close();

    private:
        void received(const uv_buf_t *buf, ssize_t nread);

        std::string                 _url;
        tlsuv_websocket_s*          _handle = nullptr;
        FutureProvider<std::string> _nextIncoming;
        std::deque<FutureProvider<std::string>> _moreIncoming;
    };

}
