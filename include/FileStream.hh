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
#include "IStream.hh"
#include "Generator.hh"
#include "UVBase.hh"
#include <memory>
#include <string>

namespace crouton {
    struct Buffer;
    
    /** Asynchronous file I/O. 

        @warning  In all the read and write calls, the data pointer passed to the call must
                  remain valid until the call completes (the Future is resolved.) */
    class FileStream : public IStream {
    public:
        /// Flags for open(). Equivalent to O_RDONLY, etc.
        static const int ReadOnly, WriteOnly, ReadWrite, Create, Append;

        /// Constructs a FileStream; next, call open().
        FileStream(std::string const& path, int flags = ReadOnly, int mode = 0644);

        FileStream(FileStream&& fs);
        FileStream& operator=(FileStream&& fs);
        ~FileStream();

        /// True if the file is open.
        bool isOpen() const override                {return _fd >= 0;}

        /// Resolves once the stream has opened.
        [[nodiscard]] Future<void> open() override;

        /// Closes the stream; resolves when it's closed.
        [[nodiscard]] Future<void> close() override;

        /// Closes the write side, but not the read side. (Like a socket's `shutdown`.)
        [[nodiscard]] Future<void> closeWrite() override    {return Future<void>();}

        [[nodiscard]] Future<size_t> read(MutableBuf) override;

        [[nodiscard]] Future<void> write(const ConstBuf buffers[], size_t nBuffers) override;

        Future<size_t> preadv(const MutableBuf bufs[], size_t nbufs, int64_t offset);
        Future<void> pwritev(const ConstBuf bufs[], size_t nbufs, int64_t offset);

    protected:
        [[nodiscard]] Future<ConstBuf> _readNoCopy(size_t maxLen) override;

        /// Abstract write method subclasses must implement.
        /// @note  If a subclass natively supports multi-buffer write ("writev"),
        ///     it can override the virtual multi-buffer write method too, and implement
        ///     this one to simply call it with one buffer.
        [[nodiscard]] Future<void> _write(ConstBuf) override;

    private:
        explicit FileStream(int fd);
        FileStream(FileStream const&) = delete;
        FileStream& operator=(FileStream const& fs) = delete;
        Future<size_t> _preadv(const MutableBuf bufs[], size_t nbufs, int64_t offset);
        void _close();

        std::string _path;
        int _flags;
        int _mode;
        int  _fd   = -1;
        std::unique_ptr<Buffer> _readBuf;
        bool _busy = false;
    };
    
}
