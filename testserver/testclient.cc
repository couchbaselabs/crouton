//
// testclient.cc
// Crouton
// Copyright 2023-Present Couchbase, Inc.
//

#include "Crouton.hh"
#include <iostream>

using namespace std;
using namespace crouton;

#define CRLF  "\r\n"


static optional<string> firstArg() {
    optional<string> arg;
    if (UVArgs.size() >= 1)
        arg = UVArgs[1];
    return arg;
}

static optional<string> popArg() {
    optional<string> arg;
    if (UVArgs.size() >= 1) {
        arg = std::move(UVArgs[1]);
        UVArgs.erase(UVArgs.begin() + 1);
    }
    return arg;
}

static optional<string> popFlag() {
    if (auto flag = firstArg(); flag && flag->starts_with("-")) {
        popArg();
        return flag;
    } else {
        return nullopt;
    }
}


static Future<int> run() {
    bool includeHeaders = false;
    bool verbose = false;
    while (auto flag = popFlag()) {
        if (flag == "-i")
            includeHeaders = true;
        else if (flag == "-v")
            verbose = true;
        else {
            cerr << "Unknown flag " << *flag << endl;
            RETURN 1;
        }
    }

    auto url = popArg();
    if (!url) {
        std::cerr << "Missing URL";
        RETURN 1;
    }
    HTTPClient client{url.value()};
    HTTPRequest req(client, "GET", "/");
    HTTPResponse resp = AWAIT req.response();

    bool ok = (resp.status == HTTPStatus::OK);
    if (!ok) {
        cout << "*** " << int(resp.status) << " " << resp.statusMessage << " ***" << endl;
    }

    if (includeHeaders || verbose) {
        auto headers = resp.headers();
        while (auto header = AWAIT headers) {
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
    return UVMain(argc, argv, run);
}
