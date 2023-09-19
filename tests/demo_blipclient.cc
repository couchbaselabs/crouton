//
// demo_blipclient.cc
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

#include "Crouton.hh"
#include "BLIP.hh"
#include <iostream>
#include <vector>

using namespace std;
using namespace crouton;


static Future<int> run() {
    // Read flags:
    auto args = MainArgs();
    string protocol;
    __unused bool verbose = false;
    while (auto flag = args.popFlag()) {
        if (flag == "--protocol") {
            if (!protocol.empty()) protocol += ",";
            protocol += args.popFirst().value();
        } else if (flag == "-v")
            verbose = true;
        else {
            cerr << "Unknown flag " << *flag << endl;
            RETURN 1;
        }
    }

    // Read URL argument:
    auto url = args.popFirst();
    if (!url) {
        std::cerr << "Missing URL";
        RETURN 1;
    }

    // Send HTTP request:
    auto ws = make_unique<ws::ClientWebSocket>(string(*url));
    if (!protocol.empty())
        ws->setHeader("Sec-WebSocket-Protocol", protocol.c_str());
    AWAIT ws->connect();

    CoCondition gotChanges;

    blip::BLIPConnection blip(std::move(ws), {
        {"changes", [&](blip::MessageInRef msg) {
            cout << "*** demo_blipclient received ";
            msg->dump(cout, false);
            if (msg->canRespond()) {
                blip::MessageBuilder response;
                response << "[]";
                msg->respond(response);
            }
            gotChanges.notifyAll();
        } }
    });
    blip.start();

    blip::MessageBuilder msg("subChanges");
    blip::MessageInRef reply = AWAIT blip.sendRequest(msg);

    cout << "*** demo_blipclient got reply to its `subChanges`: ";
    reply->dump(cout, true);

    AWAIT gotChanges;

    cout << "Closing...\n";
    AWAIT blip.close();
    RETURN 0;
}


CROUTON_MAIN(run)
