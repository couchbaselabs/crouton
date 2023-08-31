//
// TLSSocket.hh
//
// 
//

#pragma once
#include "ISocket.hh"
#include "IStream.hh"

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

    protected:
        [[nodiscard]] Future<ConstBuf> _readNoCopy(size_t maxLen) override;
        void _unRead(size_t len) override;
        [[nodiscard]] Future<void> _write(ConstBuf) override;

    private:
        class Impl;
        std::unique_ptr<Impl>   _impl;
        std::unique_ptr<Buffer> _inputBuf;
        bool                    _busy = false;
    };

}
