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
#include <iosfwd>
#include <memory>
#include <utility>

namespace crouton::io::http {
    struct Request;
    class Response;

    /** An HTTP client connection to a server, from which multiple requests can be made.
        This object must remain valid as long as any HTTPRequest created from it exists. */
    class Connection {
    public:
        /// Constructs a client that connects to the given host and port with HTTP or HTTPS.
        /// The URL's path, if any, becomes a prefix to that of all HTTPRequests.
        explicit Connection(URL);
        explicit Connection(string_view urlStr);

        ~Connection()       {close();}

        void close();

        /// Sends a request, returning the response.
        /// @note Currently, an HTTPConnection can only send a single request.
        ASYNC<Response> send(Request&);

        /// Sends a default GET request to the URI given by the constructor.
        /// @note Currently, an HTTPConnection can only send a single request.
        ASYNC<Response> send();

    private:
        friend class Response;
        ASYNC<void> closeResponse();

        URL                      _url;
        std::unique_ptr<ISocket> _socket;
        IStream*                 _stream = nullptr;
        bool                     _sent = false;
    };



    /** An HTTP request to send on an HTTPConnection. */
    struct Request {
        Method  method = Method::GET;   ///< The request method
        string      uri;                        ///< The request URI (path + query.)
        Headers headers;                    ///< The request headers.
        string      body;                       ///< The request body.
        std::shared_ptr<IStream> bodyStream;    ///< Stream to read body from (sent after `body`)

        /// Writes the request line & headers for transmission.
        /// Doesn't include the trailing CRLF after the headers, so more can be appended.
        friend std::ostream& operator<< (std::ostream&, Request const&);
    };


    /** The response received from an outgoing HTTPRequest. */
    class Response : public IStream {
    public:
        explicit Response(Connection&);
        Response(Response&&) noexcept;
        Response& operator=(Response&&) noexcept;

        /// The HTTP status code.
        Status status() const               {return _parser.status;}

        /// The HTTP status message.
        string const& statusMessage() const {return _parser.statusMessage;}

        /// The response headers.
        Headers const& headers() const      {return _parser.headers;}

        ASYNC<void> open() override             {return _parser.readHeaders();}
        bool isOpen() const override            {return _parser.status != Status::Unknown;}
        ASYNC<void> close() override;
        ASYNC<void> closeWrite() override;

        // IStream API:
        ASYNC<ConstBytes> readNoCopy(size_t maxLen = 65536) override;
        ASYNC<ConstBytes> peekNoCopy() override;
        ASYNC<void> write(ConstBytes) override;

        /// The HTTPConnection's raw socket stream.
        /// Only for use when upgrading protocols (i.e. to WebSocket.)
        IStream& upgradedStream();

    private:
        Connection* _connection;
        Parser      _parser;
        string          _buf;
        size_t          _bufUsed = 0;
    };

}
