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
#include "Logging.hh"
#include "StringUtils.hh"
#include "UVInternal.hh"
#include "WebSocketProtocol.hh"
#include <mbedtls/base64.h>
#include <mbedtls/sha1.h>

#include <iomanip>

namespace crouton {
    string ErrorDomainInfo<ws::CloseCode>::description(errorcode_t code) {
        static constexpr NameEntry names[] = {
            {1001, "Server Going Away"},
            {1002, "Protocol Error"},
            {1003, "Invalid Data"},
            {1005, "Closed With No Reason"},
            {1006, "Unexpected Disconnect"},
            {1007, "Bad Message Format"},
            {1008, "Policy Error"},
            {1009, "Message Too Big"},
            {1010, "Missing Extension"},
            {1011, "Can't Fulfill a Request"},
            {1015, "TLS Error"},
            {4001, "Transient Error"},
            {4002, "Permanent Error"},
        };
        return NameEntry::lookup(code, names);
    }
}

namespace crouton::ws {
    using namespace std;


    WebSocket::~WebSocket() {disconnect();}


    Future<void> WebSocket::close() {
        if (_stream) {
            if (!_closeReceived)
                LNet->warn("WebSocket::close called before receiving a Close msg");
            if (!_incoming.empty())
                LNet->warn("WebSocket closing with {} unread incoming messages",
                        _incoming.size());
            AWAIT _stream->close();
            _stream = nullptr;
        }
        RETURN noerror;
    }


    void WebSocket::disconnect() {
        if (!_incoming.empty())
            LNet->warn("WebSocket disconnected with {} unread incoming messages",
                    _incoming.size());
        _stream = nullptr;
    }


    Future<void> WebSocket::send(ConstBytes message, Message::Type type) {
        // Note: This method is not a coroutine, and it passes the message to _stream->write
        // as a `string`, so the caller does not need to keep the data valid.
        if (_closeSent || !_stream)
            Error::raise(CroutonError::LogicError, "WebSocket is already closing");
        if (type == Message::Close)
            _closeSent = true;
        string frame(message.size() + 10, 0);
        size_t frameLen = formatMessage(frame.data(), message, type);
        frame.resize(frameLen);
        return _stream->write(frame);
    }


    Future<Message> WebSocket::receive() {
        if (_closeReceived || !_stream)
            RETURN Error(CroutonError::InvalidState, "WebSocket is closed");
        while (true) {
            while (_incoming.empty()) {
                ConstBytes data = AWAIT _stream->readNoCopy(100000);
                if (data.size() == 0)
                    RETURN Message(CloseCode::Abnormal, "WebSocket closed unexpectedly");
                // Pass the data to the 3rd-party WebSocket parser, which will call handleFragment.
                consume(data);
            }

            Message msg = std::move(_incoming.front());
            _incoming.pop_front();

            using enum Message::Type;
            switch (msg.type) {
                case Close:
                    _closeReceived = true;
                    [[fallthrough]];
                case Text:
                case Binary:
                    RETURN msg;     // Got a message!
                case Ping:
                    (void) send(ConstBytes{}, Pong);
                    break;
                case Pong:
                    //TODO: Send periodic Pings and disconnect if no Pong received in time
                default:
                    LNet->warn("WebSocket received unknown message type {}", int(msg.type));
                    break;
            }
        }
    }



    // Called from inside consume(), called by receive(), above.
    // A single receive might result in multiple messages, so we queue them in `_incoming`.
    bool WebSocket::handleFragment(byte* data,
                                   size_t dataLen,
                                   size_t remainingBytes,
                                   uint8_t opCode,
                                   bool fin)
    {
        // Beginning:
        if (!_curMessage) {
            _curMessage.emplace();
            _curMessage->reserve(dataLen + remainingBytes);
            _curMessage->type = (Message::Type)opCode;
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
    void WebSocket::protocolError(string_view message) {
        Error::raise(CloseCode::ProtocolError, message);
    }


    // Peer sent a CLOSE message, either initiating or responding.
    Future<void> WebSocket::handleCloseMessage(Message const& msg) {
        if ( _closeReceived )
            return Future<void>{};
        _closeReceived = true;
        if ( _closeSent ) {
            // I initiated the close; the peer has confirmed, so disconnect the socket now:
            LNet->warn("Close confirmed by peer; disconnecting socket now");
            return close();
        } else {
            // Peer is initiating a close; echo it:
            LNet->warn("Peer sent {}; echoing it", msg);
            return send(std::move(msg));
        }
    }


#pragma mark - CLIENT WEBSOCKET:


    ClientWebSocket::ClientWebSocket(string urlStr)
    :_connection(urlStr)
    ,_clientParser(make_unique<uWS::ClientProtocol>())
    {
        // Generate a base64-encoded 16-byte random `Sec-WebSocket-Key`:
        uint8_t rawKey[16];
        Randomize(&rawKey, sizeof(rawKey));
        char key[100];
        size_t len;
        mbedtls_base64_encode((uint8_t*)key, sizeof(key), &len, rawKey, sizeof(rawKey));
        key[len] = '\0';
        _accept = generateAcceptResponse(key);

        // Create request headers:
        _request.headers.set("Connection",            "Upgrade");
        _request.headers.set("Upgrade",               "WebSocket");
        _request.headers.set("Sec-WebSocket-Version", "13");
        _request.headers.set("Sec-WebSocket-Key",     string(key));
    }


    // Compute the `Sec-WebSocket-Accept` response the server should generate from the key:
    string ClientWebSocket::generateAcceptResponse(const char* key) {
        string input = string(key) + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
        uint8_t digest[20];
        mbedtls_sha1((const uint8_t*)input.data(), input.size(), digest);
        char accept[60];
        size_t len;
        mbedtls_base64_encode((uint8_t*)accept, sizeof(accept), &len, digest, sizeof(digest));
        return string(accept, len);
    }


    ClientWebSocket::~ClientWebSocket() = default;


    void ClientWebSocket::setHeader(const char* name, const char* value) {
        _request.headers.set(name, value);
    }


    Future<void> ClientWebSocket::connect() {
        http::Response response = AWAIT _connection.send(_request);

        _responseHeaders = response.headers();
        if (auto status = response.status(); status != http::Status::SwitchingProtocols)
            Error::raise(status, "Server returned wrong status for WebSocket upgrade");
        if (!equalIgnoringCase(_responseHeaders.get("Connection"), "upgrade") ||
                !equalIgnoringCase(_responseHeaders.get("Upgrade"), "websocket"))
            protocolError("Server did not upgrade to WebSocket protocol");
        if (_accept != _responseHeaders.get("Sec-WebSocket-Accept"))
           protocolError("Server returned wrong Sec-WebSocket-Accept value");

        _stream = &response.upgradedStream();
        RETURN noerror;
    }


    void ClientWebSocket::disconnect() {
        WebSocket::disconnect();
        _connection.close();
    }


    size_t ClientWebSocket::formatMessage(void* dst, ConstBytes message, Message::Type type) {
        return uWS::ClientProtocol::formatMessage((byte*)dst,
                                             (const char*)message.data(),
                                             message.size(),
                                             uWS::OpCode(type),
                                             message.size(),
                                             false);
    }


    void ClientWebSocket::consume(ConstBytes bytes) {
        _clientParser->consume((byte*)bytes.data(), bytes.size(), this);
    }


#pragma mark - SERVER WEBSOCKET:


    ServerWebSocket::ServerWebSocket()
    :_serverParser(make_unique<uWS::ServerProtocol>())
    { }

    ServerWebSocket::~ServerWebSocket() = default;


    bool ServerWebSocket::isRequestValid(http::Handler::Request const& request) {
        return request.method == http::Method::GET
        && request.headers.get("Sec-WebSocket-Key").size() == 24
        && equalIgnoringCase(request.headers.get("Connection"), "upgrade")
        && equalIgnoringCase(request.headers.get("Upgrade"), "WebSocket")
        && equalIgnoringCase(request.headers.get("Sec-WebSocket-Version"), "13");
    }


    Future<bool> ServerWebSocket::connect(http::Handler::Request const& request,
                                          http::Handler::Response& response,
                                          string_view subprotocol)
    {
        if (!isRequestValid(request)) {
            response.status = http::Status::BadRequest;
            response.writeHeader("Sec-WebSocket-Version", "13");
            response.statusMessage = "Invalid WebSocket handshake";
            RETURN false;
        }

        string key = request.headers.get("Sec-WebSocket-Key");
        string accept = ClientWebSocket::generateAcceptResponse(key.c_str());

        response.status = http::Status::SwitchingProtocols;
        response.writeHeader("Connection",           "Upgrade");
        response.writeHeader("Upgrade",              "WebSocket");
        response.writeHeader("Sec-WebSocket-Accept", accept);
        if (!subprotocol.empty())
            response.writeHeader("Sec-WebSocket-Protocol", subprotocol);
        
        // Send the response and take over the socket stream:
        _stream = AWAIT response.rawStream();
        RETURN true;
    }


    size_t ServerWebSocket::formatMessage(void* dst, ConstBytes message, Message::Type type) {
        return uWS::ServerProtocol::formatMessage((byte*)dst,
                                                  (const char*)message.data(),
                                                  message.size(),
                                                  uWS::OpCode(type),
                                                  message.size(),
                                                  false);
    }


    void ServerWebSocket::consume(ConstBytes bytes) {
        _serverParser->consume((byte*)bytes.data(), bytes.size(), this);
    }


#pragma mark - MESSAGE:


    Message::Message(CloseCode code, string_view message)
    :string(message.size() + 2, 0)
    ,type(Close)
    {
        size_t sz = uWS::ClientProtocol::formatClosePayload((byte*)data(),
                                                            uint16_t(code),
                                                            message.data(), message.size());
        assert(sz <= size());
        resize(sz);
    }

    CloseCode Message::closeCode() const {
        if (type != Close)
            Error::raise(CroutonError::InvalidArgument, "Not a CLOSE message");
        auto payload = uWS::ClientProtocol::parseClosePayload((byte*)data(), size());
        return payload.code ? CloseCode(payload.code) : CloseCode::NoCode;
    }

    string_view Message::closeMessage() const {
        if (type != Close)
            Error::raise(CroutonError::InvalidArgument, "Not a CLOSE message");
        auto payload = uWS::ClientProtocol::parseClosePayload((byte*)data(), size());
        return {(char*)payload.message, payload.length};
    }


    std::ostream& operator<< (std::ostream& out, Message const& msg) {
        using enum Message::Type;
        out << msg.type << '[';
        switch (msg.type) {
            case Text:
                out << std::quoted(msg);
                break;
            case Binary:
                out << msg.size();
                break;
            case Close:
                out << msg.closeCode();
                if (auto closeMsg = msg.closeMessage(); !closeMsg.empty())
                    out << ", " << std::quoted(closeMsg);
                break;
            default:
                break;
        }
        return out << ']';
    }


    std::ostream& operator<< (std::ostream& out, Message::Type type) {
        using enum Message::Type;
        static constexpr const char* kTypeNames[] = {
            nullptr, "Text", "Binary", nullptr, nullptr, nullptr, nullptr, nullptr,
            "Close", "Ping", "Pong"
        };
        if (type >= Text && type <= Pong && kTypeNames[type])
            return out << kTypeNames[type];
        else
            return out << "Op" << int(type);
    }


    std::ostream& operator<< (std::ostream& out, CloseCode code) {
        using enum CloseCode;
        static constexpr const char* kCodeNames[] = {
            "Normal",
            "GoingAway",
            "ProtocolError",
            "DataError",
            "1004",
            "NoCode",
            "Abnormal",
            "BadMessageFormat",
            "PolicyError",
            "MessageTooBig",
            "MissingExtension",
            "CantFulfill",
        };
        if (code >= Normal && code <= CantFulfill)
            return out << kCodeNames[int(code) - int(Normal)];
        else
            return out << int(code);
    }

}


#pragma mark - WEBSOCKETPROTOCOL


// The rest of the implementation of uWS::WebSocketProtocol, which calls into WebSocket:
namespace uWS {

    static constexpr size_t kMaxMessageLength = 1 << 20;


// The `user` parameter points to the owning WebSocketImpl object.
#define USER_SOCK ((crouton::ws::WebSocket*)user)

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
        USER_SOCK->protocolError("");
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

    template class WebSocketProtocol<false>;
    template class WebSocketProtocol<true>;
}  // namespace uWS
