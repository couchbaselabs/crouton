//
// BLIPConnection.cc
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

#include "BLIPConnection.hh"
#include "io/WebSocket.hh"
#include "Task.hh"

namespace crouton::io::blip {
    using namespace std;


    BLIPConnection::BLIPConnection(std::unique_ptr<ws::WebSocket> ws,
                                   std::initializer_list<RequestHandlerItem> handlers)
    :_socket(std::move(ws))
    ,_handlers(handlers)
    { }

    BLIPConnection::~BLIPConnection() = default;


    void BLIPConnection::setRequestHandler(string profile, RequestHandler handler) {
        _handlers[profile] = std::move(handler);
    }


    void BLIPConnection::start() {
        LBLIP->info("BLIPConnection starting");
        _outputTask.emplace(outputTask());
        _inputTask.emplace(inputTask());
    }


    Task BLIPConnection::outputTask() {
        do {
            Result<string> frame = AWAIT _io.output();
            if (!frame)
                break; // BLIPIO's send side has closed.
            AWAIT _socket->send(*frame, ws::Message::Binary);
        } while (YIELD true);
        _outputDone.notify();
    }


    Task BLIPConnection::inputTask() {
        do {
            Result<ws::Message> frame = AWAIT _socket->receive();
            if (!frame || frame->type == ws::Message::Close) {
                LBLIP->info("BLIPConnection received WebSocket CLOSE");
                break;
            }
            MessageInRef msg = _io.receive(*frame);
            if (msg)
                dispatchRequest(std::move(msg));
        } while (YIELD true);
        _io.closeReceive();
        _inputDone.notify();
    }


    ASYNC<MessageInRef> BLIPConnection::sendRequest(MessageBuilder& msg) {
        return _io.sendRequest(msg);
    }


    void BLIPConnection::dispatchRequest(MessageInRef msg) {
        string profile(msg->property("Profile"));
        auto i = _handlers.find(profile);
        if (i == _handlers.end())
            i = _handlers.find("*");
        if (i == _handlers.end()) {
            msg->notHandled();
            return;
        }

        string exceptionMessage;
        try {
            i->second(std::move(msg));
            return;
        } catch (std::exception const& x) {
            exceptionMessage = x.what();
        } catch(...) {
        }
        LBLIP->error(fmt::format("Unexpected exception `{}` handling BLIP request {}",
                                 exceptionMessage, *msg));
        msg->respondWithError({"BLIP", 500, "Internal error handling message"});
    }


    Future<void> BLIPConnection::close(ws::CloseCode code, string message, bool immediate) {
        LBLIP->info("BLIPConnection closing with code {} \"{}\"", int(code), message);
        if (immediate)
            _io.stop();
        else
            _io.closeSend();
        AWAIT _outputDone;
        LBLIP->debug("BLIPConnection now sending WebSocket CLOSE...");
        AWAIT _socket->send(ws::Message{code, message});
        AWAIT _inputDone;
        AWAIT _socket->close();
        RETURN noerror;
    }

}
