//
// NWConnection.cc
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

#ifdef __APPLE__

#include "NWConnection.hh"
#include <Network/Network.h>
#include <algorithm>

namespace crouton::apple {
    using namespace std;

    template <typename T>
    struct NWAutoreleaser {
        NWAutoreleaser(T* t)  :_ref(t) { }
        NWAutoreleaser(NWAutoreleaser&& other) :_ref(other._ref) {other._ref = nullptr;}
        ~NWAutoreleaser()   {nw_release(_ref);}

        operator T*()       {return _ref;}
    private:
        T* _ref;
    };

    template <typename T>
    NWAutoreleaser<T> autorelease(T* t)   {return NWAutoreleaser<T>(t);}



    static string to_string(nw_error_t err) {
        const char* kDomains[4] = {"invalid", "POSIX", "DNS", "TLS"};
        auto domain = nw_error_get_error_domain(err);
        auto code = nw_error_get_error_code(err);
        char buf[50];
        snprintf(buf, sizeof(buf), "NWError(%s, %d)", kDomains[domain], code);
        return string(buf);
    }

    NWError::NWError(nw_error* err) :std::runtime_error(to_string(err)) { }



    NWConnection::~NWConnection() {
        nw_release(_conn);
        if (_queue) {
            dispatch_release(_queue);
        }
    }

    Future<void> NWConnection::open() {
        string portStr = std::to_string(_binding->port);
        nw_endpoint_t endpoint = nw_endpoint_create_host(_binding->address.c_str(), portStr.c_str());

        nw_parameters_configure_protocol_block_t tlsConfig;
        if (_useTLS)
            tlsConfig = NW_PARAMETERS_DEFAULT_CONFIGURATION;
        else
            tlsConfig = NW_PARAMETERS_DISABLE_PROTOCOL;
        auto params = autorelease(nw_parameters_create_secure_tcp(tlsConfig,
                                                            NW_PARAMETERS_DEFAULT_CONFIGURATION));
        //TODO: Set nodelay, keepalive based on _binding
        _conn = nw_connection_create(endpoint, params);

        _queue = dispatch_queue_create("NWConnection", DISPATCH_QUEUE_SERIAL);
        nw_connection_set_queue(_conn, _queue);

        FutureProvider<void> onOpen;
        nw_connection_set_state_changed_handler(_conn, ^(nw_connection_state_t state,
                                                         nw_error_t error) {
            std::exception_ptr x;
            switch (state) {
                case nw_connection_state_ready:
                    _isOpen = true;
                    onOpen.setValue();
                    break;
                case nw_connection_state_cancelled:
                    x = make_exception_ptr(runtime_error("cancelled"));
                    _onClose.setValue();
                    break;
                case nw_connection_state_failed:
                    x = make_exception_ptr(NWError(error));
                    break;
                default:
                    break;
            }
            if (x) {
                if (!onOpen.hasValue())
                    onOpen.setException(x);
                if (auto read = std::move(_onRead))
                    read->setException(x);
                if (auto write = std::move(_onWrite))
                    write->setException(x);
            }
        });
        nw_connection_start(_conn);
        return onOpen;
    }


    Future<void> NWConnection::close() {
        if (_conn)
            nw_connection_cancel(_conn);
        else if (_onClose.hasValue())
            _onClose.setValue();
        return _onClose;
    }


    void NWConnection::_close() {
        if (_conn) {
            nw_connection_force_cancel(_conn);
            nw_release(_conn);
            _conn = nullptr;
        }
    }


    void NWConnection::clearReadBuf() {
        if (_content) {
            dispatch_release(_content);
            _content = nullptr;
        }
        _contentBuf = {};
        _contentUsed = 0;
    }


    Future<ConstBuf> NWConnection::_readNoCopy(size_t maxLen) {
        assert(!_onRead);
        if (_content && _contentUsed < _contentBuf.len) {
            // I can return some unRead data from the buffer:
            auto len = std::min(maxLen, _contentBuf.len - _contentUsed);
            ConstBuf result{.base = (char*)_contentBuf.base + _contentUsed, .len = len};
            _contentUsed += len;
            return result;

        } else if (_eof) {
            return ConstBuf{};
            
        } else {
            // Read from the stream:
            dispatch_sync(_queue, ^{
                _onRead.emplace();
                clearReadBuf();
                nw_connection_receive(_conn, 1, uint32_t(min(maxLen, size_t(UINT32_MAX))),
                                      ^(dispatch_data_t content,
                                        nw_content_context_t context,
                                        bool is_complete,
                                        nw_error_t error) {
                    if (is_complete) {
                        std::cerr << "NWConnection read EOF\n";
                        _eof = true;
                    }
                    if (content) {
                        ConstBuf buf;
                        _content = dispatch_data_create_map(content, &buf.base, &buf.len);
                        _contentUsed = buf.len;
                        _contentBuf = buf;
                        cerr << "NWConnection read " << buf.len << " bytes\n";
                        _onRead->setValue(std::move(buf));
                    } else if (error) {
                        _onRead->setException(make_exception_ptr(NWError(error)));
                    } else if (is_complete) {
                        _onRead->setValue({});
                    }
                    _onRead.reset();
                });
            });
            return _onRead.value();
        }
    }


    void NWConnection::_unRead(size_t len) {
        assert(_contentBuf.base);
        assert(len <= _contentUsed);
        _contentUsed -= len;
    }


    Future<void> NWConnection::_writeOrShutdown(ConstBuf src, bool shutdown) {
        assert(!_onWrite);
        clearReadBuf();
        dispatch_sync(_queue, ^{
            __block __unused bool released = false;
            dispatch_data_t content = dispatch_data_create(src.base, src.len, _queue,
                                                           ^{ released = true; });
            _onWrite.emplace();
            nw_connection_send(_conn, content, NW_CONNECTION_DEFAULT_STREAM_CONTEXT, shutdown,
                               ^(nw_error_t error) {
                assert(released);
                if (error)
                    _onWrite->setException(make_exception_ptr(NWError(error)));
                else
                    _onWrite->setValue();
                _onWrite.reset();
            });
            dispatch_release(content);
        });
        return _onWrite.value();
    }

}

#endif // __APPLE__