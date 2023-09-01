//
// TLSSocket.cc
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

#include "TLSSocket.hh"
#include "TLSContext.hh"
#include "stream_wrapper.hh" // for Buffer
#include "TCPSocket.hh"

// Code adapted from Couchbase's fork of sockpp:
// https://github.com/couchbasedeps/sockpp/blob/couchbase-master/src/mbedtls_context.cpp

namespace crouton::mbed {
    using namespace std;

    class TLSSocket::Impl {
    public:

        Impl(std::unique_ptr<IStream> stream, TLSContext& context, string const& hostname)
        :_stream(std::move(stream))
        ,_context(context)
        {
            context.setLogLevel(LogLevel::None);
            mbedtls_ssl_init(&_ssl);

            check(mbedtls_ssl_setup(&_ssl, _context.config()), "mbedtls_ssl_setup");
            if (!hostname.empty())
                check(mbedtls_ssl_set_hostname(&_ssl, hostname.c_str()), "mbedtls_ssl_set_hostname");

            mbedtls_ssl_send_t *send = [](void *ctx, const uint8_t *buf, size_t len) {
                return ((Impl*)ctx)->bio_send(buf, len);
            };
            mbedtls_ssl_recv_t *recv = [](void *ctx, uint8_t *buf, size_t len) {
                return ((Impl*)ctx)->bio_recv(buf, len);
            };
            mbedtls_ssl_set_bio(&_ssl, this, send, recv, nullptr);
        }


        ~Impl() {mbedtls_ssl_free(&_ssl);}


        bool isOpen() const {return _tlsOpen;}


        // Coroutine that opens the underlying stream and then runs the TLS handshake.
        Future<void> handshake() {
            AWAIT _stream->open();
            _tcpOpen = true;

            int status;
            do {
                AWAIT processIO();
                status = mbedtls_ssl_handshake(&_ssl);
            } while (status == MBEDTLS_ERR_SSL_WANT_READ || status == MBEDTLS_ERR_SSL_WANT_WRITE
                     || status == MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS);
            check(status, "mbedtls_ssl_handshake");

            // After the handshake, verify the peer cert:
            uint32_t verify_flags = mbedtls_ssl_get_verify_result(&_ssl);
            if (verify_flags != 0 && verify_flags != uint32_t(-1)
                    && !(verify_flags & MBEDTLS_X509_BADCERT_SKIP_VERIFY)) {
                char vrfy_buf[512];
                mbedtls_x509_crt_verify_info(vrfy_buf, sizeof( vrfy_buf ), "", verify_flags);
                log(1, "Cert verify failed: %s", vrfy_buf );
                throw MbedError(MBEDTLS_ERR_X509_CERT_VERIFY_FAILED, "verifying cert");
            }
            _tlsOpen = true;
        }


        // TLSSocket wants to write.
        Future<void> write(ConstBuf buf) {
            //cerr << "TLSStream write(" << buf.len << ") ...\n";
            if (buf.len == 0)
                RETURN;
            while (true) {
                int result = mbedtls_ssl_write(&_ssl,  (const uint8_t*)buf.base, buf.len);
                if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) {
                    // Need to complete some async I/O:
                    AWAIT processIO();
                } else if (result < 0) {
                    // Error!
                    check(result, "write");
                } else if (result < buf.len) {
                    // Incomplete write; update the buffer and repeat:
                    //cerr << "\tTLSStream.write wrote " << result << "; continuing..." << endl;
                    buf.base = (char*)buf.base + result;
                    buf.len -= result;
                } else {
                    // Done!
                    //cerr << "\tTLSStream.write done" << endl;
                    RETURN;
                }
            }
        }


        // TLSSocket wants to read.
        Future<ssize_t> read(void *buf, size_t maxLen) {
            //cerr << "TLSStream read(" << maxLen << ") ...\n";
            while (true) {
                int result = mbedtls_ssl_read(&_ssl, (uint8_t*)buf, maxLen);
                if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) {
                    // Need to complete some async I/O:
                    AWAIT processIO();
                } else if (result > 0) {
                    // Done!
                    //cerr << "\tTLSStream.read got " << result << endl;
                    //cerr << "\t\t" << string_view((char*)buf, result) << endl;
                    RETURN result;
                } else if (result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
                    // Peer closed their write stream; EOF
                    RETURN 0;
                } else if (result < 0) {
                    // Error!
                    check(result, "read");
                }
            }
        }


        // Implements both TLSSocket's close() and closeWrite() methods.
        Future<void> close(bool fully) {
            if (_tcpOpen) {
                //cerr << "TLSStream close ...\n";
                mbedtls_ssl_close_notify(&_ssl);
                AWAIT processIO();
                _tlsOpen = false;
                if (fully) {
                    _tcpOpen = _tlsOpen = false;
                    AWAIT _stream->close();
                } else {
                    AWAIT _stream->closeWrite();
                }
                _tcpOpen = false;
                //cerr << "TLSStream is now closed.\n";
            }
        }

    private:

        // mbedTLS callback: it wants to write to the network.
        int bio_send(const void* buf, size_t length) {
            //cerr << "BIO_SEND(" << length << ") ";
            int result;
            if (!_tcpOpen)
                result = MBEDTLS_ERR_NET_CONN_RESET;
            else if (_pendingWrite)
                result = MBEDTLS_ERR_SSL_WANT_WRITE;  // still busy with the last write
            else {
                //cerr << "[async write " << length << "] " << endl;
                _pendingWrite.emplace(_stream->write(string((char*)buf, length)));
                result = int(length);
            }
            //cerr << "-> " << result << endl;
            return result;
        }


        // mbedTLS callback: it wants to read from the network.
        int bio_recv(void* dst, size_t maxLength) {
            //cerr << "BIO_RECV(" << maxLength << ") ";
            int result;
            if (!_tcpOpen) {
                // socket is closed
                result = MBEDTLS_ERR_NET_CONN_RESET;
            } else if (_pendingRead) {
                // waiting for a read to complete
                result = MBEDTLS_ERR_SSL_WANT_READ;
            } else if (_readBuf.len > 0) {
                // Copy bytes to the mbed buffer:
                size_t length = std::min(_readBuf.len, maxLength);
                ::memcpy(dst, _readBuf.base, length);
                _readBuf.base = (char*)_readBuf.base + length;
                _readBuf.len -= length;
                result = int(length);
            } else if (!_readEOF) {
                // We've read the entire buffer; time to read again:
                //cerr << "[async read] " << endl;
                _pendingRead.emplace(_stream->readNoCopy(100000));
                _readBuf = {};
                result = MBEDTLS_ERR_SSL_WANT_READ;
            } else {
                // At EOF:
                result = 0;
            }
            //cerr << "-> " << result << endl;
            return result;
        }


        // Awaits any read/write calls initiated by bio_send or bio_recv.
        // It's important to process writes before reads, to avoid deadlocks where each peer is
        // waiting for the other to send data.
        Future<void> processIO() {
            while (_pendingWrite || _pendingRead) {
                if (_pendingWrite) {
                    //cerr << "processIO writing...\n";
                    AWAIT *_pendingWrite;
                    _pendingWrite.reset();
                } else if (_pendingRead) {
                    //cerr << "processIO reading...\n";
                    _readBuf = AWAIT *_pendingRead;
                    //cerr << "processIO read " << _readBuf.len << " bytes\n";
                    if (_readBuf.len == 0)
                        _readEOF = true;
                    _pendingRead.reset();
                }
            }
        }


    private:
        std::unique_ptr<IStream>    _stream;            // The underlying TCP stream
        TLSContext&                 _context;           // The shared mbedTLS context
        mbedtls_ssl_context         _ssl;               // The mbedTLS SSL object
        optional<Future<void>>      _pendingWrite;      // In-progress write operation, if any
        optional<Future<ConstBuf>>  _pendingRead;       // In-progress read operation, if any
        ConstBuf                    _readBuf;           // The (remaining) data read from _stream
        bool                        _readEOF = false;   // True when TCP read stream reaches EOF
        bool                        _tcpOpen = false;   // True when the TCP stream is open
        bool                        _tlsOpen = false;   // True when the TLS connection is open
    };


#pragma mark - TLS STREAM:


    TLSSocket::TLSSocket()
    :_inputBuf(make_unique<Buffer>())
    { }


    TLSSocket::~TLSSocket() = default;


    bool TLSSocket::isOpen() const  {
        return _impl && _impl->isOpen();
    }

    Future<void> TLSSocket::open() {
        assert(!_impl);
        NotReentrant nr(_busy);
        auto stream = make_unique<TCPSocket>();
        stream->bind(_binding->address, _binding->port);
        stream->setNoDelay(_binding->noDelay);
        stream->keepAlive(_binding->keepAlive);
        _impl = make_unique<Impl>(std::move(stream),
                                  TLSContext::defaultClientContext(),
                                  _binding->address);
        _binding = nullptr;
        return _impl->handshake();
    }

    Future<ConstBuf> TLSSocket::_readNoCopy(size_t maxLen)  {
        NotReentrant nr(_busy);
        if (_inputBuf->empty()) {
            auto len = AWAIT _impl->read(_inputBuf->data, _inputBuf->kCapacity);
            _inputBuf->length = uint32_t(len);
            _inputBuf->used = 0;
            if (_inputBuf->empty())
                RETURN {};  // Reached EOF
        }
        // Advance _inputBuf->used and return the pointer:
        RETURN _inputBuf->read(maxLen);

    }

    Future<void> TLSSocket::_write(ConstBuf buf)  {
        NotReentrant nr(_busy);
        return _impl->write(buf);
    }

    Future<void> TLSSocket::close()  {
        NotReentrant nr(_busy);
        AWAIT _impl->close(true);
    }

    Future<void> TLSSocket::closeWrite()  {
        NotReentrant nr(_busy);
        AWAIT _impl->close(false);
    }

}
