//
// Filesystem.cc
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

#include "Filesystem.hh"
#include "Defer.hh"
#include "UVInternal.hh"
#include <uv.h>

namespace crouton::fs {
    using namespace std;

    struct fs_request : public uv_fs_t {
    public:
        void cleanup()  {uv_fs_req_cleanup(this);}
        ~fs_request()   {cleanup();}
    };


    static bool checkUnless(int err, int unless, const char* what) {
        if (err == unless)
            return false;
        check(err, what);
        return true;
    }


    bool mkdir(const char* path, int mode) {
        fs_request req;
        int err = uv_fs_mkdir(curLoop(), &req, path, mode, nullptr);
        return checkUnless(err, UV_EEXIST, "mkdir");
    }


    bool rmdir(const char* path) {
        fs_request req;
        int err = uv_fs_rmdir(curLoop(), &req, path, nullptr);
        return checkUnless(err, UV_ENOENT, "rmdir");
    }


    std::string mkdtemp(const char* templ) {
        fs_request req;
        check(uv_fs_mkdtemp(curLoop(), &req, templ, nullptr), "mkdtemp");
        return string(req.path);
    }


    statBuf stat(const char* path, bool followSymlink) {
        fs_request req;
        int err;
        if (followSymlink)
            err = uv_fs_stat(curLoop(), &req, path, nullptr);
        else
            err = uv_fs_lstat(curLoop(), &req, path, nullptr);
        check(err, "stat");
        return *(statBuf*)&req.statbuf;
    }


    void rename(const char* path, const char* newPath) {
        fs_request req;
        check(uv_fs_rename(curLoop(), &req, path, newPath, nullptr), "rename");
    }


    bool unlink(const char* path) {
        fs_request req;
        int err = uv_fs_unlink(curLoop(), &req, path, nullptr);
        return checkUnless(err, UV_ENOENT, "unlink");
    }

    
    void copyfile(const char* path, const char* newPath, copyfileFlags flags) {
        fs_request req;
        check(uv_fs_copyfile(curLoop(), &req, path, newPath, int(flags), nullptr), "copyfile");
    }


    std::string realpath(const char* path) {
        fs_request req;
        check(uv_fs_realpath(curLoop(), &req, path, nullptr), "realpath");
        return string((const char*)req.ptr);
    }


    Generator<dirent> readdir(const char* path) {
        auto loop = curLoop();
        fs_request req;
        check(uv_fs_opendir(loop, &req, path, nullptr), "readdir");
        auto dir = (uv_dir_t*)req.ptr;
        DEFER {uv_fs_closedir(loop, &req, dir, nullptr);};

        uv_dirent_t dirents[20];
        dir->dirents = dirents;
        dir->nentries = 20;

        while (true) {
            req.cleanup();
            int n = uv_fs_readdir(loop, &req, dir, nullptr);
            check(n, "readdir");
            if (n == 0)
                break;
            for (int i = 0; i < n; ++i) {
                YIELD dirent{dirents[i].name, dirent::type_t(dirents[i].type)};
            }
        }
    }
}
