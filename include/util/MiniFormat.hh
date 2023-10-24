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

/*
 A string formatting API somewhat compatible with `std::format`, but optimized for small code size.
 */

namespace crouton::minifmt {
    using std::string;
    using std::string_view;

    class write;


    namespace i {
        // Enumeration identifying all formattable types.
        enum class FmtID : uint8_t {
            None = 0,
            Bool,
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

        using enum FmtID;

        // This maps types to FmtID values. Every formattable type needs an entry here.
        template <typename T> struct Formatting { };
        template<> struct Formatting<bool>              { static constexpr FmtID id = Bool; };
        template<> struct Formatting<char>              { static constexpr FmtID id = Char; };
        template<> struct Formatting<unsigned char>     { static constexpr FmtID id = UInt; };
        template<> struct Formatting<short>             { static constexpr FmtID id = Int; };
        template<> struct Formatting<unsigned short>    { static constexpr FmtID id = UInt; };
        template<> struct Formatting<int>               { static constexpr FmtID id = Int; };
        template<> struct Formatting<unsigned int>      { static constexpr FmtID id = UInt; };
        template<> struct Formatting<long>              { static constexpr FmtID id = Long; };
        template<> struct Formatting<unsigned long>     { static constexpr FmtID id = ULong; };
        template<> struct Formatting<long long>         { static constexpr FmtID id = LongLong; };
        template<> struct Formatting<unsigned long long>{ static constexpr FmtID id = ULongLong; };
        template<> struct Formatting<float>             { static constexpr FmtID id = Double; };
        template<> struct Formatting<double>            { static constexpr FmtID id = Double; };
        template<> struct Formatting<const char*>       { static constexpr FmtID id = CString; };
        template<> struct Formatting<char*>             { static constexpr FmtID id = CString; };
        template<> struct Formatting<void*>             { static constexpr FmtID id = Pointer; };
        template<> struct Formatting<std::string>       { static constexpr FmtID id = String; };
        template<> struct Formatting<std::string_view>  { static constexpr FmtID id = StringView; };
        template<> struct Formatting<write>             { static constexpr FmtID id = Write; };

        // Returns the FmtID value corresponding to the type of its argument.
        template <typename T>
        consteval FmtID getFmtID(T&&) { return Formatting<std::decay_t<T>>::id; }
    }

    // Transforms args before they're passed to `format`.
    // Makes sure non-basic types, like `std::string`, are passed by pointer.
    template <std::integral T>          auto passArg(T t)         {return t;}
    template <std::floating_point T>    auto passArg(T t)         {return t;}
    template <typename T>               auto passArg(T const* t)  {return t;}
    template <typename T>               auto passArg(T* t)        {return t;}
    template <typename T>               auto passArg(T const& t)
                            requires (!std::integral<T> && !std::floating_point<T>) {return &t;}


    /** The concept `Formattable` defines what types can be passed as args to `format`. */
    template <typename T>
    concept Formattable = requires { i::Formatting<std::decay_t<T>>::id; };

    // FmtIDs<...>::ids is a C array of the FmtIDs corresponding to the template argument types.
    template<Formattable... Args>
    struct FmtIDs {
        static constexpr i::FmtID ids[] {i::Formatting<std::decay_t<Args>>::id... , i::FmtID::None};
    };
    using FmtIDList = i::FmtID const*;

    void format_types(std::ostream&, string_view fmt, FmtIDList types, ...);
    void vformat_types(std::ostream&, string_view fmt, FmtIDList types, va_list);
    string format_types(string_view fmt, FmtIDList types, ...);
    string vformat_types(string_view fmt, FmtIDList types, va_list);


    /** Writes formatted output to an ostream.
        @param out  The stream to write to.
        @param fmt  Format string, with `{}` placeholders for args.
        @param args  Arguments; any type satisfying `Formattable`. */
    template<Formattable... Args>
    void format(std::ostream& out, string_view fmt, Args &&...args) {
        format_types(out, fmt, FmtIDs<Args...>::ids, passArg(args)...);
    }


    /** Returns a formatted string..
        @param fmt  Format string, with `{}` placeholders for args.
        @param args  Arguments; any type satisfying `Formattable`. */
    template<Formattable... Args>
    string format(string_view fmt, Args &&...args) {
        return format_types(fmt, FmtIDs<Args...>::ids, passArg(args)...);
    }


    /** Struct that can be wrapped around an argument to `format()`.
        Works with any type that can be written to an ostream with `<<`. */
    class write {
    public:
        template <typename T>
        write(T &&value)
        :_ptr(reinterpret_cast<const void*>(&value))
        ,_write(&writeFn<std::remove_reference_t<T>>)
        { }

        // for compatibility with std::fmt or spdlog, this object can itself be written
        friend std::ostream& operator<< (std::ostream& out, write const& f) {
            f._write(out, f._ptr);
            return out;
        }
    private:
        template <typename T>
        static void writeFn(std::ostream& out, const void* ptr) {
            out << *(const T*)ptr;
        }

        const void* _ptr;                               // address of value -- type-erased T*
        void (*_write)(std::ostream&, const void*);     // pointer to writeFn<T>()
    };


}
