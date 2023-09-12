//
// HTTPHandler.cc
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

#include "HTTPHandler.hh"
#include "Logging.hh"
#include <llhttp.h>
#include <sstream>

namespace crouton {
    using namespace std;

    HTTPHandler::HTTPHandler(std::shared_ptr<ISocket> socket, vector<Route> const& routes)
    :_socket(std::move(socket))
    ,_stream(_socket->stream())
    ,_parser(_stream, HTTPParser::Request)
    ,_routes(routes)
    { }


    Future<void> HTTPHandler::run() {
        // Read the request:
        AWAIT _parser.readHeaders();

        auto uri = _parser.requestURI.value();
        string path(uri.path);
        spdlog::info("HTTPHandler: Request is {} {}", _parser.requestMethod, string(uri));

        HTTPHeaders responseHeaders;
        responseHeaders.set("User-Agent", "Crouton");
        responseHeaders.set("Connection", "close");

        // Find a matching route:
        auto status = HTTPStatus::MethodNotAllowed;
        for (auto &route : _routes) {
            if (route.method == _parser.requestMethod) {
                status = HTTPStatus::NotFound;
                if (regex_match(path, route.pathPattern)) {
                    // Call the handler:
                    AWAIT handleRequest(std::move(responseHeaders), route.handler);
                    RETURN;
                }
            }
        }

        // No matching route; return an error:
        AWAIT writeHeaders(status, "", responseHeaders);
        AWAIT endBody();
    }


    Future<void> HTTPHandler::handleRequest(HTTPHeaders responseHeaders,
                                            HandlerFunction const& handler)
    {
        string body = AWAIT _parser.entireBody();   //TODO: Let handler fn read at its own pace
        Request request {
            _parser.requestMethod,
            _parser.requestURI.value(),
            _parser.headers,
            std::move(body)
        };
        Response response(this, std::move(responseHeaders));
        Future<void> handled = handler(request, response); // split in 2 lines bc MSVC bug
        AWAIT handled;
        AWAIT response.finishHeaders();
        AWAIT endBody();
        RETURN;
    }


    Future<void> HTTPHandler::writeHeaders(HTTPStatus status,
                                           string_view statusMsg,
                                           HTTPHeaders const& headers)
    {
        if (statusMsg.empty())
            statusMsg = llhttp_status_name(llhttp_status_t(status));
        stringstream out;
        out << "HTTP/1.1 " << int(status) << ' ' << statusMsg << "\r\n";
        for (auto &h : headers)
            out << h.first << ": " << h.second << "\r\n";
        out << "\r\n";
        return _stream.write(out.str());
    }


    Future<void> HTTPHandler::writeToBody(string str) {
        return _stream.write(std::move(str));   //TODO: Write via the Parser
    }


    Future<void> HTTPHandler::endBody() {
        return _stream.close();
    }


#pragma mark - RESPONSE:


    HTTPHandler::Response::Response(HTTPHandler* h, HTTPHeaders&& headers)
    :_handler(h)
    ,_headers(std::move(headers))
    { }

    void HTTPHandler::Response::writeHeader(string_view name, string_view value) {
        assert(!_sentHeaders);
        _headers.set(string(name), string(value));
    }

    Future<void> HTTPHandler::Response::writeToBody(string str) {
        AWAIT finishHeaders();
        AWAIT _handler->writeToBody(std::move(str));
    }

    Future<void> HTTPHandler::Response::finishHeaders() {
        if (!_sentHeaders) {
            spdlog::info("HTTPHandler: Sending {} response", status);
            AWAIT _handler->writeHeaders(status, statusMessage, _headers);
        }
        _sentHeaders = true;
    }

    Future<IStream*> HTTPHandler::Response::rawStream() {
        AWAIT finishHeaders();
        RETURN &_handler->_stream;
    }

}
