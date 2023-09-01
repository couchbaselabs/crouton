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
#include "TLSSocket.hh"
#include "TCPSocket.hh"
#include "UVInternal.hh"
#include "llhttp.h"
#include <sstream>
#include <strings.h>

namespace crouton {
    using namespace std;


    std::ostream& operator<< (std::ostream& out, HTTPRequest const& req) {
        out << llhttp_method_name(llhttp_method_t(req.method)) << ' ';
        if (!req.uri.starts_with('/'))
            out << '/';
        out << req.uri << " HTTP/1.1\r\n";
        for (auto &h : req.headers)
            out << h.first << ": " << h.second << "\r\n";
        return out;
    }


    HTTPConnection::HTTPConnection(URL url)
    :_url(std::move(url))
    {
        if (_url.hostname.empty())
            throw invalid_argument("HTTPConnection URL must have a hostname");
        if (!_url.query.empty())
            throw invalid_argument("HTTPConnection URL must not have a query");
        bool tls;
        if (string scheme = _url.normalizedScheme(); scheme == "http" || scheme == "ws")
            tls = false;
        else if (scheme == "https" || scheme == "wss")
            tls = true;
        else
            throw invalid_argument("Non-HTTP URL");
        uint16_t port = _url.port;
        if (port == 0)
            port = tls ? 443 : 80;

        // Create the socket:       //FIXME: Don't hardcode the class names
        if (tls)
            _socket = make_unique<mbed::TLSSocket>();
        else
            _socket = make_unique<TCPSocket>();
        _socket->bind(string(_url.hostname), port);
        _stream = &_socket->stream();
    }


    HTTPConnection::HTTPConnection(string_view url) :HTTPConnection(URL(url)) { }


    Future<HTTPResponse> HTTPConnection::send(HTTPRequest& req) {
        //TODO: Support multiple requests over same socket using keepalive.
        if (_sent)
            throw std::logic_error("HTTPConnection can only send one request, for now");
        _sent = true;

        if (req.method == HTTPMethod::GET) {
            if (req.bodyStream || !req.body.empty())
                throw invalid_argument("GET request may not have a body");
        } else {
            if (req.bodyStream && !req.headers.contains("Content-Length"))
                throw invalid_argument("HTTPRequest with body stream must have a Content-Length");
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
            if (req.method != HTTPMethod::GET && !req.bodyStream) {
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
                if (ConstBuf buf = AWAIT req.bodyStream->readNoCopy(); buf.len > 0)
                    AWAIT _stream->write(buf);
                else
                    break;
            }
        }

        // Now create the response and read the headers:
        HTTPResponse response(*this);
        AWAIT response.open();
        RETURN response;
    }


    Future<HTTPResponse> HTTPConnection::send() {
        HTTPRequest req;
        return send(req);
    }


    void HTTPConnection::close() {
        (void) _stream->close();
    }


    Future<void> HTTPConnection::closeResponse() {
        return _stream->close();
    }



#pragma mark - HTTP RESPONSE:


    HTTPResponse::HTTPResponse(HTTPConnection& connection)
    :_connection(connection)
    ,_parser(*connection._stream, HTTPParser::Response)
    { }

    Future<void> HTTPResponse::close() {
        return _connection.closeResponse();
    }

    Future<void> HTTPResponse::closeWrite() {
        throw logic_error("HTTPReponse is not writeable");
    }

    Future<ConstBuf> HTTPResponse::_readNoCopy(size_t maxLen) {
        if (_bufUsed >= _buf.size()) {
            _buf = AWAIT _parser.readBody();
            _bufUsed = 0;
        }
        ConstBuf result(&_buf[_bufUsed], std::min(maxLen, _buf.size() - _bufUsed));
        _bufUsed += result.len;
        RETURN result;
    }

    Future<void> HTTPResponse::_write(ConstBuf) {
        throw logic_error("HTTPReponse is not writeable");
    }

    IStream& HTTPResponse::upgradedStream() {
        assert(_parser.upgraded());
        return *_connection._stream;
    }

}
