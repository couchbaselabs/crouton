//
// WebSocket.cc
//
// 
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

#include "WebSocket.hh"
#include "UVInternal.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#endif

#include "websocket.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace snej::coro::uv {

    WebSocket::WebSocket(std::string urlStr)
    :_url(std::move(urlStr))
    ,_handle(new tlsuv_websocket_t)
    {
        check(tlsuv_websocket_init(curLoop(), _handle), "creating WebSocket");
    }

    void WebSocket::setHeader(const char* name, const char* value) {
        tlsuv_websocket_set_header(_handle, name, value);
    }

    class ws_connect_request : public connect_request {
    public:
        explicit ws_connect_request(tlsuv_websocket_t* wsHandle) :_wsHandle(wsHandle) { }

        static void callbackWithStatus(uv_connect_t *req, int status) {
            auto self = static_cast<ws_connect_request*>(req);
            if (status >= -1) {
                // If the callback succeeded or returned generic error -1,
                // return the HTTP status instead if there is one:
                if (int httpStatus = self->_wsHandle->req->resp.code; httpStatus > 0)
                    status = httpStatus;
            }
            self->completed(status);
        }

        tlsuv_websocket_t* _wsHandle;
    };

    Future<HTTPStatus> WebSocket::connect() {
        auto onRead = [](uv_stream_t* stream, ssize_t nread, const uv_buf_t* buf) {
            auto ws = (WebSocket*)stream->data;
            ws->received(buf, nread);
        };

        ws_connect_request req(_handle);
        check(tlsuv_websocket_connect(&req, _handle, _url.c_str(), req.callbackWithStatus, onRead),
              "connecting WebSocket");
        int status = AWAIT req;
        check(status, "connecting WebSocket");
        RETURN HTTPStatus{status};
    }

    Future<void> WebSocket::send(const void* data, size_t len) {
        uv_buf_t buf{.base = (char*)data, .len = len};
        write_request req;
        check(tlsuv_websocket_write(&req, _handle, &buf, req.callbackWithStatus),
              "writing to WebSocket");
        check(AWAIT req, "writing to WebSocket");
    }

    Future<std::string> WebSocket::receive() {
        auto result = _nextIncoming.future();
        if (_nextIncoming.hasValue()) {
            if (_moreIncoming.empty()) {
                _nextIncoming.reset();
            } else {
                _nextIncoming = std::move(_moreIncoming.front());
                _moreIncoming.pop_front();
            }
        }
        return result;
    }

    void WebSocket::received(const uv_buf_t *buf, ssize_t nread) {
        FutureProvider<std::string> *provider;
        if (_nextIncoming.hasValue()) {
            _moreIncoming.emplace_back();
            provider = &_moreIncoming.back();
        } else {
            provider = &_nextIncoming;
        }

        if (nread > 0) {
            provider->setValue(std::string((const char*)buf, nread));
        } else if (nread < 0) {
            std::exception_ptr x = makeExceptionPtr(UVError("reading from WebSocket", int(nread)));
            provider->setException(x);
        }
    }

    void WebSocket::close() {
        closeHandle(_handle);
    }


}
