//
// IStream.cc
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

#include "IStream.hh"
#include "UVInternal.hh"
#include <cstring> // for memcpy

namespace crouton {
    using namespace std;


    Future<ConstBuf> IStream::i_readNoCopy(size_t maxLen) {
        if (_readUsed >= _readBuf.len) {
            _readBuf = AWAIT _readNoCopy(maxLen);
            _readUsed = 0;
        }
        ConstBuf result((char*)_readBuf.base + _readUsed,
                        std::min(maxLen, _readBuf.len - _readUsed));
        _readUsed += result.len;
        RETURN result;
    }


    Future<size_t> IStream::i_read(size_t maxLen, void* dst) {
        size_t bytesRead = 0;
        while (bytesRead < maxLen) {
            ConstBuf bytes = AWAIT i_readNoCopy(maxLen - bytesRead);
            if (bytes.len == 0)
                break;
            ::memcpy((char*)dst + bytesRead, bytes.base, bytes.len);
            bytesRead += bytes.len;
        }
        RETURN bytesRead;
    }


    Future<ConstBuf> IStream::readNoCopy(size_t maxLen) {
        NotReentrant nr(_readBusy);
        return i_readNoCopy(maxLen);
    }

    void IStream::unRead(size_t len) {
        NotReentrant nr(_readBusy);
        assert(len <= _readUsed);
        _readUsed -= len;
    }


    Future<size_t> IStream::read(size_t maxLen, void* dst) {
        NotReentrant nr(_readBusy);
        return i_read(maxLen, dst);
    }


    Future<void> IStream::readExactly(size_t len, void* dst) {
        int64_t bytesRead = AWAIT read(len, dst);
        if (bytesRead < len)
            check(int(UV_EOF), "reading from the network");
        RETURN;
    }


    Future<string> IStream::readString(size_t maxLen) {
        NotReentrant nr(_readBusy);
        static constexpr size_t kGrowSize = 32768;
        string data;
        size_t len = 0;
        while (len < maxLen) {
            size_t n = std::min(kGrowSize, maxLen - len);
            data.resize(len + n);
            size_t bytesRead = AWAIT i_read(n, &data[len]);

            if (bytesRead < n) {
                data.resize(len + bytesRead);
                break;
            }
            len += bytesRead;
        }
        RETURN data;
    }


    Future<string> IStream::readUntil(std::string end, size_t maxLen) {
        assert(!end.empty());
        assert(maxLen >= end.size());
        NotReentrant nr(_readBusy);
        string data;
        while (data.size() < maxLen) {
            auto dataLen = data.size();
            ConstBuf peek = AWAIT i_readNoCopy(maxLen - dataLen);

            // Check for a match that's split between the old and new data:
            if (!data.empty()) {
                data.append((char*)peek.base, min(end.size() - 1, peek.len));
                size_t startingAt = dataLen - std::min(end.size(), dataLen);
                if (auto found = data.find(end, startingAt); found != string::npos) {
                    // Found it:
                    found += end.size();
                    found = std::min(found, maxLen);
                    data.resize(found);
                    unRead(peek.len - (found - dataLen));
                    break;
                } else {
                    data.resize(dataLen);
                }
            }

            // Check for a match in the new data:
            if (auto found = string_view((char*)peek.base, peek.len).find(end); found != string::npos) {
                found += end.size();
                found = std::min(found, maxLen - data.size());
                data.append((char*)peek.base, found);
                unRead(peek.len - found);
                break;
            }

            // Otherwise append all the input data and read more:
            size_t addLen = std::min(peek.len, maxLen - data.size());
            data.append((char*)peek.base, addLen);
            unRead(peek.len - addLen);
        }
        RETURN data;
    }


    Future<void> IStream::write(ConstBuf buf) {
        NotReentrant nr(_writeBusy);
        return _write(buf);
    }


    Future<void> IStream::write(std::string str) {
        // Use co_await to ensure `str` stays in scope until the write completes.
        AWAIT write(str.size(), str.data());
        RETURN;
    }


    Future<void> IStream::write(const ConstBuf buffers[], size_t nBuffers) {
        NotReentrant nr(_writeBusy);
        for (size_t i = 0; i < nBuffers; ++i) {
            AWAIT _write(buffers[i]);
        }
    }


    Future<void> IStream::write(std::initializer_list<ConstBuf> buffers) {
        return write(buffers.begin(), buffers.size());
    }


}
