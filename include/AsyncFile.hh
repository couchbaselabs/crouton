//
// AsyncFile.hh
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#pragma once
#include "Future.hh"
#include "Generator.hh"
#include "UVBase.hh"
#include <string>

namespace crouton {
    
    /** Asynchronous file I/O. 

        @warning  In all the read and write calls, the data pointer passed to the call must
                  remain valid until the call completes (the Future is resolved.) */
    class FileStream {
    public:
        /// Flags for open(). Equivalent to O_RDONLY, etc.
        static const int ReadOnly, WriteOnly, ReadWrite, Create, Append;

        /// Asynchronously opens a file.
        [[nodiscard]] static Future<FileStream> open(std::string const& path,
                                                     int flags = ReadOnly,
                                                     int mode = 0644);

        FileStream(FileStream&& fs)                 {std::swap(_fd, fs._fd);}
        FileStream& operator=(FileStream&& fs)      {close(); std::swap(_fd, fs._fd); return *this;}
        ~FileStream()   {close();}

        /// True if the file is open.
        bool isOpen() const                                        {return _fd >= 0;}

        /// Reads from the file. The destination buffer must remain valid until this completes.
        [[nodiscard]] Future<size_t> read(size_t len, void* dst)   {return read(ReadBuf{dst, len});}
        [[nodiscard]] Future<size_t> read(ReadBuf buf)             {return preadv(&buf, 1, -1);}

        /// Reads from the file at the given offset.
        [[nodiscard]] Future<size_t> pread(ReadBuf buf, uint64_t o) {return preadv(&buf, 1, o);}

        /// Reads from the file, at the given offset, into multiple buffers.
        [[nodiscard]] Future<size_t> preadv(const ReadBuf bufs[], size_t nbufs, int64_t offset);

        /// Writes to the file. 
        [[nodiscard]] Future<void> write(size_t n, const void* s)   {return write(WriteBuf{s, n});}
        [[nodiscard]] Future<void> write(WriteBuf buf)              {return pwritev(&buf, 1, -1);}

        /// Writes to the file at the given offset.
        [[nodiscard]] Future<void> pwrite(WriteBuf buf, uint64_t o) {return pwritev(&buf, 1, o);}

        /// Reads from the file, at the given offset, from multiple buffers.
        [[nodiscard]] Future<void> pwritev(const WriteBuf bufs[], size_t nbufs, int64_t offset);

        /// Closes the file, if it's open. Idempotent.
        /// @note This method is synchronous.
        void close();
        
    private:
        FileStream(int fd) :_fd(fd) { }
        FileStream(FileStream const&) = delete;
        FileStream& operator=(FileStream const& fs) = delete;

        int  _fd   = -1;
        bool _busy = false;
    };
    
}
