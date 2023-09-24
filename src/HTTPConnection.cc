//
// HTTPClient.cc
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

#include "HTTPConnection.hh"
#include "NWConnection.hh"
#include "TLSSocket.hh"
#include "TCPSocket.hh"
#include "UVInternal.hh"
#include "llhttp.h"
#include <sstream>

namespace crouton::http {
    using namespace std;


    std::ostream& operator<< (std::ostream& out, Request const& req) {
        out << llhttp_method_name(llhttp_method_t(req.method)) << ' ';
        if (!req.uri.starts_with('/'))
            out << '/';
        out << req.uri << " HTTP/1.1\r\n";
        for (auto &h : req.headers)
            out << h.first << ": " << h.second << "\r\n";
        return out;
    }


    Connection::Connection(URL url)
    :_url(std::move(url))
    {
        if (_url.hostname.empty())
            Error::raise(CroutonError::InvalidArgument, "HTTPConnection URL must have a hostname");
        if (!_url.query.empty())
            Error::raise(CroutonError::InvalidArgument, "HTTPConnection URL must not have a query");
        bool tls;
        if (string scheme = _url.normalizedScheme(); scheme == "http" || scheme == "ws")
            tls = false;
        else if (scheme == "https" || scheme == "wss")
            tls = true;
        else
            Error::raise(CroutonError::InvalidArgument, "Non-HTTP URL");
        uint16_t port = _url.port;
        if (port == 0)
            port = tls ? 443 : 80;

        // Create the socket:
        _socket = ISocket::newSocket(tls);
        _socket->bind(string(_url.hostname), port);
        _stream = &_socket->stream();
    }


    Connection::Connection(string_view url) :Connection(URL(url)) { }


    Future<Response> Connection::send(Request& req) {
        //TODO: Support multiple requests over same socket using keepalive.
        if (_sent)
            RETURN Error(CroutonError::LogicError, "HTTPConnection can only send one request, for now");
        _sent = true;

        if (req.method == Method::GET) {
            if (req.bodyStream || !req.body.empty())
                RETURN Error(CroutonError::InvalidArgument, "GET request may not have a body");
        } else {
            if (req.bodyStream && !req.headers.contains("Content-Length"))
                RETURN Error(CroutonError::InvalidArgument, "HTTPRequest with body stream must have a Content-Length");
        }

        if (!_socket->isOpen())
            AWAIT _socket->open();

        // Prepend my URL's path, if any, to the request uri:
        if (!req.uri.empty() && !req.uri.starts_with('/'))
            req.uri.insert(0, 1, '/');
        if (string_view path = _url.path; !path.empty()) {
            if (path.ends_with('/'))
                path = path.substr(0, path.size() - 1);
            req.uri.insert(0, path);
        }

        // Send the request:
        {
            stringstream out;
            out << req;
            out << "Host: " << _url.hostname << "\r\n";
            out << "Connection: close\r\n";
            if (req.method != Method::GET && !req.bodyStream) {
                assert(!req.headers.contains("Content-Length"));
                out << "Content-Length: " << req.body.size() << "\r\n";
            }
            out << "\r\n";
            AWAIT _stream->write(out.str());
        }

        // Send the request body:
        if (!req.body.empty())
            AWAIT _stream->write(req.body);
        if (req.bodyStream) {
            while (true) {
                if (ConstBytes buf = AWAIT req.bodyStream->readNoCopy(); buf.size() > 0)
                    AWAIT _stream->write(buf);
                else
                    break;
            }
        }

        // Now create the response and read the headers:
        Response response(*this);
        AWAIT response.open();
        RETURN response;
    }


    Future<Response> Connection::send() {
        Request req;
        return send(req);
    }


    void Connection::close() {
        ISocket::closeAndFree(std::move(_socket));
    }


    Future<void> Connection::closeResponse() {
        return _stream->close();
    }



#pragma mark - HTTP RESPONSE:


    Response::Response(Connection& connection)
    :_connection(&connection)
    ,_parser(*connection._stream, Parser::Response)
    { }

    Response::Response(Response&& r) noexcept
    :_connection(r._connection)
    ,_parser(std::move(r._parser))
    { }

    Response& Response::operator=(Response&& r) noexcept {
        _connection = r._connection;
        _parser = std::move(r._parser);
        return *this;
    }

    Future<void> Response::close() {
        return _connection->closeResponse();
    }

    Future<void> Response::closeWrite() {
        return Error(CroutonError::LogicError, "HTTPReponse is not writeable");
    }

    Future<ConstBytes> Response::readNoCopy(size_t maxLen) {
        if (_bufUsed >= _buf.size()) {
            _buf = AWAIT _parser.readBody();
            _bufUsed = 0;
        }
        ConstBytes result(&_buf[_bufUsed], std::min(maxLen, _buf.size() - _bufUsed));
        _bufUsed += result.size();
        RETURN result;
    }

    Future<ConstBytes> Response::peekNoCopy() {
        if (_bufUsed >= _buf.size()) {
            _buf = AWAIT _parser.readBody();
            _bufUsed = 0;
        }
        RETURN ConstBytes(&_buf[_bufUsed], _buf.size() - _bufUsed);
    }

    Future<void> Response::write(ConstBytes) {
        RETURN Error(CroutonError::LogicError, "HTTPReponse is not writeable");
    }

    IStream& Response::upgradedStream() {
        assert(_parser.upgraded());
        return *_connection->_stream;
    }

}
