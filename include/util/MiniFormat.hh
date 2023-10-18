//
// MiniFormat.hh
//
// 
//

#pragma once
#include <concepts>
#include <cstdarg>
#include <initializer_list>
#include <iosfwd>
#include <string>
#include <string_view>

namespace crouton::minifmt {
    using std::string;
    using std::string_view;


    /** Struct that can be wrapped around an argument to `format`.
        Works with any type that can be written to an ostream with `<<`. */
    class write {
    public:
        template <typename T>
        write(T &&value)
        :_ptr(&value)
        ,_write(&writeFn<T>)
        { }

        friend std::ostream& operator<< (std::ostream& out, write const& f) {
            f._write(out, f._ptr);
            return out;
        }
    private:
        template <typename T>
        static void writeFn(std::ostream& out, void* ptr) {
            out << *(T*)ptr;
        }

        void* _ptr;
        void (*_write)(std::ostream&, void*);
    };


    namespace i {
        // Enumeration identifying all formattable types.
        enum class FmtID : uint8_t {
            None = 0,
            Char,
            Int,
            UInt,
            Long,
            ULong,
            LongLong,
            ULongLong,
            Double,
            CString,
            Pointer,
            String,
            StringView,
            Write,
        };

        // This defines the types that can be used as arguments to `format`:
        template <typename T> struct Formatting { };
        template <> struct Formatting<char>             { static constexpr FmtID id = FmtID::Char; };
        template <> struct Formatting<unsigned char>    { static constexpr FmtID id = FmtID::UInt; };
        template <> struct Formatting<short>            { static constexpr FmtID id = FmtID::Int; };
        template <> struct Formatting<unsigned short>   { static constexpr FmtID id = FmtID::UInt; };
        template <> struct Formatting<int>              { static constexpr FmtID id = FmtID::Int; };
        template <> struct Formatting<unsigned int>     { static constexpr FmtID id = FmtID::UInt; };
        template <> struct Formatting<long>             { static constexpr FmtID id = FmtID::Long; };
        template <> struct Formatting<unsigned long>    { static constexpr FmtID id = FmtID::ULong; };
        template <> struct Formatting<long long>         { static constexpr FmtID id = FmtID::LongLong; };
        template <> struct Formatting<unsigned long long>{ static constexpr FmtID id = FmtID::ULongLong; };
        template <> struct Formatting<float>             { static constexpr FmtID id = FmtID::Double; };
        template <> struct Formatting<double>             { static constexpr FmtID id = FmtID::Double; };
        template <> struct Formatting<const char*>         { static constexpr FmtID id = FmtID::CString; };
        template <> struct Formatting<char*>             { static constexpr FmtID id = FmtID::CString; };
        template <> struct Formatting<void*>             { static constexpr FmtID id = FmtID::Pointer; };
        template <> struct Formatting<std::string>         { static constexpr FmtID id = FmtID::String; };
        template <> struct Formatting<std::string_view> { static constexpr FmtID id = FmtID::StringView; };
        template <> struct Formatting<write>          { static constexpr FmtID id = FmtID::Write; };

        template <typename T>
        FmtID getFmtID(T&&) { return Formatting<std::decay_t<T>>::id; }

        // Transforms args before they're passed to `format`.
        // Makes sure non-basic types, like `std::string`, are passed by pointer.
        template <std::integral T>  auto passParam(T t) {return t;}
        template <std::floating_point T>  auto passParam(T t) {return t;}
        template <typename T> auto passParam(T const* t) {return t;}
        template <typename T> auto passParam(T* t) {return t;}
        template <typename T> auto passParam(T&& t) {return &t;}
    }

    /** The concept `Formattable` defines what types can be passed as args to `format`. */
    template <typename T>
    concept Formattable = requires {
        i::Formatting<std::decay_t<T>>::id;
    };

    void _format(std::ostream&, string_view fmt, std::initializer_list<i::FmtID> types, ...);
    void _vformat(std::ostream&, string_view fmt, std::initializer_list<i::FmtID> types, va_list);
    string _format(string_view fmt, std::initializer_list<i::FmtID> types, ...);
    string _vformat(string_view fmt, std::initializer_list<i::FmtID> types, va_list);


    /** Writes formatted output to an ostream.
        @param out  The stream to write to.
        @param fmt  Format string, with `{}` placeholders for args.
        @param args  Arguments; any type satisfying `Formattable`. */
    template<Formattable... Args>
    void format(std::ostream& out, string_view fmt, Args &&...args) {
        _format(out, fmt, {i::getFmtID(args)...}, i::passParam(args)...);
    }


    /** Returns a formatted string..
        @param fmt  Format string, with `{}` placeholders for args.
        @param args  Arguments; any type satisfying `Formattable`. */
    template<Formattable... Args>
    string format(string_view fmt, Args &&...args) {
        return _format(fmt, {i::getFmtID(args)...}, i::passParam(args)...);
    }

}
