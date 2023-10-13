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

#include "io/HTTPHandler.hh"
#include "Logging.hh"
#include <llhttp.h>
#include <sstream>

namespace crouton::io::http {
    using namespace std;

    Handler::Handler(std::shared_ptr<ISocket> socket, vector<Route> const& routes)
    :_socket(std::move(socket))
    ,_stream(_socket->stream())
    ,_parser(_stream, Parser::Request)
    ,_routes(routes)
    { }


    Future<void> Handler::run() {
        // Read the request:
        AWAIT _parser.readHeaders();

        auto uri = _parser.requestURI.value();
        string path(uri.path);
        LNet->info("HTTPHandler: Request is {} {}", _parser.requestMethod, string(uri));

        Headers responseHeaders;
        responseHeaders.set("User-Agent", "Crouton");
        responseHeaders.set("Connection", "close");

        // Find a matching route:
        auto status = Status::MethodNotAllowed;
        for (auto &route : _routes) {
            if (route.method == _parser.requestMethod) {
                status = Status::NotFound;
                if (regex_match(path, route.pathPattern)) {
                    // Call the handler:
                    AWAIT handleRequest(std::move(responseHeaders), route.handler);
                    RETURN noerror;
                }
            }
        }

        // No matching route; return an error:
        AWAIT writeHeaders(status, "", responseHeaders);
        AWAIT endBody();
        RETURN noerror;
    }


    Future<void> Handler::handleRequest(Headers responseHeaders,
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
        RETURN noerror;
    }


    Future<void> Handler::writeHeaders(Status status,
                                           string_view statusMsg,
                                           Headers const& headers)
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


    Future<void> Handler::writeToBody(string str) {
        return _stream.write(std::move(str));   //TODO: Write via the Parser
    }


    Future<void> Handler::endBody() {
        return _stream.close();
    }


#pragma mark - RESPONSE:


    Handler::Response::Response(Handler* h, Headers&& headers)
    :_handler(h)
    ,_headers(std::move(headers))
    { }

    void Handler::Response::writeHeader(string_view name, string_view value) {
        precondition(!_sentHeaders);
        _headers.set(string(name), string(value));
    }

    Future<void> Handler::Response::writeToBody(string str) {
        AWAIT finishHeaders();
        AWAIT _handler->writeToBody(std::move(str));
        RETURN noerror;
    }

    Future<void> Handler::Response::finishHeaders() {
        if (!_sentHeaders) {
            LNet->info("HTTPHandler: Sending {} response", status);
            AWAIT _handler->writeHeaders(status, statusMessage, _headers);
        }
        _sentHeaders = true;
        RETURN noerror;
    }

    Future<IStream*> Handler::Response::rawStream() {
        AWAIT finishHeaders();
        RETURN &_handler->_stream;
    }

}
