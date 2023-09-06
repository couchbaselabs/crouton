//
// HTTPConnection.hh
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
#include "HTTPParser.hh"
#include "IStream.hh"
#include "ISocket.hh"
#include "UVBase.hh"
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace crouton {
    struct HTTPRequest;
    class HTTPResponse;

    /** An HTTP connection to a server, from which multiple requests can be made.
        This object must remain valid as long as any HTTPRequest created from it exists. */
    class HTTPConnection {
    public:
        /// Constructs a client that connects to the given host and port with HTTP or HTTPS.
        /// The URL's path, if any, becomes a prefix to that of all HTTPRequests.
        explicit HTTPConnection(URL);
        explicit HTTPConnection(std::string_view urlStr);

        ~HTTPConnection()       {close();}

        void close();

        /// Sends a request, returning the response.
        /// @note Currently, an HTTPConnection can only send a single request.
        Future<HTTPResponse> send(HTTPRequest&);

        /// Sends a default GET request to the URI given by the constructor.
        /// @note Currently, an HTTPConnection can only send a single request.
        Future<HTTPResponse> send();

    private:
        friend class HTTPResponse;
        Future<void> closeResponse();

        URL                      _url;
        std::unique_ptr<ISocket> _socket;
        IStream*                 _stream = nullptr;
        bool                     _sent = false;
    };



    /** An HTTP request to send on an HTTPConnection. */
    struct HTTPRequest {
        HTTPMethod method = HTTPMethod::GET;    ///< The request method
        std::string uri;                        ///< The request URI (path + query.)
        HTTPHeaders headers;                    ///< The request headers.
        std::string body;                       ///< The request body.
        std::shared_ptr<IStream> bodyStream;    ///< Stream to read body from (sent after `body`)

        /// Writes the request line & headers for transmission.
        /// Doesn't include the trailing CRLF after the headers, so more can be appended.
        friend std::ostream& operator<< (std::ostream&, HTTPRequest const&);
    };


    /** The response to an HTTPRequest. */
    class HTTPResponse : public IStream {
    public:
        explicit HTTPResponse(HTTPConnection&);
        HTTPResponse(HTTPResponse&&);

        /// The HTTP status code.
        HTTPStatus status() const               {return _parser.status;}

        /// The HTTP status message.
        std::string const& statusMessage() const {return _parser.statusMessage;}

        /// The response headers.
        HTTPHeaders const& headers() const      {return _parser.headers;}

        Future<void> open() override            {return _parser.readHeaders();}
        bool isOpen() const override            {return _parser.status != HTTPStatus::Unknown;}
        [[nodiscard]] Future<void> close() override;
        [[nodiscard]] Future<void> closeWrite() override;

        /// The HTTPConnection's raw socket stream.
        /// Only for use when upgrading protocols (i.e. to WebSocket.)
        IStream& upgradedStream();

    private:
        [[nodiscard]] Future<ConstBuf> _readNoCopy(size_t maxLen) override;
        Future<void> _write(ConstBuf) override;

        HTTPConnection* _connection;
        HTTPParser _parser;
        std::string _buf;
        size_t _bufUsed = 0;
    };

}
