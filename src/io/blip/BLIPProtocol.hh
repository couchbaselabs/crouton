//
// BLIPProtocol.hh
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
#include "util/Bytes.hh"
#include "Error.hh"
#include "Logging.hh"
#include <cstdint>

namespace crouton::io::blip {

    enum MessageType : uint8_t {
        kRequestType     = 0,  // A message initiated by a peer
        kResponseType    = 1,  // A response to a Request
        kErrorType       = 2,  // A response indicating failure
        kAckRequestType  = 4,  // Acknowledgement of data received from a Request (internal)
        kAckResponseType = 5,  // Acknowledgement of data received from a Response (internal)
    };

    // Array mapping MessageType to a short mnemonic like "REQ".
    extern const char* const kMessageTypeNames[8];

    enum FrameFlags : uint8_t {
        kTypeMask   = 0x07,  // These 3 bits hold a MessageType
        kCompressed = 0x08,  // Message payload is gzip-deflated
        kUrgent     = 0x10,  // Message is given priority delivery
        kNoReply    = 0x20,  // Request only: no response desired
        kMoreComing = 0x40,  // Used only in frames, not in messages
    };

    enum class MessageNo : uint64_t { None = 0 };

    inline MessageNo operator+ (MessageNo m, int i) {return MessageNo{uint64_t(m) + i};}
    std::ostream& operator<< (std::ostream&, MessageNo);

    using MessageSize = uint64_t;

    // Implementation-imposed max encoded size of message properties (not part of protocol)
    constexpr uint64_t kMaxPropertiesSize = 100 * 1024;

    // How many bytes to receive before sending an ACK
    constexpr size_t kIncomingAckThreshold = 50000;


    enum class BLIPError : errorcode_t {
        InvalidFrame = 1,
        PropertiesTooLarge,
        CompressionError,
        BadChecksum,
    };


    extern std::shared_ptr<spdlog::logger> LBLIP;


#pragma mark - VARINTS:


    constexpr size_t kMaxVarintSize = 10;

    uint64_t readUVarint(ConstBytes& bytes);
    size_t putUVarint(uint64_t n, void* dst);

    inline void writeUVarint(uint64_t n, MutableBytes& out) {
        out = out.without_first(putUVarint(n, out.data()));
    }

}

namespace crouton {

    template <> struct ErrorDomainInfo<io::blip::BLIPError> {
        static constexpr string_view name = "BLIP";
        static string description(errorcode_t);
    };

}
