//
// MessageBuilder.hh
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
#include "Message.hh"
#include <functional>
#include <initializer_list>
#include <sstream>

namespace crouton::io::blip {

    /** A temporary object used to construct an outgoing message (request or response).
        The message is sent by calling Connection::sendRequest() or MessageIn::respond(). */
    class MessageBuilder {
      public:

        using property = std::pair<string_view, string_view>;

        /** Constructs a MessageBuilder for a request, optionally setting its Profile property. */
        explicit MessageBuilder(string_view profile = "");

        /** Constructs a MessageBuilder for a request, with a list of properties. */
        MessageBuilder(std::initializer_list<property>);

        /** Constructs a MessageBuilder for a response. */
        explicit MessageBuilder(MessageIn* inReplyTo);

        /** Adds a property. */
        MessageBuilder& addProperty(string_view name, string_view value);

        /** Adds a property with an integer value. */
        MessageBuilder& addProperty(string_view name, int64_t value);

        /** Adds multiple properties. */
        MessageBuilder& addProperties(std::initializer_list<property>);

        struct propertySetter {
            MessageBuilder& builder;
            string_view     name;

            MessageBuilder& operator=(string_view value) { return builder.addProperty(name, value); }

            MessageBuilder& operator=(int64_t value) { return builder.addProperty(name, value); }
        };

        propertySetter operator[](string_view name) { return {*this, name}; }

        /** Makes a response an error. */
        void makeError(Message::Error);

        /** Adds data to the body of the message. No more properties can be added afterwards. */
        MessageBuilder& write(ConstBytes);

        template <typename T>
        MessageBuilder& write(T&& t)  {return write(ConstBytes(std::forward<T>(t)));}

        template <typename T>
        MessageBuilder& operator<<(T&& t) { return write(std::forward<T>(t)); }

        /** Clears the MessageBuilder so it can be used to create another message. */
        void reset();

//        /** Callback to provide the body of the message; will be called whenever data is needed. */
//        MessageDataSource dataSource;
//
//        /** Callback to be invoked as the message is delivered (and replied to, if appropriate) */
//        MessageProgressCallback onProgress;

        /** Is the message urgent (will be sent more quickly)? */
        bool urgent{false};

        /** Should the message's body be gzipped? */
        bool compressed{false};

        /** Should the message refuse replies? */
        bool noreply{false};

        // Exposed for testing.
        string finish();
        FrameFlags  flags() const;

      protected:
        friend class MessageIn;
        friend class MessageOut;

        static void writeTokenizedString(std::ostream& out, string_view str);

        MessageType type{kRequestType};

      private:
        void finishProperties();

        std::stringstream   _out;                     // The message
        std::stringstream   _properties;              // Accumulates encoded properties
        bool                _wroteProperties{false};  // Have _properties been written to _out yet?
    };

}
