//
// IStream.hh
//
// 
//

#pragma once
#include "Future.hh"
#include <memory>
#include <string>
#include <string_view>

namespace crouton {

    /** Low-level struct pointing to mutable data.
        Usually serves as the destination of a read.
        Binary compatible with uv_buf_t. */
    struct MutableBuf {
        void*   base = nullptr;
        size_t  len = 0;

        MutableBuf() = default;
        MutableBuf(void* b, size_t ln) :base(b), len(ln) { }
        MutableBuf(std::string& str) :base(str.data()), len(str.size()) { }

        explicit operator std::string_view() const {
            return std::string_view((const char*)base, len);
        }
    };

    
    /** Low-level struct pointing to immutable data.
        Usually serves as the source of a write.
        Binary compatible with uv_buf_t. */
    struct ConstBuf {
        const void* base = nullptr;
        size_t      len = 0;

        ConstBuf() = default;
        ConstBuf(const void* b, size_t ln) :base(b), len(ln) { }
        ConstBuf(std::string_view str) :base(str.data()), len(str.size()) { }

        explicit operator std::string_view() const {
            return std::string_view((const char*)base, len);
        }
    };
    //TODO //FIXME: uv_buf_t's fields are in the opposite order on Windows. Deal with that.



    /** Abstract interface of an asynchronous bidirectional stream.
        It has concrete read/write methods, which are merely conveniences that call the
        abstract ones.
        Re-entrant reads or writes are not allowed: no read call may be issued until the
        previous one has completed, and equivalently for writes. */
    class IStream {
    public:
        virtual ~IStream() = default;

        /// True if the stream is open.
        virtual bool isOpen() const =0;

        /// Resolves once the stream has opened.
        [[nodiscard]] virtual Future<void> open() = 0;

        /// Closes the stream; resolves when it's closed.
        [[nodiscard]] virtual Future<void> close() = 0;

        /// Closes the write side, but not the read side. (Like a socket's `shutdown`.)
        [[nodiscard]] virtual Future<void> closeWrite() = 0;

        //---- Reading:

        /// Lowest level read method.  Reads at least 1 byte, except at EOF.
        /// Returned buffer belongs to the stream, and is valid until the next read or close call.
        [[nodiscard]] virtual Future<ConstBuf> readNoCopy(size_t maxLen = 65536);

        /// Makes the last `len` read bytes unread again.
        /// The last read call must have been `readNoCopy`.
        /// `len` may not be greater than the number of bytes returned by `readNoCopy`.
        virtual void unRead(size_t len);

        /// Reads `len` bytes, copying into memory starting at `dst` (which must remain valid.)
        /// Will always read the full number of bytes unless it hits EOF.
        [[nodiscard]] virtual Future<size_t> read(size_t len, void* dst);
        [[nodiscard]] Future<size_t> read(MutableBuf buf)            {return read(buf.len, buf.base);}

        /// Reads `len` bytes, returning them as a string.
        /// Will always read the full number of bytes unless it hits EOF.
        [[nodiscard]] Future<std::string> readString(size_t maxLen);

        /// Reads exactly `len` bytes; on eof, throws UVError(UV_EOF).
        [[nodiscard]] Future<void> readExactly(size_t len, void* dst);
        [[nodiscard]] Future<void> readExactly(MutableBuf buf) {return readExactly(buf.len, buf.base);}

        /// Reads up through the first occurrence of the string `end`,
        /// or when `maxLen` bytes have been read, whichever comes first.
        /// Throws `UV_EOF` if it hits EOF first.
        [[nodiscard]] Future<std::string> readUntil(std::string end, size_t maxLen = SIZE_MAX);

        /// Reads until EOF.
        [[nodiscard]] Future<std::string> readAll() {return readString(SIZE_MAX);}

        //---- Writing:

        /// Writes the entire buffer.
        /// The buffer must remain valid until this call completes.
        [[nodiscard]] Future<void> write(ConstBuf);
        [[nodiscard]] Future<void> write(size_t len, const void *src) {return write(ConstBuf{src,len});}

        /// Writes data, fully. The string is copied, so the caller doesn't need to keep it.
        [[nodiscard]] Future<void> write(std::string);

        /// Writes data, fully, from multiple input buffers.
        /// @warning The data pointed to by the buffers must remain valid until completion.
        [[nodiscard]] virtual Future<void> write(const ConstBuf buffers[], size_t nBuffers);
        [[nodiscard]] Future<void> write(std::initializer_list<ConstBuf> buffers);

    protected:
        /// The abstract read method that subclasses must implement.
        [[nodiscard]] virtual Future<ConstBuf> _readNoCopy(size_t maxLen) =0;

        /// Abstract write method subclasses must implement.
        /// @note  If a subclass natively supports multi-buffer write ("writev"),
        ///     it can override the virtual multi-buffer write method too, and implement
        ///     this one to simply call it with one buffer.
        [[nodiscard]] virtual Future<void> _write(ConstBuf) =0;

    private:
        [[nodiscard]] Future<ConstBuf> i_readNoCopy(size_t maxLen);
        Future<size_t> i_read(size_t len, void* dst);

        ConstBuf _readBuf;
        size_t _readUsed = 0;
        bool _readBusy = false;
        bool _writeBusy = false;
    };

}
