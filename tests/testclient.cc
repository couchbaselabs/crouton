//
// testclient.cc
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


static Future<int> run() {
    // Read flags:
    bool includeHeaders = false;
    bool verbose = false;
    while (auto flag = MainArgs.popFlag()) {
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
    auto url = MainArgs.popFirst();
    if (!url) {
        std::cerr << "Missing URL";
        RETURN 1;
    }

    // Send HTTP request:
    HTTPClient client{string(url.value())};
    HTTPRequest req(client, "GET", "/");
    HTTPResponse resp = AWAIT req.response();

    // Display result:
    bool ok = (resp.status == HTTPStatus::OK);
    if (!ok) {
        cout << "*** " << int(resp.status) << " " << resp.statusMessage << " ***" << endl;
    }

    if (includeHeaders || verbose) {
        auto headers = resp.headers();
        optional<pair<string_view,string_view>> header;
        while ((header = (AWAIT headers))) {
            cout << header->first << " = " << header->second << endl;
        }
        cout << endl;
    }

    if (ok || verbose) {
        string body;
        do {
            body = AWAIT resp.readBody();
            cout << body;
        } while (!body.empty());
        cout << endl;
    }

    RETURN ok ? 0 : 1;
}


int main(int argc, const char * argv[]) {
    return Main(argc, argv, run);
}
