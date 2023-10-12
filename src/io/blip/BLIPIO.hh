//
// BLIPIO.hh
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
#include "Codec.hh"
#include "Future.hh"
#include "Generator.hh"
#include "Queue.hh"
#include "Message.hh"
#include "MessageOut.hh"
#include "Queue.hh"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace crouton::io::blip {
    class MessageBuilder;

    class BLIPIO {
    public:
        BLIPIO();

        /// Queues a request to be sent.
        /// The result resolves to the reply message when it arrives.
        /// If this message is NoReply, the result resolves to `nullptr` when it's sent.
        ASYNC<MessageInRef> sendRequest(MessageBuilder&);

        /// Passes a received BLIP frame to be parsed, possibly resulting in a finished message.
        /// If the message is a response, it becomes the resolved value of the Future returned from
        /// the `sendRequest` call.
        /// If the message is a request, it's returned from this call.
        /// @returns  A completed incoming request, or nullptr.
        MessageInRef receive(ConstBytes frame);

        /// A Generator that yields BLIP frames that should be sent to the destination,
        /// i.e. as binary WebSocket messages.
        Generator<string>& output()     {return  _frameGenerator;}

        /// True if there is work for the Generator to do.
        bool hasOutput() const          {return !_outbox.empty() || !_wayOutBox.empty() || !_icebox.empty();}

        /// True if requests/responses can be sent (neither `closeWrite` nor `stop` called.)
        bool isSendOpen() const         {return _sendOpen;}

        /// True if messages will still be received (neither `closeRead` nor `stop` called.)
        bool isReceiveOpen() const      {return _receiveOpen;}

        /// Tells BLIPIO that no new requests or responses will be sent (no more calls to `send`.)
        /// The Generator (`output()`) will yield all remaining frames of already-queued messages,
        /// then end.
        void closeSend();

        /// Tells BLIPIO that no more frames will be received, i.e. `receive` won't be called again.
        /// Any partially-complete incoming requests will be discarded.
        /// Any pending responses (`Future<MessageIn>`) will immediately resolve to Error messages.
        void closeReceive();

        /// Tells BLIPIO that the connection is closed and no more messages can be sent or
        /// received. This is like calling both `closeSend` and `closeReceive`, plus it discards all
        /// outgoing messages currently in the queue.
        ///
        /// This should only be used if the transport has abruptly failed.
        void stop();

        ~BLIPIO();

    protected:
        friend class MessageIn;
        bool send(MessageOutRef);

    private:
        void _closeRead();
        bool _queueMessage(MessageOutRef);
        void freezeMessage(MessageOutRef);
        void thawMessage(MessageOutRef);
        Generator<string> frameGenerator();
        ConstBytes createNextFrame(MessageOutRef, uint8_t*);

        MessageInRef pendingRequest(MessageNo, FrameFlags);
        MessageInRef pendingResponse(MessageNo, FrameFlags);
        void receivedAck(MessageNo, bool isResponse, ConstBytes);

        using MessageMap = std::unordered_map<MessageNo, MessageInRef>;

        /** Queue of outgoing messages; each message gets to send one frame in turn. */
        class Outbox : public AsyncQueue<MessageOutRef> {
        public:
            [[nodiscard]] MessageOutRef findMessage(MessageNo msgNo, bool isResponse) const;
            void requeue(MessageOutRef);
            bool urgent() const;
        };

        Deflater                    _outputCodec;       // Compressor for outgoing frames
        Inflater                    _inputCodec;        // Decompressor for incoming frames
        Outbox                      _outbox;            // Round-robin queue of msgs being sent
        Outbox                      _wayOutBox;         // Messages waiting to be sent
        std::vector<MessageOutRef>  _icebox;            // Outgoing msgs on hold awaiting ACK
        MessageMap                  _pendingRequests;   // Unfinished incoming requests
        MessageMap                  _pendingResponses;  // Unfinished incoming responses
        MessageNo                   _lastMessageNo {0}; // Last msg# generated
        MessageNo                   _numRequestsReceived {0}; // Max msg# received
        Generator<string>           _frameGenerator;    // The Generator side of `output()`.
        bool                        _sendOpen = true;   // True until closeSend or stop called
        bool                        _receiveOpen = true;// True until closeReceive or stop called

        // These are just for statistics/metrics:
        size_t          _maxOutboxDepth{0}, _totalOutboxDepth{0}, _countOutboxDepth{0};
        uint64_t        _totalBytesWritten{0}, _totalBytesRead{0};
    };

}
