//
// demo_client.cc
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
#include <iostream>

using namespace std;
using namespace crouton;


staticASYNC<int> run() {
    // Read flags:
    auto args = MainArgs();
    bool includeHeaders = false;
    bool verbose = false;
    while (auto flag = args.popFlag()) {
        if (flag == "-i")
            includeHeaders = true;
        else if (flag == "-v")
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
    HTTPConnection client{string(url.value())};
    HTTPRequest req;
    HTTPResponse resp = AWAIT client.send(req);

    // Display result:
    bool ok = (resp.status() == HTTPStatus::OK);
    if (!ok) {
        cout << "*** " << int(resp.status()) << " " << resp.statusMessage() << " ***" << endl;
    }

    if (includeHeaders || verbose) {
        for (auto &header : resp.headers()) {
            cout << header.first << ": " << header.second << endl;
        }
        cout << endl;
    }

    if (ok || verbose) {
        ConstBytes data;
        do {
            data = AWAIT resp.readNoCopy();
            cout << string_view((char*)data.data(), data.size());
        } while (data.size() > 0);
        cout << endl;
    }

    RETURN ok ? 0 : 1;
}


CROUTON_MAIN(run)
