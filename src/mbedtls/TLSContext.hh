//
// TLSContext.hh
//
// Copyright 2020-Present Couchbase, Inc. All rights reserved.
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

// Code adapted from Couchbase's fork of sockpp
// <https://github.com/couchbasedeps/sockpp/blob/couchbase-master/src/mbedtls_context.cpp>
// whose copyright is:

// This file is part of the "sockpp" C++ socket library.
//
// Copyright (c) 2014-2017 Frank Pagliughi
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
// IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#pragma once

#if defined(_WIN32)
#   if !defined(_CRT_SECURE_NO_DEPRECATE)
#       define _CRT_SECURE_NO_DEPRECATE
#   endif
#   include <winsock2.h>
#   include <ws2tcpip.h>
#endif

#include <mbedtls/base64.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/debug.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pem.h>
#include <mbedtls/ssl.h>
#include <cstring>

#ifdef __APPLE__
    #include <fcntl.h>
    #include <TargetConditionals.h>
    #ifdef TARGET_OS_OSX
        // For macOS read_system_root_certs():
        #include <Security/SecImportExport.h>
        #include <Security/SecTrust.h>
    #endif
#elif !defined(_WIN32)
    // For Unix read_system_root_certs():
    #include <dirent.h>
    #include <fcntl.h>
    #include <fnmatch.h>
    #include <fstream>
    #include <iostream>
    #include <sstream>
    #include <sys/stat.h>
#else
    #include <wincrypt.h>
    #include <sstream>
    #pragma comment (lib, "crypt32.lib")
    #pragma comment (lib, "cryptui.lib")
#endif

namespace crouton::mbed {
    using namespace std;

#define ENABLE_MBEDTLS_LOG

#ifdef ENABLE_MBEDTLS_LOG
    #define log(LEVEL, FMT,...) do { \
        auto ssl = &_ssl; \
        MBEDTLS_SSL_DEBUG_MSG(LEVEL, ("Crouton: " FMT, ## __VA_ARGS__)); \
    } while(0)
#else
    #define log(LEVEL, FMT,...) do { } while(0)
#endif


    // Log level values corresponding to ones used by mbedTLS
    enum class LogLevel : int {
        None = 0,
        Error,
        StateChange,
        Info,
        Verbose
    };


    /// Errors thrown by this TLS implementation.
    class MbedError : public runtime_error {
    public:
        explicit MbedError(int code_, const char* what)
            :runtime_error(to_string(code_) + " in " + what)
            ,code(code_) { }

        static std::string messageFor(int code, bool withCode) {
            char msg[100];
            mbedtls_strerror(code, msg, sizeof(msg));
            if (withCode) {
                size_t len = strlen(msg);
                snprintf(msg + len, sizeof(msg) - len, " (-0x%04x)", -code);
            }
            return string(msg);
        }

        static std::string codeToString(int status) {
            char msg[20];
            return string(msg);
        }

        /// mbedTLS status code
        int code;
    };

    static void check(int err, const char *what) {
        if (err) {
            cerr << "MBEDTLS EXCEPTION: " << MbedError::messageFor(err, true) << endl;
            throw MbedError(err, what);
        }
    }


    // Simple RAII helper for mbedTLS cert struct
    struct cert : public mbedtls_x509_crt {
        cert()  {mbedtls_x509_crt_init(this);}
        ~cert() {mbedtls_x509_crt_free(this);}
    };


    /** Context / configuration for TLS (SSL) connections.
        A single context can be shared by any number of connection instances.
        A context must remain in scope as long as any connection using it remains in scope.*/
    class TLSContext {
    public:

        /// A default context instance for client use.
        static TLSContext& defaultClientContext() {
            static TLSContext* sContext = new TLSContext(MBEDTLS_SSL_IS_CLIENT);
            return *sContext;
        }


        /// Constructs a context.
        /// @param endpoint  Must be MBEDTLS_SSL_IS_CLIENT or MBEDTLS_SSL_IS_SERVER
        explicit TLSContext(int endpoint) {
            mbedtls_ssl_config_init(&_config);
            mbedtls_ssl_conf_dbg(&_config, debugCallback, this);
            mbedtls_ssl_conf_rng(&_config, mbedtls_ctr_drbg_random, get_drbg_context());
            check(mbedtls_ssl_config_defaults(&_config,
                                              endpoint,
                                              MBEDTLS_SSL_TRANSPORT_STREAM,
                                              MBEDTLS_SSL_PRESET_DEFAULT),
                  "mbedtls_ssl_config_defaults");

            if (auto roots = get_system_root_certs())
                mbedtls_ssl_conf_ca_chain(&_config, roots, nullptr);
        }

        ~TLSContext() {
            mbedtls_ssl_config_free(&_config);
        }

        mbedtls_ssl_config* config() {
            return &_config;
        }

        void setLogLevel(LogLevel level) {
            mbedtls_debug_set_threshold(int(level));
        }

    private:

        static void debugCallback(void *ctx, int level, const char *file, int line, const char *msg) {
            size_t msgLen = strlen(msg);
            if (msgLen > 0 && msg[msgLen - 1] == '\n')
                --msgLen;
            if (auto lastSlash = strrchr(file, '/'))
                file = lastSlash + 1;
            fprintf(stderr, "MBEDTLS: [%d] %.*s  <%s:%d>\n", level, int(msgLen), msg, file, line);
        }

        // Returns a shared singleton mbedTLS random-number generator context.
        static mbedtls_ctr_drbg_context* get_drbg_context() {
            static const char* k_entropy_personalization = "Crouton";
            static mbedtls_entropy_context  s_entropy;
            static mbedtls_ctr_drbg_context s_random_ctx;

            static once_flag once;
            call_once(once, []() {
                mbedtls_entropy_init( &s_entropy );

#if defined(_MSC_VER)
#if !WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
                auto uwp_entropy_poll = [](void *data, unsigned char *output, size_t len,
                                           size_t *olen) -> int
                {
                    NTSTATUS status = BCryptGenRandom(NULL, output, (ULONG)len, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
                    if (status < 0) {
                        return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
                    }

                    *olen = len;
                    return 0;
                };
                mbedtls_entropy_add_source(&s_entropy, uwp_entropy_poll, NULL, 32,
                                           MBEDTLS_ENTROPY_SOURCE_STRONG);
#endif
#endif

                mbedtls_ctr_drbg_init( &s_random_ctx );
                int ret = mbedtls_ctr_drbg_seed(&s_random_ctx, mbedtls_entropy_func, &s_entropy,
                                                (const uint8_t *)k_entropy_personalization,
                                                strlen(k_entropy_personalization));
                if (ret != 0) {
                    throw MbedError(ret, "mbedtls_ctr_drbg_seed");
                }
            });
            return &s_random_ctx;
        }


        // Returns the set of system trusted root CA certs.
        mbedtls_x509_crt* get_system_root_certs() {
            static once_flag once;
            static mbedtls_x509_crt *s_system_root_certs;
            call_once(once, []() {
                // One-time initialization:
                string certsPEM = read_system_root_certs();
                if (!certsPEM.empty())
                    s_system_root_certs = parse_cert(certsPEM, true).release();
            });
            return s_system_root_certs;
        }


        // Parses a data blob containing one or many X.59 certs.
        static unique_ptr<cert> parse_cert(const std::string &cert_data, bool partialOk) {
            unique_ptr<cert> c(new cert);
            mbedtls_x509_crt_init(c.get());
            int ret = mbedtls_x509_crt_parse(c.get(),
                                             (const uint8_t*)cert_data.data(),
                                             cert_data.size() + 1);
            if (ret > 0 && !partialOk)
                ret = MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
            if (ret < 0)
                throw MbedError(ret, "mbedtls_x509_crt_parse");
            return c;
        }


#pragma mark - PLATFORM SPECIFIC:


        // mbedTLS does not have built-in support for reading the OS's trusted root certs.

#ifdef __APPLE__
        // Read system root CA certs on macOS.
        // (Sadly, SecTrustCopyAnchorCertificates() is not available on iOS)
        static string read_system_root_certs() {
#if TARGET_OS_OSX
            CFArrayRef roots;
            OSStatus err = SecTrustCopyAnchorCertificates(&roots);
            if (err)
                return {};
            CFDataRef pemData = nullptr;
            err =  SecItemExport(roots, kSecFormatPEMSequence, kSecItemPemArmour, nullptr, &pemData);
            CFRelease(roots);
            if (err)
                return {};
            string pem((const char*)CFDataGetBytePtr(pemData), CFDataGetLength(pemData));
            CFRelease(pemData);
            return pem;
#else
            // fallback -- no certs
            return "";
#endif
        }

#elif defined(_WIN32)
        // Windows:
        static string read_system_root_certs() {
            PCCERT_CONTEXT pContext = nullptr;
            HCERTSTORE hStore = CertOpenStore(CERT_STORE_PROV_SYSTEM_A, 0, NULL,
                                              CERT_SYSTEM_STORE_CURRENT_USER, "ROOT");
            if(hStore == nullptr) {
                return "";
            }

            stringstream certs;
            while ((pContext = CertEnumCertificatesInStore(hStore, pContext))) {
                DWORD pCertPEMSize = 0;
                if (!CryptBinaryToStringA(pContext->pbCertEncoded, pContext->cbCertEncoded, CRYPT_STRING_BASE64HEADER, NULL, &pCertPEMSize)) {
                    return "";
                }
                LPSTR pCertPEM = (LPSTR)malloc(pCertPEMSize);
                if (!CryptBinaryToStringA(pContext->pbCertEncoded, pContext->cbCertEncoded, CRYPT_STRING_BASE64HEADER, pCertPEM, &pCertPEMSize)) {
                    return "";
                }
                certs.write(pCertPEM, pCertPEMSize);
                free(pCertPEM);
            }

            CertCloseStore(hStore, CERT_CLOSE_STORE_FORCE_FLAG);
            return certs.str();
        }

#else
        // Read system root CA certs on Linux using OpenSSL's cert directory
        static string read_system_root_certs() {
#ifdef __ANDROID__
            static constexpr const char* CERTS_DIR  = "/system/etc/security/cacerts/";
#else
            static constexpr const char* CERTS_DIR  = "/etc/ssl/certs/";
            static constexpr const char* CERTS_FILE = "ca-certificates.crt";
#endif

            stringstream certs;
            char buf[1024];
            // Subroutine to append a file to the `certs` stream:
            auto read_file = [&](const string &file) {
                ifstream in(file);
                char last_char = '\n';
                while (in) {
                    in.read(buf, sizeof(buf));
                    auto n = in.gcount();
                    if (n > 0) {
                        certs.write(buf, n);
                        last_char = buf[n-1];
                    }
                }
                if (last_char != '\n')
                    certs << '\n';
            };

            struct stat s;
            if (stat(CERTS_DIR, &s) == 0 && S_ISDIR(s.st_mode)) {
#ifndef __ANDROID__
                string certs_file = string(CERTS_DIR) + CERTS_FILE;
                if (stat(certs_file.c_str(), &s) == 0) {
                    // If there is a file containing all the certs, just read it:
                    read_file(certs_file);
                } else
#endif
                {
                    // Otherwise concatenate all the certs found in the dir:
                    auto dir = opendir(CERTS_DIR);
                    if (dir) {
                        struct dirent *ent;
                        while (nullptr != (ent = readdir(dir))) {
#ifndef __ANDROID__
                            if (fnmatch("?*.pem", ent->d_name, FNM_PERIOD) == 0
                                || fnmatch("?*.crt", ent->d_name, FNM_PERIOD) == 0)
#endif
                                read_file(string(CERTS_DIR) + ent->d_name);
                        }
                        closedir(dir);
                    }
                }
            }
            return certs.str();
        }

#endif

        mbedtls_ssl_config  _config;
    };

}
