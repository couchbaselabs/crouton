//
// BLIPMessageOut.cc
//
// Copyright 2017-Present Couchbase, Inc.
//
// Use of this software is governed by the Business Source License included
// in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
// in that file, in accordance with the Business Source License, use of this
// software will be governed by the Apache License, Version 2.0, included in
// the file licenses/APL2.txt.
//

#include "MessageOut.hh"
#include "BLIPIO.hh"
#include "Codec.hh"

namespace crouton::io::blip {
    using namespace std;

    MessageOut::MessageOut(BLIPIO* connection, FrameFlags flags, string payload,
                           MessageNo number)
    : Message(flags, number)
    ,_connection(connection)
    ,_contents(payload)
    {}


    void MessageOut::nextFrameToSend(Codec& codec, MutableBytes& dst, FrameFlags& outFlags) {
        outFlags = flags();
        if (isAck()) {
            // Acks have no checksum and don't go through the codec
            ConstBytes& data = _contents.dataToSend();
            _bytesSent += (uint32_t)dst.write(data);
            return;
        }

        // Write the frame:
        size_t frameSize = dst.size();
        {
            // `frame` is the same as `dst` but 4 bytes shorter, to leave space for the checksum
            MutableBytes frame(dst.data(), frameSize - Codec::kChecksumSize);
            auto mode = hasFlag(kCompressed) ? Codec::Mode::SyncFlush : Codec::Mode::Raw;
            do {
                ConstBytes& data = _contents.dataToSend();
                if (data.size() == 0) 
                    break;
                _uncompressedBytesSent += (uint32_t)data.size();
                codec.write(data, frame, mode);
                _uncompressedBytesSent -= (uint32_t)data.size();
            } while (frame.size() >= 1024);

            if (codec.unflushedBytes() > 0)
                crouton::Error::raise(BLIPError::CompressionError, "Compression buffer overflow");

            if (mode == Codec::Mode::SyncFlush) {
                size_t bytesWritten = (frameSize - Codec::kChecksumSize) - frame.size();
                if (bytesWritten > 0) {
                    // SyncFlush always ends the output with the 4 bytes 00 00 FF FF.
                    // We can remove those, then add them when reading the data back in.
                    assert(bytesWritten >= 4 
                           && memcmp((const char*)frame.data() - 4, "\x00\x00\xFF\xFF", 4) == 0);
                    frame = MutableBytes(frame.data() - 4, frame.endByte());
                }
            }

            // Write the checksum:
            dst = MutableBytes(frame.data(), dst.endByte());  // Catch `dst` up to where `frame` is
            codec.writeChecksum(dst);
        }

        // Compute the (compressed) frame size, and update running totals:
        frameSize -= dst.size();
        _bytesSent += (uint32_t)frameSize;
        _unackedBytes += (uint32_t)frameSize;

        // Update flags & state:
        __unused MessageProgress::State state;
        if (_contents.hasMoreDataToSend()) {
            outFlags = (FrameFlags)(outFlags | kMoreComing);
            state = MessageProgress::kSending;
        } else if (noReply()) {
            state = MessageProgress::kComplete;
        } else {
            state = MessageProgress::kAwaitingReply;
        }
        //sendProgress(state, _uncompressedBytesSent, 0, nullptr);
    }


    void MessageOut::receivedAck(uint32_t byteCount) {
        if (byteCount <= _bytesSent)
            _unackedBytes = min(_unackedBytes, (uint32_t)(_bytesSent - byteCount));
    }


    MessageIn* MessageOut::createResponse() {
        if (type() != kRequestType || noReply()) 
            return nullptr;
        // Note: The MessageIn's flags will be updated when the 1st frame of the response arrives;
        // the type might become kErrorType, and kUrgent or kCompressed might be set.
        return new MessageIn(_connection,
                             FrameFlags(kResponseType),
                             _number,
                             _uncompressedBytesSent,
                             std::move(_onResponse));
    }


    ASYNC<MessageInRef> MessageOut::onResponse() {
        assert(!_onResponse);
        _onResponse = std::make_shared<FutureState<MessageInRef>>();
        return Future<MessageInRef>(_onResponse);
    }


    void MessageOut::noResponse() {
        if (auto d = std::move(_onResponse))
            d->setResult(MessageInRef(nullptr));
    }


    void MessageOut::disconnected() {
        noResponse();
        if (type() != kRequestType || noReply())
            return;
        Message::disconnected();
    }


    void MessageOut::dump(std::ostream& out, bool withBody) {
        auto [props, body] = getPropsAndBody();
        Message::dump(props, body, withBody, out);
    }


    const char* MessageOut::findProperty(const char* propertyName) {
        ConstBytes props = getPropsAndBody().first;
        return Message::findProperty(props, propertyName);
    }


    string MessageOut::description() {
        stringstream s;
        ConstBytes props = getPropsAndBody().first;
        writeDescription(props, s);
        return s.str();
    }


#pragma mark - DATA:


    pair<ConstBytes, ConstBytes> MessageOut::getPropsAndBody() const {
        if (type() < kAckRequestType) 
            return _contents.getPropsAndBody();
        else
            return {{}, _contents.body()};  // (ACKs do not have properties)
    }


    MessageOut::Contents::Contents(string payload)
    : _payload(std::move(payload))
    ,_unsentPayload(_payload)
    {
        assert(_payload.size() <= UINT32_MAX);
    }


    // Returns the next message-body data to send (as a ConstBytes _reference_)
    ConstBytes& MessageOut::Contents::dataToSend() {
        return _unsentPayload;
    }


    // Is there more data to send?
    bool MessageOut::Contents::hasMoreDataToSend() const {
        return _unsentPayload.size() > 0;
    }
    

    pair<ConstBytes, ConstBytes> MessageOut::Contents::getPropsAndBody() const {
        if (_payload.empty())
            return {};
        ConstBytes in(_payload);
        // This assumes the message starts with properties, which start with a UVarInt32.
        auto propertiesSize = readUVarint(in);
        if (propertiesSize > in.size())
            crouton::Error::raise(BLIPError::InvalidFrame, "Invalid properties size in BLIP frame");
        return {in.first(propertiesSize), in.without_first(propertiesSize)};
    }

}
