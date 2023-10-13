//
// FileStream.hh
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
#include "io/IStream.hh"
#include "Generator.hh"


namespace crouton::io {
    struct Buffer;
    
    /** Asynchronous file I/O. 

        @warning  In all the read and write calls, the data pointer passed to the call must
                  remain valid until the call completes (the Future is resolved.) */
    class FileStream : public IStream {
    public:
        /// Flags for open(). Equivalent to O_RDONLY, etc.
        static const int ReadOnly, WriteOnly, ReadWrite, Create, Append;

        /// Constructs a FileStream; next, call open().
        FileStream(string const& path, int flags = ReadOnly, int mode = 0644);

        FileStream(FileStream&& fs) noexcept;
        FileStream& operator=(FileStream&& fs) noexcept;
        ~FileStream();

        /// True if the file is open.
        bool isOpen() const override pure           {return _fd >= 0;}

        /// Resolves once the stream has opened.
        ASYNC<void> open() override;

        /// Closes the stream; resolves when it's closed.
        ASYNC<void> close() override;

        /// Closes the write side, but not the read side. (Like a socket's `shutdown`.)
        ASYNC<void> closeWrite() override    {return Future<void>();}

        ASYNC<ConstBytes> readNoCopy(size_t maxLen = 65536) override;
        ASYNC<ConstBytes> peekNoCopy() override;

        ASYNC<void> write(ConstBytes) override;
        ASYNC<void> write(const ConstBytes buffers[], size_t nBuffers) override;
        using IStream::write;

        /// Ignores the current stream position and reads from an absolute offset in the file
        /// into one or more buffers.
        ASYNC<size_t> preadv(const MutableBytes bufs[], size_t nbufs, int64_t offset);

        /// Ignores the current stream position and writes to an absolute offset in the file
        /// from one or more buffers.
        ASYNC<void> pwritev(const ConstBytes bufs[], size_t nbufs, int64_t offset);

    private:
        explicit FileStream(int fd);
        FileStream(FileStream const&) = delete;
        FileStream& operator=(FileStream const& fs) = delete;
        ASYNC<size_t> _preadv(const MutableBytes bufs[], size_t nbufs, int64_t offset);
        ASYNC<ConstBytes> _fillBuffer();
        void _close();

        string _path;
        int _flags;
        int _mode;
        int  _fd   = -1;
        std::unique_ptr<Buffer> _readBuf;
        bool _busy = false;
    };
    
}
