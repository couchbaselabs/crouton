//
// Codec.cc
//
// Copyright 2017-Present Couchbase, Inc.
//
// Use of this software is governed by the Business Source License included
// in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
// in that file, in accordance with the Business Source License, use of this
// software will be governed by the Apache License, Version 2.0, included in
// the file licenses/APL2.txt.
//


// For zlib API documentation, see: https://zlib.net/manual.html


#include "Codec.hh"
#include "BLIPProtocol.hh"
#include "Endian.hh"
#include "Logging.hh"
#include <algorithm>
#include <mutex>

namespace crouton::io::blip {
    using namespace std;

    // "The windowBits parameter is the base two logarithm of the window size (the size of the
    // history buffer)." 15 is the max, and the suggested default value.
    static constexpr int kZlibWindowSize = 15;

    // True to use raw DEFLATE format, false to add the zlib header & checksum
    static constexpr bool kZlibRawDeflate = true;

    // "The memLevel parameter specifies how much memory should be allocated for the internal
    // compression state." Default is 8; we bump it to 9, which uses 256KB.
    static constexpr int kZlibDeflateMemLevel = 9;


    shared_ptr<spdlog::logger> LZip = MakeLogger("Zip");

    Codec::Codec()
    :_checksum((uint32_t)crc32(0, nullptr, 0))  // the required initial value
    {}

    void Codec::addToChecksum(ConstBytes data) {
        _checksum = (uint32_t)crc32(_checksum, (const Bytef*)data.data(), (int)data.size());
    }

    void Codec::writeChecksum(MutableBytes& output) const {
        uint32_t chk = endian::encodeBig(_checksum);
        __unused bool ok = output.write(&chk, sizeof(chk));
        assert(ok);
    }

    void Codec::readAndVerifyChecksum(ConstBytes& input) const {
        uint32_t chk;
        static_assert(kChecksumSize == sizeof(chk), "kChecksumSize is wrong");
        if (!input.readAll(&chk, sizeof(chk)))
            Error::raise(BLIPError::InvalidFrame, "BLIP message ends before checksum");
        chk = endian::decodeBig(chk);
        if (chk != _checksum)
            Error::raise(BLIPError::BadChecksum);
    }

    // Uncompressed write: just copies input bytes to output (updating checksum)
    MutableBytes Codec::_writeRaw(ConstBytes& input, MutableBytes& output) {
        LZip->debug("Copying {} bytes into {}-byte buf (no compression)", input.size(), output.size());
        assert(output.size() > 0);
        auto outStart = output.data();
        size_t count = output.write(input);
        addToChecksum({input.data(), count});
        input = input.without_first(count);
        return MutableBytes(outStart, count);
    }

    void ZlibCodec::check(int ret) const {
        if (ret < 0 && ret != Z_BUF_ERROR) {
            string msg = fmt::format("zlib error {}: {}", ret, (_z.msg ? _z.msg : "???"));
            Error::raise(BLIPError::CompressionError, msg);
        }
    }

    void ZlibCodec::_write(const char* operation, ConstBytes& input, MutableBytes& output, Mode mode,
                           size_t maxInput) {
        _z.next_in  = (Bytef*)input.data();
        auto inSize = _z.avail_in = (unsigned)std::min(input.size(), maxInput);
        _z.next_out = (Bytef*)output.data();
        auto outSize = _z.avail_out = (unsigned)output.size();
        assert(outSize > 0);
        assert(mode > Mode::Raw);
        int result = _flate(&_z, (int)mode);
        LZip->debug("    {}(in {}, out {}, mode {})-> {}; read {} bytes, wrote {} bytes",
                   operation, inSize, outSize,
                   (int)mode, result, (long)(_z.next_in - (uint8_t*)input.data()),
                   (long)(_z.next_out - (uint8_t*)output.data()));
        if (!kZlibRawDeflate) 
            _checksum = (uint32_t)_z.adler;
        input = ConstBytes(_z.next_in, input.endByte());
        output = MutableBytes(_z.next_out, output.endByte());
        check(result);
    }

    
#pragma mark - DEFLATER:


    Deflater::Deflater(CompressionLevel level) : ZlibCodec(::deflate) {
        check(::deflateInit2(&_z, level, Z_DEFLATED, kZlibWindowSize * (kZlibRawDeflate ? -1 : 1), kZlibDeflateMemLevel,
                             Z_DEFAULT_STRATEGY));
    }

    Deflater::~Deflater() { ::deflateEnd(&_z); }

    MutableBytes Deflater::write(ConstBytes& input, MutableBytes& output, Mode mode) {
        if (mode == Mode::Raw) 
            return _writeRaw(input, output);

        auto outStart = output.data();
        ConstBytes  origInput = input;
        size_t origOutputSize = output.size();
        LZip->debug("Compressing {} bytes into {}-byte buf", input.size(), origOutputSize);

        switch (mode) {
            case Mode::NoFlush:
                _write("deflate", input, output, mode);
                break;
            case Mode::SyncFlush:
                _writeAndFlush(input, output);
                break;
            default:
                Error::raise(CroutonError::InvalidArgument, "invalid Codec mode");
        }

        if (kZlibRawDeflate) 
            addToChecksum({origInput.data(), input.data()});

        LZip->debug("    compressed {} bytes to {} ({}%), {} unflushed",
                    (origInput.size() - input.size()),
                    (origOutputSize - output.size()),
                    (origOutputSize - output.size()) * 100 / (origInput.size() - input.size()),
                    unflushedBytes());
        return MutableBytes(outStart, output.data());
    }

    void Deflater::_writeAndFlush(ConstBytes& input, MutableBytes& output) {
        // If we try to write all of the input, and there isn't room in the output, the zlib
        // codec might end up with buffered data that hasn't been output yet (even though we
        // told it to flush.) To work around this, write the data gradually and stop before
        // the output fills up.
        static constexpr size_t kHeadroomForFlush = 12;
        static constexpr size_t kStopAtOutputSize = 100;

        Mode curMode = Mode::PartialFlush;
        while (input.size() > 0) {
            if (output.size() >= deflateBound(&_z, (unsigned)input.size())) {
                // Entire input is guaranteed to fit, so write it & flush:
                curMode = Mode::SyncFlush;
                _write("deflate", input, output, Mode::SyncFlush);
            } else {
                // Limit input size to what we know can be compressed into output.
                // Don't flush, because we may try to write again if there's still room.
                _write("deflate", input, output, curMode, output.size() - kHeadroomForFlush);
            }
            if (output.size() <= kStopAtOutputSize)
                break;
        }

        if (curMode != Mode::SyncFlush) {
            // Flush if we haven't yet (consuming no input):
            _write("deflate", input, output, Mode::SyncFlush, 0);
        }
    }

    unsigned Deflater::unflushedBytes() const {
        unsigned bytes;
        int      bits;
        check(deflatePending(&_z, &bytes, &bits));
        return bytes + (bits > 0);
    }


#pragma mark - INFLATER:


    Inflater::Inflater() : ZlibCodec(::inflate) {
        check(::inflateInit2(&_z, kZlibRawDeflate ? (-kZlibWindowSize) : (kZlibWindowSize + 32)));
    }

    Inflater::~Inflater() { ::inflateEnd(&_z); }

    MutableBytes Inflater::write(ConstBytes& input, MutableBytes& output, Mode mode) {
        if (mode == Mode::Raw) 
            return _writeRaw(input, output);

        LZip->debug("Decompressing {} bytes into {}-byte buf", input.size(), output.size());
        auto outStart = (uint8_t*)output.data();
        _write("inflate", input, output, mode);
        if (kZlibRawDeflate)
            addToChecksum({outStart, output.data()});

        LZip->trace("    decompressed {} bytes: %.*s",
                    (long)((uint8_t*)output.data() - outStart),
                    string_view((char*)outStart, (char*)output.data()));
        return MutableBytes(outStart, output.data());
    }

} 
