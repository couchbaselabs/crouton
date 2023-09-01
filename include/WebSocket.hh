//
// WebSocket.hh
//
// Copyright 2023-Present Couchbase, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#pragma once
#include "HTTPConnection.hh"
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <string_view>

namespace uWS {
    template <const bool isServer> class WebSocketProtocol;
}

namespace crouton {

    /** A WebSocket client connection. */
    class WebSocket {
    public:

        /// WebSocket message types
        enum MessageType : uint8_t {
            NONE = 0,   // used when receive() hits EOF
            Text = 1,
            Binary = 2,
            Close = 8,
            Ping = 9,
            Pong = 10,
        };


        /// A WebSocket message: a string plus a type.
        struct Message : public std::string {
            using std::string::string;
            MessageType type = Binary;
        };


        /// Constructs a WebSocket, but doesn't connect yet.
        explicit WebSocket(std::string urlStr);
        ~WebSocket();

        /// Adds an HTTP request header.
        /// @note You may need to add a `Sec-WebSocket-Protocol` header if the server requires one.
        void setHeader(const char* name, const char* value);

        /// Connects to the server.
        [[nodiscard]] Future<void> connect();

        /// The HTTP response headers.
        HTTPHeaders const& responseHeaders()    {return _responseHeaders;}

        /// Returns the next incoming WebSocket binary message, asynchronously.
        /// If the server has closed the connection, the message's type will be `NONE`.
        /// If there's a connection error, the Future will hold it (and throw when resolved.)
        [[nodiscard]] Future<Message> receive();

        /// Sends a binary message, asynchronously.
        /// @note The data is copied and does not need to remain valid after the call.
        [[nodiscard]] Future<void> send(const void* data, size_t len, MessageType = Binary);
        [[nodiscard]] Future<void> send(std::string_view str, MessageType type = Binary)
            {return send(str.data(), str.size(), type);}
        [[nodiscard]] Future<void> send(const char* str, MessageType type = Binary)
            {return send(str, strlen(str), type);}

        /// Closes the connection immediately.
        void close();

    private:
        template <bool S> friend class uWS::WebSocketProtocol;
        using ClientProtocol = uWS::WebSocketProtocol<false>;

        bool handleFragment(std::byte* data, size_t length, size_t remainingBytes, uint8_t opCode,
                       bool fin);
        void protocolError();

        HTTPConnection              _connection;
        HTTPRequest                 _request;
        std::string                 _accept;
        HTTPHeaders                 _responseHeaders;
        IStream*                    _stream = nullptr;
        std::unique_ptr<ClientProtocol> _parser;
        std::deque<Message>         _incoming;
        std::optional<Message>      _curMessage;
    };

}
