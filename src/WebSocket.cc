//
// WebSocket.cc
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

#include "WebSocket.hh"
#include "StringUtils.hh"
#include "UVInternal.hh"
#include "WebSocketProtocol.hh"
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>

namespace crouton {
    using namespace std;


    WebSocket::WebSocket(std::string urlStr)
    :_connection(urlStr)
    ,_parser(make_unique<ClientProtocol>())
    {
        _request.headers.set("Connection", "Upgrade");
        _request.headers.set("Upgrade", "WebSocket");
        _request.headers.set("Sec-WebSocket-Version", "13");

        // Generate a base64-encoded 16-byte random `Sec-WebSocket-Key`:
        uint8_t rawKey[16];
        Randomize(&rawKey, sizeof(rawKey));
        char key[100];
        size_t len;
        mbedtls_base64_encode((uint8_t*)key, sizeof(key), &len, rawKey, sizeof(rawKey));
        key[len] = '\0';
        _request.headers.set("Sec-WebSocket-Key", string(key));

        // Compute the `Sec-WebSocket-Accept` response the server should generate from the key:
        strcat(key, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        uint8_t digest[20];
        mbedtls_sha1((const uint8_t*)key, strlen(key), digest);
        char accept[60];
        mbedtls_base64_encode((uint8_t*)accept, sizeof(accept), &len, digest, sizeof(digest));
        _accept = string(accept, len);
    }


    WebSocket::~WebSocket() {disconnect();}


    void WebSocket::setHeader(const char* name, const char* value) {
        _request.headers.set(name, value);
    }


    Future<void> WebSocket::connect() {
        HTTPResponse response = AWAIT _connection.send(_request);

        _responseHeaders = response.headers();
        if (auto status = response.status(); status != HTTPStatus::SwitchingProtocols)
            throw runtime_error("Server returned wrong status " + to_string(int(status)));
        if (!equalIgnoringCase(_responseHeaders.get("Connection"), "upgrade") ||
                !equalIgnoringCase(_responseHeaders.get("Upgrade"), "websocket"))
            throw runtime_error("Server did not upgrade to WebSocket protocol");
        if (_accept != _responseHeaders.get("Sec-WebSocket-Accept"))
            throw runtime_error("Server returned wrong Sec-WebSocket-Accept value");

        _stream = &response.upgradedStream();
    }


    Future<void> WebSocket::close() {
        if (_stream) {
            if (!_closeReceived)
                fprintf(stderr, "WARNING: WebSocket::close called before receiving a Close msg\n");
            if (!_incoming.empty())
                fprintf(stderr, "WARNING: WebSocket closing with %zu unread incoming messages\n",
                        _incoming.size());
            AWAIT _stream->close();
            _stream = nullptr;
        }
    }


    void WebSocket::disconnect() {
        if (!_incoming.empty())
            fprintf(stderr, "WARNING: WebSocket disconnected with %zu unread incoming messages\n",
                    _incoming.size());
        if (_stream) {
            _connection.close();
            _stream = nullptr;
        }
    }


    Future<void> WebSocket::send(const void* data, size_t len, MessageType type) {
        // Note: This method is not a coroutine, and it passes the message to _stream->write
        // as a `string`, so the caller does not need to keep the data valid.
        if (_closeSent || !_stream)
            throw std::logic_error("WebSocket is already closing");
        if (type == Close)
            _closeSent = true;
        string frame(len + 10, 0);
        size_t frameLen = ClientProtocol::formatMessage((std::byte*)frame.data(),
                                                        (const char*)data,
                                                        len,
                                                        uWS::OpCode(type),
                                                        len,
                                                        false);
        frame.resize(frameLen);
        return _stream->write(frame);
    }


    Future<void> WebSocket::send(Message const& msg) {
        return send(msg.data(), msg.size(), msg.type);
    }


    // Caller is initiating a close.
    Future<void> WebSocket::sendClose(CloseCode code, std::string_view message) {
        if (_closeSent)
            return Future<void>{};
        else
            return send(Message(code, message));
    }


    Future<WebSocket::Message> WebSocket::receive() {
        if (_closeReceived || !_stream)
            RETURN runtime_error("WebSocket is closed");
        while (true) {
            while (_incoming.empty()) {
                ConstBuf data = AWAIT _stream->readNoCopy(100000);
                if (data.len == 0)
                    RETURN Message(CloseCode::Abnormal, "WebSocket closed unexpectedly");
                // Pass the data to the 3rd-party WebSocket parser, which will call handleFragment.
                _parser->consume((std::byte*)data.base, data.len, this);
            }

            Message msg = std::move(_incoming.front());
            _incoming.pop_front();
            switch (msg.type) {
                case Text:
                case Binary:
                    if (_closeReceived) {
                        fprintf(stderr, "Peer illegally sent data message after CLOSE\n");
                        break;
                    }
                    RETURN msg;     // Got a message!
                case Close:
                    (void) handleCloseMessage(msg);
                    RETURN msg;     // Pass the CLOSE message to the peer.
                case Ping:
                    (void) send("", Pong);
                    break;
                case Pong:
                    //TODO: Send periodic Pings and disconnect if no Pong received in time
                default:
                    break; // ignore
           }
        }
    }


    // Called from inside _parser->consume(), called by receive(), above.
    // A single receive might result in multiple messages, so we queue them in `_incoming`.
    bool WebSocket::handleFragment(std::byte* data,
                                   size_t dataLen,
                                   size_t remainingBytes,
                                   uint8_t opCode,
                                   bool fin)
    {
        // Beginning:
        if (!_curMessage) {
            _curMessage.emplace();
            _curMessage->reserve(dataLen + remainingBytes);
            _curMessage->type = (MessageType)opCode;
        }

        // Data:
        _curMessage->append((char*)data, dataLen);

        // End:
        if (fin && remainingBytes == 0) {
            _incoming.emplace_back(std::move(_curMessage.value()));
            _curMessage = nullopt;
        }
        return true;
    }


    // Called from inside _parser->consume()
    void WebSocket::protocolError() {
        throw runtime_error("WebSocket protocol error");
    }


    // Peer sent a CLOSE message, either initiating or responding.
    Future<void> WebSocket::handleCloseMessage(Message const& msg) {
        if ( _closeReceived )
            return Future<void>{};
        _closeReceived = true;
        if ( _closeSent ) {
            // I initiated the close; the peer has confirmed, so disconnect the socket now:
            fprintf(stderr, "Close confirmed by peer; disconnecting socket now\n");
            return close();
        } else {
            // Peer is initiating a close; echo it:
            auto close = ClientProtocol::parseClosePayload((std::byte*)msg.data(), msg.size());
            fprintf(stderr, "Client is requesting close (%d '%.*s'); echoing it\n",
                    close.code, (int)close.length, (char*)close.message);
            _closeSent = true;
            return send(std::move(msg));
        }
    }


#pragma mark - MESSAGE:


    WebSocket::Message::Message(CloseCode code, string_view message)
    :string(message.size() + 2, 0)
    ,type(Close)
    {
        size_t sz = ClientProtocol::formatClosePayload((byte*)data(),
                                                       int16_t(code),
                                                       message.data(), message.size());
        assert(sz <= size());
        resize(sz);
    }

    WebSocket::CloseCode WebSocket::Message::closeCode() const {
        if (type != Close)
            throw std::logic_error("Not a CLOSE message");
        auto payload = ClientProtocol::parseClosePayload((std::byte*)data(), size());
        return CloseCode{payload.code};
    }

    string_view WebSocket::Message::closeMessage() const {
        if (type != Close)
            throw std::logic_error("Not a CLOSE message");
        auto payload = ClientProtocol::parseClosePayload((std::byte*)data(), size());
        return {(char*)payload.message, payload.length};
    }


}


#pragma mark - WEBSOCKETPROTOCOL


// The rest of the implementation of uWS::WebSocketProtocol, which calls into WebSocket:
namespace uWS {

    static constexpr size_t kMaxMessageLength = 1 << 20;


// The `user` parameter points to the owning WebSocketImpl object.
#define USER_SOCK ((crouton::WebSocket*)user)

    template <const bool isServer>
    bool WebSocketProtocol<isServer>::setCompressed(void* user) {
        return false;  //TODO: Implement compression
    }

    template <const bool isServer>
    bool WebSocketProtocol<isServer>::refusePayloadLength(void* user, size_t length) {
        return length > kMaxMessageLength;
    }

    template <const bool isServer>
    void WebSocketProtocol<isServer>::forceClose(void* user) {
        USER_SOCK->protocolError();
    }

    template <const bool isServer>
    bool WebSocketProtocol<isServer>::handleFragment(std::byte* data, size_t length,
                                                     size_t remainingByteCount,
                                                     uint8_t opcode, bool fin, void* user)
    {
        // WebSocketProtocol expects this method to return true on error, but this confuses me
        // so I'm having my code return false on error, hence the `!`. --jpa
        return !USER_SOCK->handleFragment(data, length, remainingByteCount, opcode, fin);
    }


    // Explicitly generate code for template methods:

//    template class WebSocketProtocol<SERVER>;
    template class WebSocketProtocol<CLIENT>;
}  // namespace uWS
