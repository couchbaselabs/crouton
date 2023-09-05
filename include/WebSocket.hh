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
#include "Coroutine.hh"
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

        /// Status code in a WebSocket Close message.
        /// Definitions are at <http://tools.ietf.org/html/rfc6455#section-7.4.1>
        enum class CloseCode : uint16_t {
            Normal           = 1000, // Normal close
            GoingAway        = 1001, // Peer has to close, e.g. because host app is quitting
            ProtocolError    = 1002, // Protocol violation: invalid framing data
            DataError        = 1003, // Message payload cannot be handled
            NoCode           = 1005, // No status code in close frame [do not send]
            Abnormal         = 1006, // Peer closed socket unexpectedly w/o close frame [do not send]
            BadMessageFormat = 1007, // Unparseable message
            PolicyError      = 1008,
            MessageTooBig    = 1009, // Peer doesn't provide a necessary extension
            MissingExtension = 1010, // Client needs extension not priv
            CantFulfill      = 1011, // Server could not fulfil request [never sent by client]
            AppTransient     = 4001, // App-defined transient error
            AppPermanent     = 4002, // App-defined permanent error
            FirstAvailable   = 5000, // First unregistered code for freeform use
        };


        /// WebSocket message types (numeric values defined by the protocol.)
        enum MessageType : uint8_t {
            Text   =  1,
            Binary =  2,
            Close  =  8,
            Ping   =  9,
            Pong   = 10,
        };


        /// A WebSocket message: a string plus a type.
        struct Message : public std::string {
            using std::string::string;

            MessageType type = Binary;

            Message(CloseCode, std::string_view message);
            CloseCode closeCode() const;            ///< If type==Close, this is the status code
            std::string_view closeMessage() const;  ///< If type==Close, this is the status message
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
        /// - If the server has closed the connection, the message's type will be `NONE`.
        /// - If there's a connection error, the Future will hold it (and throw when resolved.)
        /// - If the peer decides to close the socket, or after you call `close`, a message of
        ///   type `Close` will arrive. No further messages will arrive; don't call receive again.
        [[nodiscard]] Future<Message> receive();

        /// Sends a binary message, asynchronously.
        /// @note The data is copied and does not need to remain valid after the call.
        [[nodiscard]] Future<void> send(const void* data, size_t len, MessageType = Binary);
        [[nodiscard]] Future<void> send(std::string_view str, MessageType type = Binary)
            {return send(str.data(), str.size(), type);}
        [[nodiscard]] Future<void> send(const char* str, MessageType type = Binary)
            {return send(str, strlen(str), type);}
        [[nodiscard]] Future<void> send(Message const&);

        /// Sends a request to close the socket. Peer will respond with a Close message that
        /// will be returned from `receive`; at that point the socket is closed.
        Future<void> sendClose(CloseCode = CloseCode::Normal, std::string_view message = "");

        /// Closes the socket without sending a Close message or waiting to receive a response.
        /// This should only be called after you've already done the above.
        Future<void> close();

        /// Closes the connection immediately.
        void disconnect();

    private:
        template <bool S> friend class uWS::WebSocketProtocol;
        using ClientProtocol = uWS::WebSocketProtocol<false>;

        bool handleFragment(std::byte*, size_t, size_t,uint8_t, bool);
        void protocolError();
        [[nodiscard]] Future<void> handleCloseMessage(Message const& msg);

        HTTPConnection              _connection;
        HTTPRequest                 _request;
        std::string                 _accept;
        HTTPHeaders                 _responseHeaders;
        IStream*                    _stream = nullptr;
        std::unique_ptr<ClientProtocol> _parser;
        std::deque<Message>         _incoming;
        std::optional<Message>      _curMessage;
        bool                        _closeSent = false;
        bool                        _closeReceived = false;
    };

}
