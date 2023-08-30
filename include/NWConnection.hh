//
// NWConnection.hh
//
// 
//

#pragma once
#ifdef __APPLE__

#include "ISocket.hh"
#include "IStream.hh"
#include <stdexcept>

struct dispatch_data_s;
struct dispatch_queue_s;
struct nw_connection;
struct nw_error;

namespace crouton {

    class NWError : public std::runtime_error {
    public:
        explicit NWError(nw_error*);
    };


    /** A TCP client connection using Apple's Network.framework. */
    class NWConnection final : public IStream, public ISocket {
    public:
        NWConnection() = default;
        ~NWConnection();

        /// Opens the socket to the bound address. Resolves once opened.
        [[nodiscard]] virtual Future<void> open() override;

        /// Sets the TCP nodelay option.
        void setNoDelay(bool) override;

        /// Enables TCP keep-alive with the given ping interval.
        void keepAlive(unsigned intervalSecs) override;

        bool isOpen() const override {return _isOpen;}

        [[nodiscard]] Future<void> close() override;

        [[nodiscard]] Future<void> closeWrite() override    {return _writeOrShutdown({}, true);}

    private:
        [[nodiscard]] virtual Future<ConstBuf> _readNoCopy(size_t maxLen) override;
        void _unRead(size_t len) override;
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
        bool                _isOpen = false;
        bool                _eof = false;
    };

}

#endif // __APPLE__
