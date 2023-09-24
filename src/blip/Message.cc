//
// Message.cc
//
// Copyright 2017-Present Couchbase, Inc.
//
// Use of this software is governed by the Business Source License included
// in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
// in that file, in accordance with the Business Source License, use of this
// software will be governed by the Apache License, Version 2.0, included in
// the file licenses/APL2.txt.
//

#include "Message.hh"
#include "MessageBuilder.hh"
#include "MessageOut.hh"
#include "BLIPIO.hh"
#include "Codec.hh"
#include "StringUtils.hh"
#include <iostream>
#include <sstream>

namespace crouton::blip {
    using namespace std;

    const char* const kMessageTypeNames[8] = {
        "REQ", "RES", "ERR", "?3?", "ACKREQ", "AKRES", "?6?", "?7?"};


    std::ostream& operator<< (std::ostream& out, MessageNo n) {
        return out << '#' << uint64_t(n);
    }

    
    // Writes a ConstBytes to a stream. If it contains non-ASCII characters, it will be written as hex
    // inside "<<...>>". If empty, it's written as "<<>>".
    static ostream& dumpSlice(ostream& o, ConstBytes s) {
        if (s.size() == 0) 
            return o << "<<>>";
        auto buf = (const uint8_t*)s.data();
        for (size_t i = 0; i < s.size(); i++) {
            if (buf[i] < 32 || buf[i] > 126) 
                return o << "<<" << hexString(s) << ">>";
        }
        return o << string_view(s);
    }


#pragma mark - MESSAGE:


    void Message::dumpHeader(std::ostream& out) const {
        out << kMessageTypeNames[type()] << _number << ' ';
        if (_flags & kUrgent) out << 'U';
        if (_flags & kNoReply) out << 'N';
        if (_flags & kCompressed) out << 'Z';
    }

    
    void Message::writeDescription(ConstBytes payload, std::ostream& out) const {
        if (type() == kRequestType) {
            const char* profile = findProperty(payload, "Profile");
            if (profile) 
                out << "'" << profile << "' ";
        }
        dumpHeader(out);
    }


    void Message::dump(ConstBytes payload, ConstBytes body, bool withBody, std::ostream& out) {
        dumpHeader(out);
        if (type() != kAckRequestType && type() != kAckResponseType) {
            out << " {";
            auto key = (const char*)payload.data();
            auto end = key + payload.size_bytes();
            while (key < end) {
                auto endOfKey = key + strlen(key);
                auto val      = endOfKey + 1;
                if (val >= end)
                    break;  // illegal: missing value
                auto endOfVal = val + strlen(val);

                out << "\n\t";
                dumpSlice(out, ConstBytes{key, endOfKey});
                out << ": ";
                dumpSlice(out, {val, endOfVal});
                key = endOfVal + 1;
            }
            if (withBody) {
                out << "\n\tBODY: ";
                dumpSlice(out, body);
            } else {
                out << "\n\tBODY: " << body.size() << " bytes";
            }
            out << " }";
        }
        out << endl;
    }


    const char* Message::findProperty(ConstBytes payload, const char* propertyName) {
        auto key = (const char*)payload.data();
        auto end = key + payload.size_bytes();
        while (key < end) {
            auto endOfKey = key + strlen(key);
            auto val      = endOfKey + 1;
            if (val >= end) 
                break;  // illegal: missing value
            if (0 == strcmp(key, propertyName)) 
                return val;
            key = val + strlen(val) + 1;
        }
        return nullptr;
    }


#pragma mark - MESSAGE IN:


    MessageIn::~MessageIn() = default;


    MessageIn::MessageIn(BLIPIO* connection, FrameFlags flags, MessageNo n,
                         MessageSize outgoingSize,
                         FutureProvider<std::shared_ptr<MessageIn>> onResponse)
    : Message(flags, n)
    ,_connection(connection)
    ,_outgoingSize(outgoingSize)
    ,_onResponse(std::move(onResponse))
    { }


    MessageIn::ReceiveState MessageIn::receivedFrame(Codec& codec,
                                                     ConstBytes entireFrame,
                                                     FrameFlags frameFlags)
    {
        ReceiveState state = kOther;

        // Update byte count and send acknowledgement packet when appropriate:
        _rawBytesReceived += entireFrame.size();
        acknowledge((uint32_t)entireFrame.size());

        auto mode = (frameFlags & kCompressed) ? Codec::Mode::SyncFlush : Codec::Mode::Raw;

        // Get and remove the checksum from the end of the frame:
        ConstBytes frame = entireFrame;
        auto checksum = entireFrame.last(Codec::kChecksumSize);
        if (mode == Codec::Mode::SyncFlush) {
            // Replace checksum with the untransmitted deflate empty-block trailer,
            // which is conveniently the same size:
            static_assert(Codec::kChecksumSize == 4, "Checksum not same size as deflate trailer");
            memcpy(const_cast<byte*>(checksum.data()), "\x00\x00\xFF\xFF", 4);
        } else {
            // In uncompressed message, just trim off the checksum:
            frame = frame.without_last(Codec::kChecksumSize);
        }

        if (!_gotProperties) {
            // Read a few bytes, enough to decode the properties' size:
            char buf[10];
            MutableBytes out(buf, sizeof(buf));
            ConstBytes dst = codec.write(frame, out, mode);
            _propertiesSize = readUVarint(dst);
            if (_propertiesSize > kMaxPropertiesSize)
                crouton::Error::raise(BLIPError::PropertiesTooLarge);
            _properties.reserve(_propertiesSize);
            // Copy any properties into _properties, and any body after that into _body:
            _properties.append(string_view(dst.read(_propertiesSize)));
            _body.append(string_view(dst));
            _gotProperties = true;
        }
        if (size_t curSize = _properties.size(); curSize < _propertiesSize) {
            // Read from the frame into _properties:
            _properties.resize(_propertiesSize);
            MutableBytes out(&_properties[curSize], _propertiesSize - curSize);
            codec.write(frame, out, mode);
            _properties.resize((char*)out.data() - _properties.data());
            if (_properties.size() == _propertiesSize) {
                // Finished the properties:
                state = kBeginning;
                if (_propertiesSize > 0 && _properties[_propertiesSize - 1] != 0)
                    crouton::Error::raise(BLIPError::InvalidFrame, "message properties not null-terminated");
            }
        } else {
            state = kBeginning;
        }

        if (!frame.empty()) {
            // Add remaining data to the body:
            uint8_t buffer[4096];
            while (frame.size() > 0) {
                MutableBytes output(buffer, sizeof(buffer));
                MutableBytes written = codec.write(frame, output, Codec::Mode(mode));
                _body.append(string_view(written));
            }
        }

        if (!(frameFlags & kMoreComing)) {
            // Completed!
            if (state < kBeginning)
                crouton::Error::raise(BLIPError::InvalidFrame, "message ends before end of properties");
            _complete = true;
            state = kEnd;
            LBLIP->info("Finished receiving {}", *this);
            if (_onResponse) {
                auto ref = dynamic_pointer_cast<MessageIn>(this->shared_from_this());
                _onResponse->setResult(ref);
            }
        }
        return state;
    }


    void MessageIn::acknowledge(uint32_t frameSize) {
        _unackedBytes += frameSize;
        if (_unackedBytes >= kIncomingAckThreshold) {
            // Send an ACK after enough data has been received of this message:
            MessageType msgType = isResponse() ? kAckResponseType : kAckRequestType;
            char buf[10];
            string payload(buf, putUVarint(_rawBytesReceived, buf));
            _connection->send(make_shared<MessageOut>(_connection,
                                                      (FrameFlags)(msgType|kUrgent|kNoReply),
                                                      payload, _number));
            _unackedBytes = 0;
        }
    }


    void MessageIn::disconnected() {
        Message::disconnected();
        if (_onResponse)
            _onResponse->setResult(nullptr);
    }


#pragma mark - PROPERTIES:


    string_view MessageIn::property(string_view property) const {
        // Note: using strlen here is safe. It can't fall off the end of _properties, because the
        // receivedFrame() method has already verified that _properties ends with a zero byte.
        // OPT: This lookup isn't very efficient. If it turns out to be a hot-spot, we could cache
        // the starting point of every property string.
        auto key = (const char*)_properties.data();
        auto end = key + _properties.size();
        while (key < end) {
            auto endOfKey = key + strlen(key);
            auto val      = endOfKey + 1;
            if (val >= end)
                break;  // illegal: missing value
            auto endOfVal = val + strlen(val);
            if (property == string_view(key, endOfKey)) 
                return {val, endOfVal};
            key = endOfVal + 1;
        }
        return string_view{};
    }


    long MessageIn::intProperty(string_view name, long defaultValue) const {
        string value(property(name));
        if (value.empty())
            return defaultValue;
        char* end;
        long result = strtol(value.c_str(), &end, 10);
        if (*end != '\0')
            return defaultValue;
        return result;
    }


    bool MessageIn::boolProperty(string_view name, bool defaultValue) const {
        string_view value = property(name);
        if (equalIgnoringCase(value, "true") || equalIgnoringCase(value, "YES"))
            return true;
        else if (equalIgnoringCase(value, "false") || equalIgnoringCase(value, "NO"))
            return false;
        else
            return intProperty(name, defaultValue) != 0;
    }


    Message::Error MessageIn::getError() const {
        if (!isError()) 
            return {};
        return {string(property("Error-Domain")), (int)intProperty("Error-Code"), body()};
    }


    string MessageIn::description() {
        stringstream s;
        writeDescription(ConstBytes(_properties), s);
        return s.str();
    }


#pragma mark - RESPONSES:


    bool MessageIn::canRespond() const {
        return !noReply() && _connection->isSendOpen();
    }


    void MessageIn::respond(MessageBuilder& mb) {
        if (noReply()) {
            LBLIP->warn("Ignoring attempt to respond to a noReply message");
            return;
        }
        assert(!_responded);
        _responded = true;
        if (mb.type == kRequestType)
            mb.type = kResponseType;
        _connection->send(make_shared<MessageOut>(_connection, mb, _number));
    }


    void MessageIn::respondWithError(Error err) {
        if (!noReply()) {
            MessageBuilder mb(this);
            mb.makeError(err);
            respond(mb);
        }
    }


    void MessageIn::respond() {
        if (!noReply()) {
            MessageBuilder reply(this);
            respond(reply);
        }
    }


    void MessageIn::notHandled() {
        respondWithError({"BLIP", 404, "no handler for message"});
    }


}
