//
// BLIPConnection.hh
//
// Copyright 2023-Present Couchbase, Inc. All rights reserved.
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

#pragma once
#include "BLIPIO.hh"
#include "io/WebSocket.hh"
#include "CoCondition.hh"
#include "Message.hh"
#include "Task.hh"

#include <functional>
#include <optional>
#include <unordered_map>  

namespace crouton::io::blip {
    class MessageBuilder;


    /** A BLIP WebSocket connection. Glues a `BLIPIO` to a `WebSocket`.
        You should first create and connect the ClientWebSocket or ServerWebSocket,
        then pass it to the BLIPConnection constructor, then call `start`. */
    class BLIPConnection {
    public:
        using RequestHandler = std::function<void(MessageInRef)>;
        using RequestHandlerItem = std::pair<const string,RequestHandler>;

        /// Constructs a BLIPConnection and registers any given request handlers.
        explicit BLIPConnection(std::unique_ptr<ws::WebSocket> ws,
                                std::initializer_list<RequestHandlerItem> = {});
        ~BLIPConnection();

        /// Registers a handler for incoming requests with a specific `Profile` property value.
        /// The profile string `"*"` is a wild-card that matches any message.
        void setRequestHandler(string profile, RequestHandler);

        /// Begins listening for incoming messages and sending outgoing ones.
        /// You should register your request handlers before calling this.
        void start();

        /// Queues a request to be sent over the WebSocket.
        /// The result resolves to the reply message when it arrives.
        /// If this message is NoReply, the result resolves to `nullptr` when it's sent.
        ASYNC<MessageInRef> sendRequest(MessageBuilder&);

        /// Initiates the close protocol:
        /// 1. Sends all currently queued messages (but no more can be sent)
        /// 2. Sends a WebSocket CLOSE frame with the given code/message
        /// 3. Processes all remaining incoming frames/messages from the peer
        /// 4. When peer's WebSocket CLOSE frame is received, closes the socket.
        ///
        /// If `immediate` is true, step 1 is skipped.
        ASYNC<void> close(ws::CloseCode = ws::CloseCode::Normal,
                          string message = "",
                          bool immediate = false);

    private:
        Task outputTask();
        Task inputTask();
        void dispatchRequest(MessageInRef);

        using HandlerMap = std::unordered_map<string,RequestHandler>;

        BLIPIO                          _io;
        std::unique_ptr<ws::WebSocket>  _socket;
        HandlerMap                      _handlers;
        std::optional<Task>             _outputTask, _inputTask;
        Blocker<void>                   _outputDone, _inputDone;
    };

}
