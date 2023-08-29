//
// HTTP.hh
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
#include "UVBase.hh"
#include "Generator.hh"
#include <string>
#include <string_view>

struct tlsuv_http_s;
struct tlsuv_http_req_s;
struct tlsuv_http_resp_s;
struct tlsuv_http_hdr_s;

namespace crouton {
    class HTTPResponse;

    enum class HTTPStatus : int {
        Unknown = 0,
        Connected = 101,
        OK = 200,
        MovedPermanently = 301,
        NotFound = 404,
        ServerError = 500,
    };


    /** An HTTP connection to a server, from which multiple requests can be made.
        This object must remain valid as long as any HTTPRequest created from it exists. */
    class HTTPClient {
    public:
        /// Constructs a client that connects to the given host and port with HTTP or HTTPS.
        /// The URL's path, if any, becomes a prefix to that of all HTTPRequests.
        explicit HTTPClient(std::string const& urlStr);

        ~HTTPClient()       {close();}

        void cancelAll();
        void close();

        void setHeader(const char* name, const char* value);

    private:
        friend class HTTPRequest;

        tlsuv_http_s* _client;
    };



    /** An HTTP request made on an HTTPClient connection. */
    class HTTPRequest {
    public:
        /// Creates an HTTP request to the HTTPClient's server.
        explicit HTTPRequest(HTTPClient&, const char* method, const char* path);
        ~HTTPRequest();

        /// Stops a request.
        void cancel();

        /// Sets the value of a request header.
        void setHeader(const char* name, const char* value);

        /// Writes data to the request body.
        Future<void> writeToBody(std::string_view);

        /// Signals that the body is complete.
        /// Only needed if `Transfer-Encoding` header was set to `chunked`.
        void endBody();

        /// Asynchronously returns the response.
        Future<HTTPResponse> response();

    private:
        void callback(tlsuv_http_resp_s*);

        tlsuv_http_s*                _client;           // The client handle
        tlsuv_http_req_s*            _req;              // The request handle
        FutureProvider<void>         _bodyFuture;       // Resolves when body chunk written
        FutureProvider<HTTPResponse> _responseFuture;   // The response, when it arrives
    };


    /** The response to an HTTPRequest. */
    class HTTPResponse {
    public:
        /// The HTTP status code.
        HTTPStatus status;

        /// The HTTP status message.
        std::string statusMessage;

        /// Returns the value of an HTTP response header. (Case-insensitive.)
        std::string_view getHeader(const char* name);

        /// Returns a Generator that yields all the response headers as {name,value} pairs.
        Generator<std::pair<std::string_view, std::string_view>> headers();

        /// Reads from the response body and returns some more data.
        /// On EOF returns an empty string.
        Future<std::string> readBody();

        /// Reads and returns the entire body.
        /// If readBody() has already been called, this will return the remainder.
        Future<std::string> entireBody();

        HTTPResponse(HTTPResponse&&);
        ~HTTPResponse();

    private:
        friend class HTTPRequest;
        explicit HTTPResponse(tlsuv_http_resp_s*);
        HTTPResponse(HTTPResponse const&) =delete;
        void bodyCallback(const char *body, ssize_t len);
        void detach();

        tlsuv_http_resp_s*          _res = nullptr;     // The response handle
        tlsuv_http_hdr_s*           _headers = nullptr; // Really a um_header_list
        FutureProvider<std::string> _bodyFuture;        // Will receive entire or partial body
        std::string                 _partialBody;       // Body data accumulated by callback
        bool                        _readPartialBody = false; // caller called readBody()
    };

}
