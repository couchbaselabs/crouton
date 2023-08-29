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

#include "HTTPClient.hh"
#include "UVInternal.hh"
#include <strings.h>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#endif

#include "http.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif


extern "C" void free_hdr_list(um_header_list *l);   // in tlsuv's http_req.c


static void move_hdr_list(um_header_list &dst, um_header_list &src) {
    dst = src;
    src.lh_first = nullptr;
    if (dst.lh_first)
        dst.lh_first->_next.le_prev = &dst.lh_first;
}


namespace crouton {
    using namespace std;

    HTTPClient::HTTPClient(std::string const& urlStr)
    :_client(new tlsuv_http_t)
    {
        check(tlsuv_http_init(curLoop(), _client, urlStr.c_str()),
              "creating an HTTP request");
    }

    void HTTPClient::cancelAll() {
        tlsuv_http_cancel_all(_client);
    }

    void HTTPClient::close() {
        if (_client) {
            tlsuv_http_close(_client, [](tlsuv_http_t *client) {delete client;});
            _client = nullptr;
        }
    }

    void HTTPClient::setHeader(const char* name, const char* value) {
        tlsuv_http_header(_client, name, value);
    }


#pragma mark - HTTP REQUEST:


    HTTPRequest::HTTPRequest(HTTPClient& client, const char* method, const char* path)
    :_client(client._client)
    {
        auto callback = [](tlsuv_http_resp_t *resp, void *ctx) {
            ((HTTPRequest*)ctx)->callback(resp);
        };
        _req = tlsuv_http_req(_client, method, path, callback, this);
        assert(_req);
    }

    HTTPRequest::~HTTPRequest() {
        cancel();
    }

    void HTTPRequest::setHeader(const char* name, const char* value) {
        tlsuv_http_req_header(_req, name, value);
    }

    Future<void> HTTPRequest::writeToBody(std::string_view body) {
        assert(!_bodyFuture.hasValue());
        auto callback = [](tlsuv_http_req_t *req, const char *body, ssize_t len) {
            auto request = (HTTPRequest*)req->data;
            auto bf = request->_bodyFuture;
            request->_bodyFuture = FutureProvider<void>();
            if (len >= 0)
                bf.setValue();
            else
                bf.setException(makeExceptionPtr(UVError("writing to HTTP request", int(len))));
        };
        check(tlsuv_http_req_data(_req, body.data(), body.size(), callback),
              "writing to an HTTP request");
        return _bodyFuture;
    }

    void HTTPRequest::endBody() {
        tlsuv_http_req_end(_req);
    }

    void HTTPRequest::cancel() {
        if (auto req = _req) {
            _req = nullptr;
            tlsuv_http_req_cancel(_client, req);
        }
    }

    Future<HTTPResponse> HTTPRequest::response() {
        return _responseFuture;
    }

    void HTTPRequest::callback(tlsuv_http_resp_t* response) {
        _req = nullptr; // will be freed by tlsuv after response is complete
        _responseFuture.setValue(HTTPResponse(response));
    }


#pragma mark - HTTP RESPONSE:


    HTTPResponse::HTTPResponse(tlsuv_http_resp_s* res)
    :status(HTTPStatus{res->code})
    ,statusMessage(res->status)
    ,_headers(res->headers.lh_first)
    {
        _res = res;
        _res->req->data = this;
        _res->body_cb = [](tlsuv_http_req_t *req, const char *body, ssize_t len) {
            if (auto response = (HTTPResponse*)req->data)
                response->bodyCallback(body, len);
        };
    }

    // HTTPResponse is the payload of a Future so it has to be movable, which complicates things
    HTTPResponse::HTTPResponse(HTTPResponse&& other)
    :status(other.status)
    ,statusMessage(std::move(other.statusMessage))
    ,_res(other._res)
    ,_bodyFuture(std::move(other._bodyFuture))
    ,_partialBody(std::move(other._partialBody))
    {
        other._res = nullptr;
        if (_res)
            _res->req->data = this;
        if (_res) {
            _headers = other._headers;
            other._headers = nullptr;
        } else
            move_hdr_list((um_header_list&)_headers, (um_header_list&)other._headers);
    }

    HTTPResponse::~HTTPResponse() {
        if (_res) {
            if (_res->req->data == this) {
                _res->req->data = nullptr;
                _res->body_cb = nullptr;
            }
        } else if (_headers) {
            free_hdr_list((um_header_list*)&_headers);
        }
    }

    void HTTPResponse::detach() {
        if (_res) {
            // Move the response's linked list of headers to myself:
            move_hdr_list((um_header_list&)_headers, _res->headers);
        }
        _res = nullptr;
    }

    std::string_view HTTPResponse::getHeader(const char* name) {
        for (tlsuv_http_hdr* hdr = _headers; hdr; hdr = hdr->_next.le_next) {
            if (0 == strcasecmp(hdr->name, name))
                return hdr->value;
        }
        return "";
    }

    Generator<pair<string_view, string_view>> HTTPResponse::headers() {
        for (tlsuv_http_hdr* hdr = _headers; hdr; hdr = hdr->_next.le_next) {
            YIELD pair<string_view, string_view>{hdr->name, hdr->value};
        }
    }

    Future<string> HTTPResponse::readBody() {
        if (_bodyFuture.hasValue()) {
            // Entire body was already read:
            auto result = _bodyFuture.future();
            _bodyFuture = FutureProvider<string>();
            return result;
        } else if (_partialBody.empty() && _res) {
            // Wait for data to arrive:
            _readPartialBody = true;
            return _bodyFuture;
        } else {
            // Return data that's arrived since last time:
            return std::move(_partialBody);
        }
    }

    Future<string> HTTPResponse::entireBody() {
        _readPartialBody = false;
        if (!_res && !_bodyFuture.hasValue())
            _bodyFuture.setValue("");
        return _bodyFuture;
    }

    void HTTPResponse::bodyCallback(const char *body, ssize_t len) {
        // Note that I have no control over when or how often this is called.
        // It's going to receive data whether or not readBody() or entireBody() were called.
        if (len > 0) {
            _partialBody.append(body, len);
            if (_readPartialBody) {
                _readPartialBody = false;
                _bodyFuture.setValue(std::move(_partialBody));
                _bodyFuture = FutureProvider<string>(); // prepare for more
            }
        } else if (len == UV_EOF) {
            detach();   // tlsuv frees _res after this callback
            _bodyFuture.setValue(std::move(_partialBody));
        } else if (len < 0) {
            detach();
            _bodyFuture.setException(makeExceptionPtr(UVError("reading HTTP response", int(len))));
        }
    }

}
