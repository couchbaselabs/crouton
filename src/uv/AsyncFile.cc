//
// AsyncFile.cc
//
// 
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

#include "AsyncFile.hh"
#include "UVInternal.hh"
#include <unistd.h>

namespace snej::coro::uv {
    using namespace std;
    
    
#pragma mark - FILE STREAM:
    
    
    const int FileStream::Append = O_APPEND;
    const int FileStream::Create = O_CREAT;
    const int FileStream::ReadOnly = O_RDONLY;
    const int FileStream::ReadWrite = O_RDWR;
    const int FileStream::WriteOnly = O_WRONLY;
    
    
    Future<void> FileStream::open(std::string const& path, int flags, int mode) {
        assert(!isOpen());
        uv::fs_request req;
        uv::check(uv_fs_open(curLoop(), &req, path.c_str(), flags, mode, req.callback));
        co_await req;
        
        uv::check(req.result);
        _fd = int(req.result);
        co_return;
    }
    
    
    Future<int64_t> FileStream::read(size_t len, void* dst) {
        assert(isOpen());
        uv::fs_request req;
        uv_buf_t buf = {.len = len, .base = (char*)dst};
        uv::check(uv_fs_read(curLoop(), &req, _fd, &buf, 1, -1, req.callback));
        co_await req;
        
        uv::check(req.result);
        co_return req.result;
    }
    
    
    Future<void> FileStream::write(size_t len, const void* src) {
        assert(isOpen());
        uv::fs_request req;
        uv_buf_t buf = {.len = len, .base = (char*)src};
        uv::check(uv_fs_write(curLoop(), &req, _fd, &buf, 1, -1, req.callback));
        co_await req;
        
        uv::check(req.result);
        co_return;
    }
    
    
    void FileStream::close() {
        if (isOpen()) {
            // Close synchronously, for simplicity
            uv_fs_t closeReq;
            uv_fs_close(curLoop(), &closeReq, _fd, nullptr);
            _fd = -1;
        }
    }
    
}
