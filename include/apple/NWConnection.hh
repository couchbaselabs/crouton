//
// NWConnection.hh
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
#ifdef __APPLE__

#include "Error.hh"
#include "ISocket.hh"
#include "IStream.hh"

struct dispatch_data_s;
struct dispatch_queue_s;
struct nw_connection;
struct nw_error;

namespace crouton::apple {

    // The three error domains that Network.framework's NWError can represent:
    enum class POSIXError : errorcode_t { };
    enum class DNSError : errorcode_t { };
    enum class TLSError : errorcode_t { };


    /** A TCP client connection using Apple's Network.framework.
        Supports TLS. */
    class NWConnection final : public IStream, public ISocket {
    public:
        NWConnection() = default;
        explicit NWConnection(bool useTLS)                  :_useTLS(useTLS) { }
        ~NWConnection();

        void useTLS(bool tls)                               {_useTLS = tls;}

        /// Opens the socket to the bound address. Resolves once opened.
        virtualASYNC<void> open() override;

        bool isOpen() const override                        {return _isOpen;}

        ASYNC<void> close() override;

        ASYNC<void> closeWrite() override    {return _writeOrShutdown({}, true);}

        IStream& stream() override               {return *this;}

        virtualASYNC<ConstBytes> readNoCopy(size_t maxLen = 65536) override;
        virtualASYNC<ConstBytes> peekNoCopy() override;
        ASYNC<void> write(ConstBytes b) override {return _writeOrShutdown(b, false);}
        using IStream::write;
        
    private:
        virtualASYNC<ConstBytes> _readNoCopy(size_t maxLen, bool peek);
        ASYNC<void> _writeOrShutdown(ConstBytes, bool shutdown);
        void _close();
        void clearReadBuf();

        nw_connection*      _conn = nullptr;
        dispatch_queue_s*   _queue = nullptr;
        FutureProvider<void> _onClose;
        dispatch_data_s*    _content = nullptr;
        ConstBytes            _contentBuf;
        size_t              _contentUsed;
        bool                _useTLS = false;
        bool                _isOpen = false;
        bool                _canceled = false;
        bool                _eof = false;
    };
}

namespace crouton {
    template <> struct ErrorDomainInfo<apple::POSIXError> {
        static constexpr string_view name = "POSIX";
        static string description(errorcode_t);
    };
    template <> struct ErrorDomainInfo<apple::DNSError> {
        static constexpr string_view name = "DNS";
        static string description(errorcode_t);
    };
    template <> struct ErrorDomainInfo<apple::TLSError> {
        static constexpr string_view name = "Apple TLS";
        static string description(errorcode_t);
    };
}

#endif // __APPLE__
