//
// HTTPParser.cc
//
// 
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

#include "HTTPParser.hh"
#include "llhttp.h"

namespace crouton {
    using namespace std;


    HTTPParser::Error::Error(int code, const char* reason)
    :runtime_error(reason)
    ,code(code)
    { }

#define SELF ((HTTPParser*)parser->data)

    HTTPParser::HTTPParser(IStream* stream, Role role)
    :_stream(stream)
    ,_role(role)
    ,_settings(make_unique<llhttp_settings_s>())
    ,_parser(make_unique<llhttp_t>())
    {
        llhttp_settings_init(_settings.get());

        _settings->on_status = [](llhttp_t* parser, const char *data, size_t length) -> int {
            SELF->statusMessage = string(data, length);
            return 0;
        };
        _settings->on_url = [](llhttp_t* parser, const char *data, size_t length) -> int {
            SELF->requestURI = URL(string(data, length));
            return 0;
        };
        _settings->on_header_field = [](llhttp_t* parser, const char *data, size_t length) -> int {
            SELF->_curHeaderName = string(data, length);
            return 0;
        };
        _settings->on_header_value = [](llhttp_t* parser, const char *data, size_t length) -> int {
            return SELF->addHeader(string(data, length));
        };
        _settings->on_headers_complete = [](llhttp_t* parser) -> int {
            SELF->_headersComplete = true;
            return 0;
        };
        _settings->on_body = [](llhttp_t* parser, const char *data, size_t length) -> int {
            return SELF->gotBody(data, length);
        };
        _settings->on_message_complete = [](llhttp_t* parser) -> int {
            SELF->_messageComplete = true;
            return 0;
        };

        llhttp_init(_parser.get(),
                    (role == Request ? HTTP_REQUEST : HTTP_RESPONSE),
                    _settings.get());
        _parser->data = this;
    }


    HTTPParser::~HTTPParser() {
        llhttp_reset(_parser.get());
    }


    Future<HTTPStatus> HTTPParser::readRequest() {
        assert(_stream);
        if (!_stream->isOpen())
            AWAIT _stream->open();

        ConstBuf data;
        do {
            data = AWAIT _stream->readNoCopy();
        } while (!parseData(data));

        RETURN this->status;
    }


    Future<std::string> HTTPParser::readBody() {
        assert(_stream);
        ConstBuf data;
        while (_body.empty() && !complete()) {
            data = AWAIT _stream->readNoCopy();
            parseData(data);
        }
        RETURN std::move(_body);
    }

    Future<std::string> HTTPParser::entireBody() {
        string entireBody;
        while (!complete()) {
            entireBody += (AWAIT readBody());
        }
        RETURN entireBody;
    }


    bool HTTPParser::parseData(ConstBuf data) {
        llhttp_errno_t err;
        if (data.len > 0)
            err = llhttp_execute(_parser.get(), (const char*)data.base, data.len);
        else
            err = llhttp_finish(_parser.get());

        if (err != HPE_OK) {
            if (err == HPE_PAUSED_UPGRADE) {
                // We have a (WebSocket) upgrade. Put any data after the request into _body.
                assert(llhttp_get_upgrade(_parser.get()) != 0);
                _upgraded = true;
                const char* end = llhttp_get_error_pos(_parser.get());
                assert(end >= data.base && end <= (char*)data.base + data.len);
                _body = string(end, (char*)data.base + data.len - end);
            } else {
                throw Error(err, llhttp_get_error_reason(_parser.get()));
            }
        }

        if (_headersComplete) {
            if (_role == Role::Request) {
                this->requestMethod = HTTPMethod{llhttp_get_method(_parser.get())};
            } else {
                this->status = HTTPStatus{llhttp_get_status_code(_parser.get())};
            }
        }
        return _headersComplete;
    }


    int HTTPParser::addHeader(std::string value) {
        assert(!_curHeaderName.empty());
        auto [i, added] = this->headers.insert({_curHeaderName, value});
        if (!added) {
            i->second += ", ";
            i->second += value;
        }
        _curHeaderName = "";
        return 0;
    }


    int HTTPParser::gotBody(const char *data, size_t length) {
        _body.append(data, length);
        return 0;
    }

}
