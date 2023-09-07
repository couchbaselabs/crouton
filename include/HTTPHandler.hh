//
// HTTPHandler.hh
//
// 
//

#pragma once
#include "HTTPParser.hh"
#include "IStream.hh"
#include "ISocket.hh"
#include "Task.hh"
#include <regex>

namespace crouton {

    /** An HTTP server's connection to a client,
        from which it will read a request and send a response.
        @note  It does not support keep-alive, so it closes the socket after one response. */
    class HTTPHandler {
    public:

        /// An HTTP request as sent to a HandlerFunction function.
        struct Request {
            HTTPMethod  method = HTTPMethod::GET;   ///< The request method
            URL         uri;                        ///< The request URI (path + query.)
            HTTPHeaders headers;                    ///< The request headers.
            std::string body;                       ///< The request body.
        };


        /// An HTTP response for a HandlerFunction function to define.
        class Response {
        public:
            HTTPStatus  status = HTTPStatus::OK;    ///< Can change this before calling writeToBody
            std::string statusMessage;              ///< Can change this before calling writeToBody

            /// Adds a response header.
            void writeHeader(std::string_view name, std::string_view value);

            /// Writes to the body. After this you can't call writeHeader any more.
            [[nodiscard]] Future<void> writeToBody(std::string);

            /// The socket's stream. Only use this when bypassing HTTP, e.g. for WebSockets.
            [[nodiscard]] Future<IStream*> rawStream();

        private:
            friend class HTTPHandler;
            Response(HTTPHandler*, HTTPHeaders&&);
            [[nodiscard]] Future<void> finishHeaders();

            HTTPHandler* _handler;
            HTTPHeaders  _headers;
            bool         _sentHeaders = false;
        };


        /// A function that handles a request, writing a response.
        using HandlerFunction = std::function<Future<void>(Request const&, Response&)>;

        /// An HTTP method and path pattern, with the function that should be called.
        using Route = std::tuple<HTTPMethod,std::regex,HandlerFunction>;

        /// Constructs an HTTPHandler on a socket, given its routing table.
        explicit HTTPHandler(std::shared_ptr<ISocket>, std::vector<Route> const&);

        /// Reads the request, calls the handler (or writes an error) and closes the socket.
        [[nodiscard]] Future<void> run();

    private:
        [[nodiscard]] Future<void> writeHeaders(HTTPStatus status,
                                                std::string_view statusMsg,
                                                HTTPHeaders const& headers);
        [[nodiscard]] Future<void> writeToBody(std::string);
        [[nodiscard]] Future<void> endBody();

        std::shared_ptr<ISocket> _socket;
        IStream&                 _stream;
        HTTPParser               _parser;
        std::vector<Route> const&_routes;
        std::optional<Request>   _request;
        std::optional<Response>  _response;
    };

}
