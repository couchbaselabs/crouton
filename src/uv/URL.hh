//
// URL.hh
//
// 
//

#pragma once
#include <string>
#include <string_view>

namespace snej::coro::uv {

    /** A parsed version of a URL. The string properties point into the given string,
        and become invalid if that string changes or goes out of scope.
        If that's a problem, use `URL` instead.

        @note The string properties are all substrings of the input; nothing is unescaped. */
    class URLRef {
    public:
        URLRef() = default;
        explicit URLRef(const char* str)           {parse(str);}
        explicit URLRef(std::string const& str)    :URLRef(str.c_str()) { }

        /// Parses a URL, updating the properties. Returns false on error.
        [[nodiscard]] bool tryParse(const char*);

        /// Parses a URL, updating the properties. Throws std::invalid_argument on error.
        void parse(const char*);

        std::string_view scheme;
        std::string_view hostname;
        uint16_t port = 0;
        std::string_view path;
        std::string_view query;

    protected:
    };


    /** A parsed version of a URL. Contains a copy of the string. */
    class URL : public URLRef {
    public:
        explicit URL(std::string&& str)     :URLRef(), _str(std::move(str)) {parse(_str.c_str());}
        explicit URL(std::string_view str)  :URL(std::string(str)) { }
        explicit URL(const char* str)       :URL(std::string(str)) { }

        URL(URL const& url)                 :URL(url.asString()) { }
        URL& operator=(URL const& url)      {_str = url._str; parse(_str.c_str()); return *this;}

        std::string const& asString() const {return _str;}
        operator std::string() const        {return _str;}

    private:
        std::string _str;
    };

}
