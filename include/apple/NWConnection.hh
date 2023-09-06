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

#include "ISocket.hh"
#include "IStream.hh"
#include <optional>
#include <stdexcept>

struct dispatch_data_s;
struct dispatch_queue_s;
struct nw_connection;
struct nw_error;

namespace crouton::apple {

    class NWError : public std::runtime_error {
    public:
        explicit NWError(nw_error*);
    };


    /** A TCP client connection using Apple's Network.framework.
        Supports TLS. */
    class NWConnection final : public IStream, public ISocket {
    public:
        NWConnection() = default;
        explicit NWConnection(bool useTLS)                  :_useTLS(useTLS) { }
        ~NWConnection();

        void useTLS(bool tls)                               {_useTLS = tls;}

        /// Opens the socket to the bound address. Resolves once opened.
        [[nodiscard]] virtual Future<void> open() override;

        bool isOpen() const override                        {return _isOpen;}

        [[nodiscard]] Future<void> close() override;

        [[nodiscard]] Future<void> closeWrite() override    {return _writeOrShutdown({}, true);}

        IStream& stream() override               {return *this;}
        IStream const& stream() const override   {return *this;}

    private:
        [[nodiscard]] virtual Future<ConstBuf> _readNoCopy(size_t maxLen) override;
        [[nodiscard]] Future<void> _write(ConstBuf b) override {return _writeOrShutdown(b, false);}
        [[nodiscard]] Future<void> _writeOrShutdown(ConstBuf, bool shutdown);
        void _close();
        void clearReadBuf();

        nw_connection*      _conn = nullptr;
        dispatch_queue_s*   _queue = nullptr;
        FutureProvider<void> _onClose;
        dispatch_data_s*    _content = nullptr;
        ConstBuf            _contentBuf;
        size_t              _contentUsed;
        bool                _useTLS = false;
        bool                _isOpen = false;
        bool                _eof = false;
    };

}

#endif // __APPLE__
