//
// AsyncFile.hh
//
// 
//

#pragma once
#include "Future.hh"
#include "Generator.hh"
#include "UVBase.hh"
#include <string>

namespace snej::coro::uv {
    
    class FileStream {
    public:
        /// Flags for open(). Equivalent to O_RDONLY, etc.
        static const int ReadOnly, WriteOnly, ReadWrite, Create, Append;
        
        FileStream() = default;
        ~FileStream()   {close();}
        
        /// Asynchronously opens a file.
        Future<void> open(std::string const& path, int flags = ReadOnly, int mode = 0644);
        
        bool isOpen() const {return _fd >= 0;}
        
        /// Reads from the file. The destination buffer must remain valid until this completes.
        Future<size_t> read(size_t len, void* dst)          {return read(ReadBuf{dst, len});}
        Future<size_t> read(ReadBuf buf)                    {return preadv(&buf, 1, -1);}

        /// Reads from the file at the given offset.
        Future<size_t> pread(ReadBuf buf, uint64_t offset)  {return preadv(&buf, 1, offset);}

        /// Reads from the file, at the given offset, into multiple buffers.
        Future<size_t> preadv(const ReadBuf bufs[], size_t nbufs, int64_t offset);

        /// Writes to the file. The source buffer must remain valid until this completes.
        Future<void> write(size_t len, const void* src)     {return write(WriteBuf{src, len});}
        Future<void> write(WriteBuf buf)                    {return pwritev(&buf, 1, -1);}

        /// Writes to the file at the given offset.
        Future<void> pwrite(WriteBuf buf, uint64_t offset)  {return pwritev(&buf, 1, offset);}

        /// Reads from the file, at the given offset, from multiple buffers.
        Future<void> pwritev(const WriteBuf bufs[], size_t nbufs, int64_t offset);

        /// Closes the file, if it's open. This method is synchronous.
        void close();
        
    private:
        int  _fd = -1;
        bool _busy = false;
    };
    
}
