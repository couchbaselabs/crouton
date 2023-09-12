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
#include "BLIPProtocol.hh"
#include "Bytes.hh"
#include "Future.hh"

namespace crouton::blip {
    class BLIPIO;
    class Codec;
    class Message;
    class MessageBuilder;
    class MessageIn;

    using MessageRef = std::shared_ptr<Message>;
    using MessageInRef = std::shared_ptr<MessageIn>;


    struct Error {
        const string   domain;
        const int      code = 0;
        const string   message;
    };


    /** Abstract base class of messages */
    class Message : public std::enable_shared_from_this<Message> {
      public:
        virtual ~Message() = default;

        bool isResponse() const { return type() >= kResponseType; }
        bool isError() const { return type() == kErrorType; }
        bool urgent() const { return hasFlag(kUrgent); }
        bool noReply() const { return hasFlag(kNoReply); }
        
        MessageNo number() const { return _number; }

        MessageType type() const { return (MessageType)(_flags & kTypeMask); }

        // exposed for testing:
        FrameFlags flags() const { return _flags; }

      protected:

        Message(FrameFlags f, MessageNo n) 
        :_flags(FrameFlags(f & ~kMoreComing))
        ,_number(n) {
            /*Log("NEW Message<%p, %s #%llu>", this, typeName(), _number);*/
        }
        bool hasFlag(FrameFlags f) const { return (_flags & f) != 0; }
        bool isAck() const { return type() == kAckRequestType || type() == kAckResponseType; }
        virtual bool isIncoming() const { return false; }
        const char* typeName() const { return kMessageTypeNames[type()]; }
        virtual void disconnected() { }

        void dump(ConstBytes payload, ConstBytes body, bool withBody, std::ostream&);
        void dumpHeader(std::ostream&) const;
        void writeDescription(ConstBytes payload, std::ostream&) const;
        static const char* findProperty(ConstBytes payload, const char* propertyName);

        FrameFlags              _flags;
        MessageNo               _number;
    };


    /** An incoming message. */
    class MessageIn : public Message {
      public:
        /** Gets a property value */
        string_view property(string_view property) const;
        long  intProperty(string_view property, long defaultValue = 0) const;
        bool  boolProperty(string_view property, bool defaultValue = false) const;

        /** Returns information about an error (if this message is an error.) */
        Error getError() const;

        /** Returns true if the message has been completely received including the body. */
        bool isComplete() const             {return _complete;}

        /** The body of the message. */
        string const& body() const     {return _body;}

        /** Returns the body, removing it from the message. The next call to extractBody() or
            body() will return only the data that's been read since this call. */
        string extractBody()           {return std::move(_body);}

        /// True if (a) this message is not NoReply, and (b) the BLIP connection is not closing.
        bool canRespond() const;

        /** Sends a response. (The message must be complete.) */
        void respond(MessageBuilder&);

        /** Sends an empty default response, unless the request was sent noreply.
            (The message must be complete.) */
        void respond();

        /** Sends an error as a response. (The message must be complete.)
            This is a no-op if the message was sent NoReply. */
        void respondWithError(Error);

        /** Responds with an error saying that the message went unhandled.
            Call this if you don't know what to do with a request.
            (The message must be complete.)
            This is a no-op if the message was sent NoReply. */
        void notHandled();

        void dump(std::ostream& out, bool withBody) {
            Message::dump(ConstBytes(_properties), ConstBytes(_body), withBody, out);
        }

        MessageIn(BLIPIO*, FrameFlags, MessageNo, MessageSize outgoingSize = 0,
                  FutureProvider<std::shared_ptr<MessageIn>> onResponse = nullptr);
        ~MessageIn() override;

        friend std::ostream& operator<< (std::ostream &out, MessageIn const& msg) {
            msg.writeDescription(ConstBytes(msg._properties), out);
            return out;
        }

        string description();

    protected:
        friend class MessageOut;
        friend class BLIPIO;

        enum ReceiveState { kOther, kBeginning, kEnd };

        bool isIncoming() const override { return true; }

        ReceiveState receivedFrame(Codec&, ConstBytes frame, FrameFlags);
        virtual void disconnected() override;

    private:
        void acknowledge(uint32_t frameSize);

        BLIPIO*                         _connection = nullptr;  // The owning BLIP connection
        MessageSize                     _rawBytesReceived{0};
        string                          _in;                   // Accumulates body data
        size_t                          _propertiesSize{0};    // Length of properties in bytes
        uint32_t                        _unackedBytes{0};      // # unACKed bytes received
        string                          _properties;           // Just the (still encoded) properties
        string                          _body;                 // Just the body
        string                          _bodyAsFleece;         // Body re-encoded into Fleece [lazy]
        const MessageSize               _outgoingSize{0};
        FutureProvider<MessageInRef>    _onResponse;
        bool                            _gotProperties{false};
        bool                            _complete{false};
        bool                            _responded{false};
    };



    /** Progress notification for an outgoing request. */
    struct MessageProgress {
        enum State {
            kQueued,          // Outgoing request has been queued for delivery
            kSending,         // First bytes of message have been sent
            kAwaitingReply,   // Message sent; waiting for a reply (unless noreply)
            kReceivingReply,  // Reply is being received
            kComplete,        // Delivery (and receipt, if not noreply) complete.
            kDisconnected     // Socket disconnected before delivery or receipt completed
        };
        
        State               state;
        MessageSize         bytesSent;
        MessageSize         bytesReceived;
        MessageInRef        reply;
    };

}
