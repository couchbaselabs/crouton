//
// Filesystem.hh
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
#include "Generator.hh"
#include <vector>

namespace crouton::io::fs {

#pragma mark - DIRECTORIES:

    /// Creates a directory. Returns false if a file/dir already exists at that path.
    bool mkdir(const char* path, int mode = 0644);

    /// Deletes a directory (must be empty.) Returns false if it doesn't exist.
    bool rmdir(const char* path);

    /// Makes a temporary directory.
    string mkdtemp(const char* templ);

    struct dirent {
        enum type_t : uint8_t {
            Unknown,
            File,
            Dir,
            Link,
            FIFO,
            Socket,
            CharDevice,
            BlockDevice
        };

        const char* name;
        type_t type;
    };

    /// Iterates the items in a directory.
    Generator<dirent> readdir(const char* path);


#pragma mark - FILE INFO:

    /// Returns the full absolute path without symlinks.
    string realpath(const char* path);

    typedef struct {        // copied from uv_timespec_t in uv.h
        long sec;
        long nsec;
    } timeSpec;

    struct statBuf {        // copied from uv_stat_t in uv.h
        uint64_t dev;
        uint64_t mode;
        uint64_t nlink;
        uint64_t uid;
        uint64_t gid;
        uint64_t rdev;
        uint64_t ino;
        uint64_t size;
        uint64_t blksize;
        uint64_t blocks;
        uint64_t flags;
        uint64_t gen;
        timeSpec atim;
        timeSpec mtim;
        timeSpec ctim;
        timeSpec birthtim;
    };

    /// Returns all the metadata for the item at that path.
    statBuf stat(const char* path, bool followSymlink =true);


#pragma mark - COPY / MOVE / DELETE:

    enum copyfileFlags {
        CopyFile_Excl           = 0x0001,   // Don't overwrite, return error instead
        CopyFile_FiClone        = 0x0002,   // Create copy-on-write link if possible, else copy
        CopyFile_FiClone_Force  = 0x0004,   // Fail if copy-on-write isn't possible
    };

    /// Copies from `path` to `newPath`.
    void copyfile(const char* path, const char* newPath, copyfileFlags);

    /// Moves/renames from `path` to `newPath`.
    void rename(const char* path, const char* newPath);

    /// Deletes the file at `path`. Returns false if it doesn't exist.
    bool unlink(const char* path);

}
