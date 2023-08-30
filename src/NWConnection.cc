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

namespace crouton {
    using namespace std;

    template <typename T>
    struct Autoreleaser {
        Autoreleaser(T* t)  :_ref(t) { }
        Autoreleaser(Autoreleaser&& other) :_ref(other._ref) {other._ref = nullptr;}
        ~Autoreleaser()     {nw_release(_ref);}

        operator T*()       {return _ref;}
    private:
        T* _ref;
    };

    template <typename T>
    Autoreleaser<T> autorelease(T* t)   {return Autoreleaser<T>(t);}



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

    void NWConnection::setNoDelay(bool) { } // TODO

    void NWConnection::keepAlive(unsigned intervalSecs) { } // TODO

    Future<void> NWConnection::open() {
        string portStr = std::to_string(_binding->port);
        nw_endpoint_t endpoint = nw_endpoint_create_host(_binding->address.c_str(), portStr.c_str());

        nw_parameters_configure_protocol_block_t tlsConfig;
        if (_binding->withTLS)
            tlsConfig = NW_PARAMETERS_DEFAULT_CONFIGURATION;
        else
            tlsConfig = NW_PARAMETERS_DISABLE_PROTOCOL;
        auto params = autorelease(nw_parameters_create_secure_tcp(tlsConfig,
                                                            NW_PARAMETERS_DEFAULT_CONFIGURATION));
        _conn = nw_connection_create(endpoint, params);

        _queue = dispatch_queue_create("NWConnection", DISPATCH_QUEUE_SERIAL);
        nw_connection_set_queue(_conn, _queue);

        FutureProvider<void> onOpen;
        nw_connection_set_state_changed_handler(_conn, ^(nw_connection_state_t state,
                                                         nw_error_t error) {
            switch (state) {
                case nw_connection_state_ready:
                    _isOpen = true;
                    onOpen.setValue();
                    break;
                case nw_connection_state_cancelled:
                    if (!onOpen.hasValue())
                        onOpen.setException(make_exception_ptr(runtime_error("cancelled")));
                    _onClose.setValue();
                    break;
                case nw_connection_state_failed:
                    onOpen.setException(make_exception_ptr(NWError(error)));
                    break;
                default:
                    break;
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
        FutureProvider<ConstBuf> result;
        if (_content && _contentUsed < _contentBuf.len) {
            // I can return some unRead data from the buffer:
            auto len = std::min(maxLen, _contentBuf.len - _contentUsed);
            result.setValue(ConstBuf{.base = (char*)_contentBuf.base + _contentUsed, .len = len});
            _contentUsed += len;

        } else if (_eof) {
            result.setValue(ConstBuf{});
            
        } else {
            // Read from the stream:
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
                    result.setValue(std::move(buf));
                } else if (error) {
                    result.setException(make_exception_ptr(NWError(error)));
                } else if (is_complete) {
                    result.setValue({});
                }
            });
        }
        return result;
    }


    void NWConnection::_unRead(size_t len) {
        assert(_contentBuf.base);
        assert(len <= _contentUsed);
        _contentUsed -= len;
    }


    Future<void> NWConnection::_writeOrShutdown(ConstBuf src, bool shutdown) {
        clearReadBuf();
        FutureProvider<void> result;
        __block bool released = false;
        dispatch_data_t content = dispatch_data_create(src.base, src.len, _queue,
                                                       ^{ released = true; });
        nw_connection_send(_conn, content, NW_CONNECTION_DEFAULT_STREAM_CONTEXT, shutdown,
                           ^(nw_error_t error) {
            assert(released);
            if (error)
                result.setException(make_exception_ptr(NWError(error)));
            else
                result.setValue();
        });
        dispatch_release(content);
        return result;
    }

}

#endif // __APPLE__
