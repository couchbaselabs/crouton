//
// MessageBuilder.cc
//
// Copyright 2017-Present Couchbase, Inc.
//
// Use of this software is governed by the Business Source License included
// in the file licenses/BSL-Couchbase.txt.  As of the Change Date specified
// in that file, in accordance with the Business Source License, use of this
// software will be governed by the Apache License, Version 2.0, included in
// the file licenses/APL2.txt.
//

#include "MessageBuilder.hh"
#include "BLIPIO.hh"
#include "BLIPProtocol.hh"
#include "StringUtils.hh"
#include <ostream>

namespace crouton::blip {
    using namespace std;


#pragma mark - MESSAGE BUILDER:

    
    MessageBuilder::MessageBuilder(string_view profile) {
        if (!profile.empty())
            addProperty("Profile", profile);
    }

    
    MessageBuilder::MessageBuilder(MessageIn* inReplyTo) : MessageBuilder() {
        assert(!inReplyTo->isResponse());
        type = kResponseType;
        urgent = inReplyTo->urgent();
    }


    MessageBuilder::MessageBuilder(initializer_list<property> properties) : MessageBuilder() {
        addProperties(properties);
    }


    MessageBuilder& MessageBuilder::addProperties(initializer_list<property> properties) {
        for (const property& p : properties) addProperty(p.first, p.second);
        return *this;
    }


    void MessageBuilder::makeError(Message::Error err) {
        assert(!err.domain.empty() && err.code != 0);
        type = kErrorType;
        addProperty("Error-Domain", err.domain);
        addProperty("Error-Code", err.code);
        write(ConstBytes(err.message));
    }


    FrameFlags MessageBuilder::flags() const {
        int flags = type & kTypeMask;
        if (urgent) flags |= kUrgent;
        if (compressed) flags |= kCompressed;
        if (noreply) flags |= kNoReply;
        return (FrameFlags)flags;
    }


    void MessageBuilder::writeTokenizedString(ostream& out, string_view str) {
        assert(str.find('\0') == string::npos);
        out << str << '\0';
    }


    MessageBuilder& MessageBuilder::addProperty(string_view name, string_view value) {
        assert(!_wroteProperties);
        writeTokenizedString(_properties, name);
        writeTokenizedString(_properties, value);
        return *this;
    }


    MessageBuilder& MessageBuilder::addProperty(string_view name, int64_t value) {
        constexpr size_t bufSize = 30;
        char             valueStr[bufSize];
        return addProperty(name, string_view(valueStr, snprintf(valueStr, bufSize, "%lld", (long long)value)));
    }


    void MessageBuilder::finishProperties() {
        if (!_wroteProperties) {
            string properties = _properties.str();
            _properties.clear();
            size_t propertiesSize = properties.size();
            if (propertiesSize > kMaxPropertiesSize)
                Error::raise(BLIPError::PropertiesTooLarge);
            char  buf[kMaxVarintSize];
            _out << string_view(buf, putUVarint(propertiesSize, buf));
            _out << properties;
            _wroteProperties = true;
        }
    }


    MessageBuilder& MessageBuilder::write(ConstBytes data) {
        if (!_wroteProperties) 
            finishProperties();
        _out << string_view(data);
        return *this;
    }


    string MessageBuilder::finish() {
        finishProperties();
        return _out.str();
    }


    void MessageBuilder::reset() {
        urgent = compressed = noreply = false;
        _out.str("");
        _properties.str("");
        _wroteProperties = false;
    }

}  
