//
// uv_stream_wrapper.hh
//
// 
//

#pragma once
#include "uv.h"
#include "UVInternal.hh"
#include <memory>

namespace snej::coro::uv {

    struct Buffer {
        static constexpr size_t kCapacity = 65536 - 2 *sizeof(uint32_t);
        uint32_t length = 0;
        uint32_t used = 0;
        char data[kCapacity];

        size_t available() const {return length - used;}
    };

    using BufferRef = std::unique_ptr<Buffer>;


    /** Very low-level wrapper around a libuv stream. Used as an adapter for uvtls. */
    struct stream_wrapper {
        virtual ~stream_wrapper() = default;

        using ReadCallback = std::function<void(BufferRef, int err)>;

        virtual int read_start(ReadCallback cb) {
            _readCallback = cb;
            // Deliver all pending input buffers:
            while (_readCallback) {
                if (!_input.empty()) {
                    BufferRef buf = std::move(_input[0]);
                    _input.erase(_input.begin());
                    _readCallback(std::move(buf), 0);
                } else if (_readError) {
                    _readCallback(nullptr, _readError);
                    _readError = 0;
                } else {
                    break;
                }
            }
            return 0;
        }

        virtual int read_stop() {
            _readCallback = nullptr;
            return 0;
        }

        void recycleBuffer(BufferRef buf) {
            buf->length = 0;
            _spare.emplace_back(std::move(buf));
        }

        virtual int write(uv_write_t *req, const uv_buf_t bufs[], unsigned nbufs, uv_write_cb cb) =0;
        virtual int try_write(const uv_buf_t bufs[], unsigned nbufs) =0;
        virtual bool is_readable() =0;
        virtual bool is_writable() =0;
        virtual int shutdown(uv_shutdown_t*, uv_shutdown_cb) =0;

    protected:
        void alloc(uv_buf_t* uvbuf) {
            assert(!_readingBuf);
            if (_spare.empty()) {
                _readingBuf = std::make_unique<Buffer>();
            } else {
                _readingBuf = std::move(_spare[0]);
                _spare.erase(_spare.begin());
            }
            uvbuf->base = _readingBuf->data;
            uvbuf->len = Buffer::kCapacity;
        }

        void read(ssize_t nread, const uv_buf_t* uvbuf) {
            if (nread > 0) {
                assert(uvbuf->base == _readingBuf->data);
                _readingBuf->length = uint32_t(nread);
                if (_readCallback)
                    _readCallback(std::move(_readingBuf), 0);
                else
                    _input.push_back(std::move(_readingBuf));
            } else {
                if (_readingBuf)
                    _spare.push_back(std::move(_readingBuf));
                if (nread < 0) {
                    if (_readCallback)
                        _readCallback(nullptr, int(nread));
                    else
                        _readError = int(nread);
                }
            }
        }

        std::vector<BufferRef> _input, _spare;
        BufferRef _readingBuf;
        ReadCallback _readCallback = nullptr;
        int _readError = 0;
    };


    /** Wrapper around a uv_stream_t. */
    struct uv_stream_wrapper final : public stream_wrapper {

        explicit uv_stream_wrapper(uv_stream_t *stream) :_stream(stream) {_stream->data = this;}
        explicit uv_stream_wrapper(uv_tcp_t *socket)    :uv_stream_wrapper((uv_stream_t*)socket) { }
        explicit uv_stream_wrapper(uv_pipe_t *pipe)     :uv_stream_wrapper((uv_stream_t*)pipe) { }

        ~uv_stream_wrapper() {closeHandle(_stream);}

        int read_start(ReadCallback cb) override {
            int err = stream_wrapper::read_start(cb);
            if (err == 0 && _readCallback) {
                auto alloc = [](uv_handle_t* h, size_t, uv_buf_t* uvbuf) {
                    auto stream = (uv_stream_wrapper*)h->data;
                    stream->alloc(uvbuf);
                };
                auto read = [](uv_stream_t* h, ssize_t nread, const uv_buf_t* uvbuf) {
                    auto stream = (uv_stream_wrapper*)h->data;
                    stream->read(nread, uvbuf);
                };
                err = uv_read_start(_stream, alloc, read);
            }
            return err;
        }

        int read_stop() override {
            stream_wrapper::read_stop();
            return uv_read_stop(_stream);
        }

        int write(uv_write_t *req, const uv_buf_t bufs[], unsigned nbufs, uv_write_cb cb) override {
            return uv_write(req, _stream, bufs, nbufs, cb);
        }

        int try_write(const uv_buf_t bufs[], unsigned nbufs) override {
            return uv_try_write(_stream, bufs, nbufs);
        }

        bool is_readable() override {return uv_is_readable(_stream);}
        bool is_writable() override {return uv_is_writable(_stream);}

        int shutdown(uv_shutdown_t* req, uv_shutdown_cb cb) override {
            return uv_shutdown(req, _stream, cb);
        }

        uv_stream_t* _stream;
    };

}
