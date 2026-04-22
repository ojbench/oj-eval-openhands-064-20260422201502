#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <type_traits>
#include <concepts>
#include <cstdint>
using namespace std;

namespace sjtu {
using sv_t = std::string_view;

struct format_error : std::exception {
    const char *msg;
    explicit format_error(const char *m = "invalid format") : msg(m) {}
    const char *what() const noexcept override { return msg; }
};

// Type traits
template <class T>
struct is_cstr_array : std::false_type {};

template <std::size_t N>
struct is_cstr_array<const char (&)[N]> : std::true_type {};

template <std::size_t N>
struct is_cstr_array<char (&)[N]> : std::true_type {};

template <class T>
inline constexpr bool is_cstr_array_v = is_cstr_array<T>::value;

template <class T>
struct is_string_like : std::false_type {};

template <>
struct is_string_like<std::string> : std::true_type {};

template <>
struct is_string_like<std::string_view> : std::true_type {};

template <>
struct is_string_like<const char *> : std::true_type {};

template <>
struct is_string_like<char *> : std::true_type {};

template <class T>
inline constexpr bool is_string_like_v = is_string_like<std::remove_cv_t<std::remove_reference_t<T>>>::value || is_cstr_array_v<std::remove_reference_t<T>>;

// vector trait
template <class T>
struct is_vector : std::false_type {};

template <class T, class A>
struct is_vector<std::vector<T, A>> : std::true_type {};

template <class T>
inline constexpr bool is_vector_v = is_vector<std::remove_cv_t<std::remove_reference_t<T>>>::value;

// ostream insertion detection
template <class T>
concept has_ostream_insertion = requires(std::ostream &os, const T &t) { os << t; };

// to string_view helper
inline std::string_view to_sv(const std::string &s) { return std::string_view{s}; }
inline std::string_view to_sv(std::string_view s) { return s; }
inline std::string_view to_sv(const char *p) { return std::string_view{p ? p : ""}; }
inline std::string_view to_sv(char *p) { return std::string_view{p ? p : ""}; }

template <std::size_t N>
inline std::string_view to_sv(const char (&arr)[N]) { return std::string_view{arr, N - 1}; }

// compile-time parser utilities
consteval bool next_specifier(sv_t &s, char &spec) {
    while (true) {
        auto pos = s.find('%');
        if (pos == sv_t::npos) return false;
        if (pos + 1 >= s.size()) throw format_error{"missing specifier after '%'"};
        if (s[pos + 1] == '%') {
            s.remove_prefix(pos + 2);
            continue; // escaped percent
        }
        spec = s[pos + 1];
        s.remove_prefix(pos + 2);
        return true;
    }
}

// compile-time type checks per spec
template <class T>
consteval bool type_matches(char spec) {
    using D = std::decay_t<T>;
    if (spec == 's') return is_string_like_v<D>;
    if (spec == 'd') return std::is_integral_v<D> && !std::is_same_v<D, bool>;
    if (spec == 'u') return std::is_integral_v<D> && !std::is_same_v<D, bool>;
    if (spec == '_') return true; // accepts any
    return false;
}

// format_string class that validates at compile time
template <typename... Args>
struct format_string {
    std::string_view fmt;
    consteval explicit format_string(const char *f) : fmt(f) {
        sv_t s = fmt;
        char c{};
        // check one-by-one for each arg
        (([&] {
            while (true) {
                if (!next_specifier(s, c)) throw format_error{"too few specifiers"};
                if (c == 's' || c == 'd' || c == 'u' || c == '_') {
                    if (!type_matches<Args>(c)) throw format_error{"invalid specifier for argument type"};
                    break;
                } else {
                    throw format_error{"invalid specifier"};
                }
            }
        })(), ...);
        // ensure no extra specifiers remain
        if (next_specifier(s, c)) throw format_error{"too many specifiers"};
    }
    constexpr std::string_view get() const { return fmt; }
};

template <typename... Args>
using format_string_t = format_string<std::decay_t<Args>...>;

// default formatting
template <class T>
void default_print(std::ostream &os, const T &val);

template <class T>
void default_print_vector(std::ostream &os, const std::vector<T> &v) {
    os << '[';
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) os << ',';
        default_print(os, v[i]);
    }
    os << ']';
}

template <class T>
void default_print(std::ostream &os, const T &val) {
    using D = std::decay_t<T>;
    if constexpr (is_string_like_v<D>) {
        os << to_sv(val);
    } else if constexpr (std::is_integral_v<D> && !std::is_same_v<D, bool>) {
        if constexpr (std::is_signed_v<D>) os << static_cast<std::int64_t>(val);
        else os << static_cast<std::uint64_t>(val);
    } else if constexpr (is_vector_v<D>) {
        default_print_vector(os, val);
    } else if constexpr (has_ostream_insertion<D>) {
        os << val;
    } else {
        static_assert(!std::is_same_v<D, D>, "Type not printable by default formatter");
    }
}

// print a single argument according to spec
template <class T>
void format_value(std::ostream &os, char spec, const T &val) {
    using D = std::decay_t<T>;
    switch (spec) {
    case 's':
        if constexpr (is_string_like_v<D>) {
            os << to_sv(val);
        } else {
            throw format_error{"%s requires string-like"};
        }
        break;
    case 'd':
        if constexpr (std::is_integral_v<D> && !std::is_same_v<D, bool>) {
            os << static_cast<std::int64_t>(val);
        } else {
            throw format_error{"%d requires integral"};
        }
        break;
    case 'u':
        if constexpr (std::is_integral_v<D> && !std::is_same_v<D, bool>) {
            os << static_cast<std::uint64_t>(val);
        } else {
            throw format_error{"%u requires integral"};
        }
        break;
    case '_':
        default_print(os, val);
        break;
    default:
        throw format_error{"invalid specifier"};
    }
}

// runtime helpers for printing literals and specs
inline void print_until_spec(std::ostream &os, std::string_view &s) {
    while (true) {
        auto pos = s.find('%');
        if (pos == std::string_view::npos) {
            os << s;
            s = std::string_view{};
            return;
        }
        if (pos + 1 < s.size() && s[pos + 1] == '%') {
            os << s.substr(0, pos) << '%';
            s.remove_prefix(pos + 2);
            continue;
        }
        os << s.substr(0, pos);
        s.remove_prefix(pos);
        return;
    }
}

inline char get_next_spec(std::string_view &s) {
    if (s.empty() || s[0] != '%') throw format_error{"internal parsing error"};
    if (s.size() == 1) throw format_error{"missing specifier after '%'"};
    char c = s[1];
    s.remove_prefix(2);
    return c;
}

// printf implementation
template <typename... Args>
inline auto printf(format_string_t<Args...> fmt, const Args &...args) -> void {
    std::string_view s = fmt.get();
    auto emit = [&](auto &&arg) {
        print_until_spec(std::cout, s);
        char spec = get_next_spec(s);
        if (spec == 's' || spec == 'd' || spec == 'u' || spec == '_') {
            format_value(std::cout, spec, arg);
        } else {
            throw format_error{"invalid specifier"};
        }
    };
    (emit(args), ...);
    print_until_spec(std::cout, s);
}

} // namespace sjtu

int main() {
    // Intentionally no I/O. Library is implemented for evaluation.
    return 0;
}
