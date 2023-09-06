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


    MutableBytes::MutableBytes(uv_buf_t buf) :span((byte*)buf.base, buf.len) { }
    ConstBytes::ConstBytes(uv_buf_t buf)     :span((const byte*)buf.base, buf.len) { }

    MutableBytes::operator uv_buf_t() const { return {.base = (char*)data(), .len = size()}; }
    ConstBytes::operator uv_buf_t() const   { return {.base = (char*)data(), .len = size()}; }



    Future<ConstBytes> IStream::i_readNoCopy(size_t maxLen) {
        if (_readUsed >= _readBuf.size()) {
            _readBuf = AWAIT _readNoCopy(maxLen);
            _readUsed = 0;
        }
        ConstBytes result((byte*)_readBuf.data() + _readUsed,
                        std::min(maxLen, _readBuf.size() - _readUsed));
        _readUsed += result.size();
        RETURN result;
    }


    Future<size_t> IStream::i_read(MutableBytes buf) {
        size_t bytesRead = 0;
        while (bytesRead < buf.size()) {
            ConstBytes bytes = AWAIT i_readNoCopy(buf.size() - bytesRead);
            if (bytes.size() == 0)
                break;
            ::memcpy(buf.data() + bytesRead, bytes.data(), bytes.size());
            bytesRead += bytes.size();
        }
        RETURN bytesRead;
    }


    Future<ConstBytes> IStream::readNoCopy(size_t maxLen) {
        NotReentrant nr(_readBusy);
        return i_readNoCopy(maxLen);
    }

    void IStream::unRead(size_t len) {
        NotReentrant nr(_readBusy);
        assert(len <= _readUsed);
        _readUsed -= len;
    }


    Future<size_t> IStream::read(MutableBytes buf) {
        NotReentrant nr(_readBusy);
        return i_read(buf);
    }


    Future<void> IStream::readExactly(MutableBytes buf) {
        size_t bytesRead = AWAIT read(buf);
        if (bytesRead < buf.size())
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
            size_t bytesRead = AWAIT i_read({&data[len], n});

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
            ConstBytes peek = AWAIT i_readNoCopy(maxLen - dataLen);

            // Check for a match that's split between the old and new data:
            if (!data.empty()) {
                data.append((char*)peek.data(), min(end.size() - 1, peek.size()));
                size_t startingAt = dataLen - std::min(end.size(), dataLen);
                if (auto found = data.find(end, startingAt); found != string::npos) {
                    // Found it:
                    found += end.size();
                    found = std::min(found, maxLen);
                    data.resize(found);
                    unRead(peek.size() - (found - dataLen));
                    break;
                } else {
                    data.resize(dataLen);
                }
            }

            // Check for a match in the new data:
            if (auto found = string_view((char*)peek.data(), peek.size()).find(end); found != string::npos) {
                found += end.size();
                found = std::min(found, maxLen - data.size());
                data.append((char*)peek.data(), found);
                unRead(peek.size() - found);
                break;
            }

            // Otherwise append all the input data and read more:
            size_t addLen = std::min(peek.size(), maxLen - data.size());
            data.append((char*)peek.data(), addLen);
            unRead(peek.size() - addLen);
        }
        RETURN data;
    }


    Future<void> IStream::write(ConstBytes buf) {
        NotReentrant nr(_writeBusy);
        return _write(buf);
    }


    Future<void> IStream::write(std::string str) {
        // Use co_await to ensure `str` stays in scope until the write completes.
        AWAIT write(ConstBytes(str));
        RETURN;
    }


    Future<void> IStream::write(const ConstBytes buffers[], size_t nBuffers) {
        NotReentrant nr(_writeBusy);
        for (size_t i = 0; i < nBuffers; ++i) {
            AWAIT _write(buffers[i]);
        }
    }


    Future<void> IStream::write(std::initializer_list<ConstBytes> buffers) {
        return write(buffers.begin(), buffers.size());
    }


}
