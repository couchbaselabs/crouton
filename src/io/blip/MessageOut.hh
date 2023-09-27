//
// BLIPMessageOut.hh
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
#include "Message.hh"
#include "MessageBuilder.hh"
#include "Future.hh"
#include <ostream>

namespace crouton::io::blip {
    class BLIPIO;
    class Codec;

    /** An outgoing message that's been constructed by a MessageBuilder. */
    class MessageOut : public Message {
      public:
        friend class MessageIn;
        friend class Connection;
        friend class BLIPIO;

        MessageOut(BLIPIO* connection, FrameFlags flags, string payload, MessageNo number);

        MessageOut(BLIPIO* connection, MessageBuilder& builder, MessageNo number)
        : MessageOut(connection, (FrameFlags)0, builder.finish(), number)
        {
            _flags = builder.flags();  // finish() may update the flags, so set them after
        }

        friend std::ostream& operator<< (std::ostream &out, MessageOut const& msg) {
            msg.writeDescription(ConstBytes(msg.getPropsAndBody().first), out);
            return out;
        }

        bool isNew() const      {return _bytesSent == 0;}

    protected:
        void dontCompress() { _flags = (FrameFlags)(_flags & ~kCompressed); }

        void nextFrameToSend(Codec& codec, MutableBytes& dst, FrameFlags& outFlags);
        void receivedAck(uint32_t byteCount);

        bool needsAck() const { return _unackedBytes >= kMaxUnackedBytes; }

        MessageIn* createResponse();
        void       disconnected() override;
        ASYNC<MessageInRef> onResponse();
        void noResponse();

        // for debugging/logging:
        std::string description();
        void        dump(std::ostream& out, bool withBody);
        const char* findProperty(const char* propertyName);

    private:
        std::pair<ConstBytes, ConstBytes> getPropsAndBody() const;

        static const uint32_t kMaxUnackedBytes = 128000;

        /** Manages the data (properties, body, data source) of a MessageOut. */
        class Contents {
          public:
            Contents(string payload);
            ConstBytes&                        dataToSend();
            [[nodiscard]] bool                    hasMoreDataToSend() const;
            [[nodiscard]] std::pair<ConstBytes, ConstBytes> getPropsAndBody() const;

            [[nodiscard]] string_view body() const { return _payload; }

          private:
            void readFromDataSource();

            string       _payload;           // Message data (uncompressed)
            ConstBytes     _unsentPayload;     // Unsent subrange of _payload
        };

        BLIPIO* const _connection;                // My BLIP connection
        Contents      _contents;                  // Message data
        uint32_t      _uncompressedBytesSent{0};  // Number of bytes of the data sent so far
        uint32_t      _bytesSent{0};              // Number of bytes transmitted (after compression)
        uint32_t      _unackedBytes{0};           // Bytes transmitted for which no ack received yet
        FutureProvider<MessageInRef> _onResponse;
    };

    using MessageOutRef = std::shared_ptr<MessageOut>;

} 
