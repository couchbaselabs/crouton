//
// AsyncSocket.hh
//
// 
//

#pragma once
#include "Future.hh"
#include "UVBase.hh"
#include <string>

struct uv_buf_t;
struct uv_stream_s;
struct uv_tcp_s;

namespace snej::coro::uv {

    /** A TCP socket. */
    class TCPSocket {
    public:
        TCPSocket();
        ~TCPSocket();

        /// Connects to an address/port. The address may be a hostname or dotted-quad IPv4 address.
        [[nodiscard]] Future<void> connect(std::string const& address, uint16_t port);

        /// Returns true while the socket is connected.
        bool isOpen() const {return _socket != nullptr;}

        /// Sets the TCP nodelay option.
        void setNoDelay(bool);

        /// Enables TCP keep-alive with the given ping interval.
        void keepAlive(unsigned intervalSecs);

        /// Closes the write stream, leaving the read stream open until the peer closes it.
        [[nodiscard]] Future<void> shutdown();

        /// Closes the socket entirely. (Called by the destructor.)
        void close();

        //---- READING

        /// True if the socket has data available to read.
        bool isReadable() const;

        /// Reads up to `maxLen` bytes, returning a `string_view` pointing into the internal buffer.
        /// If the read stream is at EOF, returns an empty string.
        /// Otherwise returns at least one byte, but may return fewer than requested since it reads
        /// from the socket at most once.
        /// @warning The result is invalidated by any subsequent read, shutdown or close.
        [[nodiscard]] Future<WriteBuf> readNoCopy(size_t maxLen);

        /// Reads `len` bytes, copying into memory starting at `dst` (which must remain valid.)
        /// Will always read the full number of bytes unless it hits EOF.
        [[nodiscard]] Future<int64_t> read(size_t len, void* dst);
        [[nodiscard]] Future<int64_t> read(ReadBuf buf)               {return read(buf.len, buf.base);}

        /// Reads exactly `len` bytes; on eof, throws UVError(UV_EOF).
        [[nodiscard]] Future<void> readExactly(size_t len, void* dst);
        [[nodiscard]] Future<void> readExactly(ReadBuf buf)        {return readExactly(buf.len, buf.base);}

        /// Reads `len` bytes, returning them as a string.
        /// Will always read the full number of bytes unless it hits EOF.
        [[nodiscard]] Future<std::string> read(size_t maxLen);

        /// Reads up through the first occurrence of the string `end`.
        /// Throws `UV_EOF` if it hits EOF first.
        Future<std::string> readUntil(std::string end);

        /// Reads until EOF.
        [[nodiscard]] Future<std::string> readAll() {return read(SIZE_MAX);}

        //---- WRITING

        /// True if the socket has buffer space available to write to.
        bool isWritable() const;

        /// Writes data, fully.
        /// @warning The data pointed to by the buffer(s) must remain valid until completion.
        [[nodiscard]] Future<void> write(const WriteBuf buffers[], size_t nBuffers);
        [[nodiscard]] Future<void> write(std::initializer_list<WriteBuf> buffers);
        [[nodiscard]] Future<void> write(size_t len, const void *src);

        /// Writes data, fully. The string is copied, so the caller doesn't need to keep it.
        [[nodiscard]] Future<void> write(std::string);

        /// Writes as much as possible immediately, without blocking.
        /// @return  Number of bytes written, which may be 0 if the write buffer is full.
        size_t tryWrite(WriteBuf);

    private:
        friend class TCPServer;
        
        struct BufWithCapacity : public ReadBuf {
            size_t capacity = 0;
        };

        void acceptFrom(uv_tcp_s* server);

        TCPSocket(TCPSocket const&) = delete;
        TCPSocket& operator=(TCPSocket const&) = delete;

        void freeInputBuf();
        [[nodiscard]] Future<int64_t> _read(size_t len, void* dst);
        [[nodiscard]] Future<WriteBuf> _readNoCopy(size_t maxLen);
        [[nodiscard]] Future<BufWithCapacity> readBuf();
        [[nodiscard]] Future<void> _read();

        uv_tcp_s*       _tcpHandle;         // Handle for TCP operations
        uv_stream_s*    _socket = nullptr;  // Handle for stream operations (actually the same)
        BufWithCapacity _inputBuf {};       // The last data read from the socket
        size_t          _inputOff = 0;      // How many bytes of inputBuf have been consumed
        BufWithCapacity _spareInputBuf {};  // Second input buffer, for use by next read call
        bool            _readBusy = false;  // Detects re-entrant calls
        bool            _writeBusy = false; // Detects re-entrant calls
    };

}
