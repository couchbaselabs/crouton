//
// test_blip.cc
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

#include "tests.hh"
#include "BLIPIO.hh"
#include "StringUtils.hh"

using namespace std;
using namespace crouton::blip;

TEST_CASE("BLIP MessageBuilder", "[blip]") {
    MessageBuilder msg{ {"Shoe-Size", "8.5"}, {"Hair", "yes"} };
    msg["Eyes"] = "Brown";
    msg << "Hi! ";
    msg << "This is the body.";
    msg.urgent = true;

    string data = std::move(msg).finish();
//    cout << data << endl;
//    cout << hexString(data) << endl;
    CHECK(data == "\x22Shoe-Size\08.5\0Hair\0yes\0Eyes\0Brown\0Hi! This is the body."s);
    CHECK(msg.flags() == FrameFlags::kUrgent);
}


static constexpr string_view kTestFrameHex = "01201753686f652d53697a6500382e35004861697200796573004869216031fe1e";

TEST_CASE("BLIP Send Message", "[blip]") {
    RunCoroutine([]() -> Future<void> {
        BLIPIO b;
        MessageBuilder msg{ {"Shoe-Size", "8.5"}, {"Hair", "yes"} };
        msg << "Hi!";
        msg.noreply = true;
        Future<MessageInRef> fReply = b.sendRequest(msg);
        CHECK(!fReply.hasResult());

        Result<string> frame = AWAIT b.output();
        REQUIRE(frame);
        CHECK(hexString(*frame) == kTestFrameHex);

        MessageInRef reply = AWAIT fReply;
        CHECK(!reply);

        b.stop();
        frame = AWAIT b.output();
        CHECK(!frame);
        RETURN noerror;
    });
}


TEST_CASE("BLIP Receive Message", "[blip]") {
    BLIPIO b;

    string testFrame = decodeHexString(kTestFrameHex);
    MessageInRef msg = b.receive(testFrame);
    REQUIRE(msg);
    msg->dump(cout, true);
    cout << endl;

    CHECK(msg->number() == MessageNo{1});
    CHECK(int(msg->flags()) == int(FrameFlags(kRequestType) | kNoReply));
    CHECK(msg->property("Shoe-Size") == "8.5");
    CHECK(msg->boolProperty("Hair") == true);
    CHECK(msg->property("foo") == "");
    CHECK(msg->body() == "Hi!");
    b.stop();
}


staticASYNC<void> testSendReceive(initializer_list<MessageBuilder::property> properties,
                                  string body,
                                  bool compressed)
{
    BLIPIO sender;
    MessageBuilder msg{properties};
    msg << body;
    msg.noreply = true;
    msg.compressed = compressed;
    Future<MessageInRef> fReply = sender.sendRequest(msg);
    CHECK(!fReply.hasResult());
    sender.closeSend();

    vector<string> frames;
    size_t size = 0;
    while (true) {
        Result<string> frame = AWAIT sender.output();
        if (!frame)
            break;
        frames.push_back(frame.value());
        size += frame->size();
    }
    REQUIRE(!frames.empty());
    if (compressed)
        cout << "Compressed to " << (size * 100.0 / body.size()) << "%\n";
    sender.stop();

    BLIPIO receiver;
    MessageInRef rcvd;
    for (auto &frame : frames) {
        REQUIRE(!rcvd);
        rcvd = receiver.receive(frame);
    }
    REQUIRE(rcvd);
    CHECK(rcvd->number() == MessageNo{1});
    if (compressed)
        CHECK(int(rcvd->flags()) == (kRequestType | kNoReply | kCompressed));
    else
        CHECK(int(rcvd->flags()) == (kRequestType | kNoReply));
    for (auto kv : properties)
        CHECK(rcvd->property(kv.first) == kv.second);
    CHECK(rcvd->body() == body);
    RETURN noerror;
}


TEST_CASE("BLIP Send And Receive Message", "[blip]") {
    RunCoroutine([]() -> Future<void> {
        string body = AWAIT readFile("README.md");
        AWAIT testSendReceive({}, body, false);
        AWAIT testSendReceive({}, body, true);
        RETURN noerror;
    });
}
