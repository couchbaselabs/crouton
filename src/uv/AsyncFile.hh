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
        
        /// Reads from the file. The `dst` buffer must remain valid until this completes.
        Future<int64_t> read(size_t len, void* dst);
        
        /// Writes to the file. The `src` buffer must remain valid until this completes.
        Future<void> write(size_t len, const void* src);
        
        /// Closes the file, if it's open. This method is synchronous.
        void close();
        
    private:
        int _fd = -1;
    };
    
}
