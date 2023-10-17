//
// ESPTCPSocket.hh
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
#include "CoCondition.hh"
#include "io/ISocket.hh"
#include "io/IStream.hh"
#include <mutex>
#include <vector>

struct pbuf;
struct tcp_pcb;
namespace crouton {
    struct Buffer;
}

namespace crouton::io::esp {

    class TCPSocket : public io::ISocket, public io::IStream {
    public:
        TCPSocket();
        ~TCPSocket();

        /// Opens the socket to the bound address. Resolves once opened.
        ASYNC<void> open() override;

        bool isOpen() const override                {return _isOpen;}
        IStream& stream() override                  {return *this;}
        ASYNC<void> close() override;

        ASYNC<void> closeWrite() override;

        /// True if the stream has data available to read.
        bool isReadable() const noexcept pure;

        /// The number of bytes known to be available without blocking.
        size_t bytesAvailable() const noexcept pure;

        ASYNC<ConstBytes> readNoCopy(size_t maxLen = 65536) override;

        ASYNC<ConstBytes> peekNoCopy() override;

        ASYNC<void> write(ConstBytes) override;

    private:
        using BufferRef = std::unique_ptr<Buffer>;

        int _writeCallback(uint16_t len);
        int _readCallback(::pbuf *p, int err);
        ASYNC<int64_t> _read(size_t len, void* dst);
        ASYNC<BufferRef> readBuf();
        ASYNC<ConstBytes> fillInputBuf();

        tcp_pcb*    _tcp;
        bool        _isOpen = false;

        ConstBytes              _inputBuf;
        pbuf*                   _readBufs = nullptr;
        Error                   _readErr;

        Blocker<void>           _readBlocker;
        Blocker<void>           _writeBlocker;
    };

}
