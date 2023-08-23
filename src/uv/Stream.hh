//
// Stream.hh
//
// 
//

#pragma once
#include "Future.hh"
#include "UVBase.hh"
#include <string>

namespace snej::coro::uv {
    struct Buffer;
    struct stream_wrapper;

    /** An asynchronous bidirectional stream. Abstract base class. */
    class Stream {
    public:
        virtual ~Stream();

        /// Returns true while the stream is open.
        bool isOpen() const {return _stream != nullptr;}

        /// Closes the write stream, leaving the read stream open until the peer closes it.
        [[nodiscard]] Future<void> shutdown();

        /// Closes the stream entirely. (Called by the destructor.)
        virtual void close();

        //---- READING

        /// True if the stream has data available to read.
        bool isReadable() const;

        size_t bytesAvailable() const;

        /// Reads up to `maxLen` bytes, returning a `string_view` pointing into the internal buffer.
        /// If the read stream is at EOF, returns an empty string.
        /// Otherwise returns at least one byte, but may return fewer than requested since it reads
        /// from the stream at most once.
        /// @warning The result is invalidated by any subsequent read, shutdown or close.
        [[nodiscard]] Future<WriteBuf> readNoCopy(size_t maxLen);

        /// Reads `len` bytes, copying into memory starting at `dst` (which must remain valid.)
        /// Will always read the full number of bytes unless it hits EOF.
        [[nodiscard]] Future<int64_t> read(size_t len, void* dst);
        [[nodiscard]] Future<int64_t> read(ReadBuf buf)            {return read(buf.len, buf.base);}

        /// Reads exactly `len` bytes; on eof, throws UVError(UV_EOF).
        [[nodiscard]] Future<void> readExactly(size_t len, void* dst);
        [[nodiscard]] Future<void> readExactly(ReadBuf buf) {return readExactly(buf.len, buf.base);}

        /// Reads `len` bytes, returning them as a string.
        /// Will always read the full number of bytes unless it hits EOF.
        [[nodiscard]] Future<std::string> read(size_t maxLen);

        /// Reads up through the first occurrence of the string `end`,
        /// or when `maxLen` bytes have been read, whichever comes first.
        /// Throws `UV_EOF` if it hits EOF first.
        [[nodiscard]] Future<std::string> readUntil(std::string end, size_t maxLen = SIZE_MAX);

        /// Reads until EOF.
        [[nodiscard]] Future<std::string> readAll() {return read(SIZE_MAX);}

        //---- WRITING

        /// True if the stream has buffer space available to write to.
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

    protected:
        Stream() = default;

        void opened(std::unique_ptr<stream_wrapper> s);

    private:
        struct BufWithCapacity : public ReadBuf {
            size_t capacity = 0;
        };

        Stream(Stream const&) = delete;
        Stream& operator=(Stream const&) = delete;

        [[nodiscard]] Future<int64_t> _read(size_t len, void* dst);
        [[nodiscard]] Future<WriteBuf> _readNoCopy(size_t maxLen);
        [[nodiscard]] Future<std::unique_ptr<Buffer>> readBuf();
        [[nodiscard]] Future<void> _read();

        std::unique_ptr<stream_wrapper> _stream;  // Handle for stream operations (actually the same)
        std::unique_ptr<Buffer> _inputBuf;       // The last data read from the stream
        bool            _readBusy = false;  // Detects re-entrant calls
        bool            _writeBusy = false; // Detects re-entrant calls
    };

}
