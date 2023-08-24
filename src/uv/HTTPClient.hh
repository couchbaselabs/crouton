//
// HTTP.hh
//
// 
//

#pragma once
#include "UVBase.hh"
#include "Generator.hh"
#include <string_view>

struct tlsuv_http_s;
struct tlsuv_http_req_s;
struct tlsuv_http_resp_s;
struct tlsuv_http_hdr_s;

namespace snej::coro::uv {
    class HTTPRequest;
    class HTTPResponse;


    class HTTPClient {
    public:
        explicit HTTPClient(std::string const& urlStr);
        ~HTTPClient()       {close();}

        void cancelAll();
        void close();

        void setHeader(const char* name, const char* value);

    private:
        friend class HTTPRequest;

        tlsuv_http_s* _client;
    };



    class HTTPRequest {
    public:
        explicit HTTPRequest(HTTPClient&, const char* method, const char* path);
        ~HTTPRequest();
        void cancel();

        void setHeader(const char* name, const char* value);
        Future<void> writeToBody(std::string_view);
        void endBody(); // "Only needed if `Transfer-Encoding` header was set to `chunked`"

        Future<HTTPResponse> response();

    private:
        void callback(tlsuv_http_resp_s*);

        tlsuv_http_s*       _client;
        tlsuv_http_req_s*   _req;
        FutureProvider<void> _bodyFuture;
        FutureProvider<HTTPResponse> _responseFuture;
    };



    class HTTPResponse {
    public:
        int status;
        std::string statusMessage;

        std::string_view getHeader(const char* name);
        Generator<std::pair<std::string_view, std::string_view>> headers();

        Future<std::string> body();

        HTTPResponse(HTTPResponse&&);
        ~HTTPResponse();

    private:
        friend class HTTPRequest;
        explicit HTTPResponse(tlsuv_http_resp_s*, bool hasBody);
        HTTPResponse(HTTPResponse const&) =delete;
        void bodyCallback(const char *body, ssize_t len);
        void detach();

        tlsuv_http_resp_s* _res = nullptr;
        tlsuv_http_hdr_s* _headers = nullptr;
        FutureProvider<std::string> _bodyFuture;
        std::string _partialBody;
    };

}
