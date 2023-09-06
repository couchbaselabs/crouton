//
// TLSSocket.hh
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
#include "ISocket.hh"
#include "IStream.hh"
#include <memory>

namespace crouton {
    struct Buffer;
}

namespace crouton::mbed {

    /*** A TCP socket with TLS, using mbedTLS. */
    class TLSSocket : public IStream, public ISocket {
    public:
        TLSSocket();
        ~TLSSocket();

        bool isOpen() const override;
        [[nodiscard]] Future<void> open() override;
        [[nodiscard]] Future<void> close() override;
        [[nodiscard]] Future<void> closeWrite() override;

        IStream& stream() override              {return *this;}
        IStream const& stream() const override  {return *this;}

        [[nodiscard]] Future<ConstBytes> readNoCopy(size_t maxLen = 65536) override;
        [[nodiscard]] Future<ConstBytes> peekNoCopy() override;
        [[nodiscard]] Future<void> write(ConstBytes) override;
        using IStream::write;

    private:
        [[nodiscard]] Future<ConstBytes> _readNoCopy(size_t maxLen, bool peek);

        class Impl;
        std::unique_ptr<Impl>   _impl;
        std::unique_ptr<Buffer> _inputBuf;
        bool                    _busy = false;
    };

}
