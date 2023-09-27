//
// Codec.hh
//
// Copyright 2017-Present Couchbase, Inc.
//
// Use of this software is governed by the Business Source License included
// in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
// in that file, in accordance with the Business Source License, use of this
// software will be governed by the Apache License, Version 2.0, included in
// the file licenses/APL2.txt.
//

#pragma once
#include "Bytes.hh"
#include <zlib.h>

namespace crouton::io::blip {


    /** Abstract encoder/decoder class. */
    class Codec {
      public:
        Codec();
        virtual ~Codec() = default;

        // See https://zlib.net/manual.html#Basic for info about modes
        enum class Mode : int {
            Raw     = -1,  // not a zlib mode; means copy bytes w/o compression
            NoFlush = 0,
            PartialFlush,
            SyncFlush,
            FullFlush,
            Finish,
            Block,
            Trees,

            Default = SyncFlush
        };

        /** Reads data from `input` and writes transformed data to `output`.
            Each Bytes's start is moved forwards past the consumed data. */
        virtual MutableBytes write(ConstBytes& input, MutableBytes& output, Mode = Mode::Default) = 0;

        /** Number of bytes buffered in the codec that haven't been written to
            the output yet for lack of space. */
        virtual unsigned unflushedBytes() const { return 0; }

        static constexpr size_t kChecksumSize = 4;

        /** Writes the codec's current checksum to the output MutableBytes.
            This is a CRC32 checksum of all the unencoded data processed so far. */
        void writeChecksum(MutableBytes& output) const;

        /** Reads a checksum from the input slice and compares it with the codec's current one.
            If they aren't equal, throws an exception. */
        void readAndVerifyChecksum(ConstBytes& input) const;

      protected:
        void addToChecksum(ConstBytes data);
        MutableBytes _writeRaw(ConstBytes& input, MutableBytes& output);

        uint32_t _checksum{0};
    };


    /** Abstract base class of Zlib-based codecs Deflater and Inflater */
    class ZlibCodec : public Codec {
      protected:
        using FlateFunc = int (*)(z_stream*, int);

        explicit ZlibCodec(FlateFunc flate) : _flate(flate) {}

        void _write(const char* operation, ConstBytes& input, MutableBytes& output, Mode,
                    size_t maxInput = SIZE_MAX);
        void check(int) const;

        mutable ::z_stream _z{};
        FlateFunc const    _flate;
    };


    /** Compressing codec that performs a zlib/gzip "deflate". */
    class Deflater final : public ZlibCodec {
      public:
        enum CompressionLevel : int8_t {
            NoCompression      = 0,
            FastestCompression = 1,
            BestCompression    = 9,
            DefaultCompression = -1,
        };

        explicit Deflater(CompressionLevel = DefaultCompression);
        ~Deflater() override;

        MutableBytes write(ConstBytes& input, MutableBytes& output, Mode = Mode::Default) override;
        unsigned unflushedBytes() const override;

      private:
        void _writeAndFlush(ConstBytes& input, MutableBytes& output);
    };


    /** Decompressing codec that performs a zlib/gzip "inflate". */
    class Inflater final : public ZlibCodec {
      public:
        Inflater();
        ~Inflater() override;

        MutableBytes write(ConstBytes& input, MutableBytes& output, Mode = Mode::Default) override;
    };

}
