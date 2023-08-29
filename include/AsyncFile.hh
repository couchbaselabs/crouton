//
// AsyncFile.hh
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
        [[nodiscard]] Future<size_t> read(size_t len, void* dst)   {return read(MutableBuf{dst, len});}
        [[nodiscard]] Future<size_t> read(MutableBuf buf)             {return preadv(&buf, 1, -1);}

        /// Reads from the file at the given offset.
        [[nodiscard]] Future<size_t> pread(MutableBuf buf, uint64_t o) {return preadv(&buf, 1, o);}

        /// Reads from the file, at the given offset, into multiple buffers.
        [[nodiscard]] Future<size_t> preadv(const MutableBuf bufs[], size_t nbufs, int64_t offset);

        /// Writes to the file. 
        [[nodiscard]] Future<void> write(size_t n, const void* s)   {return write(ConstBuf{s, n});}
        [[nodiscard]] Future<void> write(ConstBuf buf)              {return pwritev(&buf, 1, -1);}

        /// Writes to the file at the given offset.
        [[nodiscard]] Future<void> pwrite(ConstBuf buf, uint64_t o) {return pwritev(&buf, 1, o);}

        /// Reads from the file, at the given offset, from multiple buffers.
        [[nodiscard]] Future<void> pwritev(const ConstBuf bufs[], size_t nbufs, int64_t offset);

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
