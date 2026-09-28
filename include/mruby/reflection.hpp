#pragma once
#include <mruby.h>
#include <mruby/data.h>

extern "C" {
extern const struct mrb_data_type mrb_void_pointer_type;
extern const struct mrb_data_type mrb_const_void_pointer_type;
}

#if defined(__cpp_impl_reflection)

#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/error.h>
#include <mruby/gc.h>
#include <mruby/hash.h>
#include <mruby/proc.h>
#include <mruby/string.h>
#include <mruby/throw.h>
#include <mruby/variable.h>

#include <mruby/cpp_helpers.hpp>
#include <mruby/cpp_to_mrb_value.hpp>
#include <mruby/mrb_value_to_cpp.hpp>
#include <mruby/reflect_presyms.hpp>

#if defined(__GLIBCXX__)
#include <cxxabi.h>
#endif
#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <compare>
#include <cstdio>
#include <functional>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <map>
#include <meta>
#include <new>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <variant>
#include <ranges>
#include <span>
#include <filesystem>
#include <regex>
#include <system_error>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

#if __has_include(<mruby/presym/reflect.h>) && !defined(MRB_REFLECT_NO_PRESYMS)
#include <mruby/presym/reflect.h>
#else
inline constexpr std::array<std::pair<std::string_view, mrb_sym>, 0> reflect_presyms{};
#endif

namespace mrb_cpp_reflector
{

consteval mrb_sym reflect_presym(const std::string_view name)
{
    for (const auto &[known, sym] : reflect_presyms)
        if (known == name) return sym;
    return 0;
}

consteval std::string_view reflect_name(const std::meta::info named)
{
    if (std::meta::is_type(named) || std::meta::is_namespace(named)) return std::define_static_string(reflect_class_name(named));
    return reflect_identifier(named);
}

template <std::meta::info Named>
mrb_sym reflect_intern(mrb_state *const mrb)
{
    constexpr std::string_view name = reflect_name(Named);
    constexpr mrb_sym presym = reflect_presym(name);
    if constexpr (presym != 0) return presym;
    else return mrb_intern_static(mrb, name.data(), name.size());
}

inline constexpr std::string_view kInstanceMethods = "InstanceMethods", kOwner = "owner", kInitialize = "initialize", kReplace = "replace", kToS = "to_s",
                                  kToA = "to_a", kToH = "to_h", kEach = "each",
                                  kEnumerable = "Enumerable";

template <const std::string_view &Name>
mrb_sym reflect_sym(mrb_state *const mrb)
{
    constexpr mrb_sym presym = reflect_presym(Name);
    if constexpr (presym != 0) return presym;
    else return mrb_intern_static(mrb, Name.data(), Name.size());
}

struct reflect_upcast {
    const mrb_data_type *base;
    void *(*to_base)(void *);
};

struct reflect_lifetime_base;
struct reflect_tracked_base;
struct reflect_gc_root;

struct reflect_callbacks {
    std::thread::id thread;
    std::unordered_set<reflect_gc_root *> roots;
    bool closed = false;
    void unregister_released(mrb_state *mrb);
};

inline mrb_sym reflect_callbacks_key(mrb_state *const mrb)
{
    return MRB_SYM(__reflected_callbacks__);
}

inline reflect_callbacks &reflect_callbacks_of(mrb_state *const mrb)
{
    return *static_cast<reflect_callbacks *>(mrb_cptr(mrb_iv_get(mrb, mrb_obj_value(mrb->object_class), reflect_callbacks_key(mrb))));
}

inline constexpr int reflect_receiver = -1;
inline constexpr int reflect_nowhere = -2;

enum class reflect_answer_test : unsigned char { none, success, error, negative };

enum class reflect_word : unsigned char { takes_ownership, ends_lifetime, retains, errors, stack_reserve, threadsafe, allocator, deallocator, shared_ownership };

struct reflect_parameter {
    int number = -1;
    const char *identifier = std::define_static_string("");
};

struct reflect_object_lifetime_word {
    reflect_word word;
    const char *function = std::define_static_string("");
    reflect_parameter of{};
    reflect_parameter by{};
    reflect_parameter position{};
    reflect_parameter output_parameter{};
    const char *results_of = std::define_static_string("");
    const char *increment = std::define_static_string("");
    const char *decrement = std::define_static_string("");
    reflect_answer_test test = reflect_answer_test::none;
    bool expects_nil = false;
    mrb_int expected = 0;
    bool sets_errno = false;
    std::size_t stack_reserve = 0;
};

template <std::meta::info Class>
inline constexpr std::span<const reflect_object_lifetime_word> reflect_object_lifetime_words{};

struct reflect_function_lifetime {
    const char *name = "";
    int of = reflect_nowhere;
    int by = reflect_nowhere;
    int ended = reflect_nowhere;
    std::span<const int> retained;
    reflect_answer_test test = reflect_answer_test::none;
    bool expects_nil = false;
    mrb_int expected = 0;
    bool sets_errno = false;
    std::size_t stack_reserve = 0;
    bool allocates = false;
    bool shared = false;
    void (*deallocator)(void *object) = nullptr;
    void (*increment)(void *object) = nullptr;
    void (*decrement)(void *object) = nullptr;
};

consteval bool reflect_crosses_as_copy(const std::meta::info type)
{
    const std::meta::info bare = reflect_bare(type);
    return !std::meta::is_reference_type(type) && !std::meta::is_pointer_type(bare) && !reflect_is_view(bare) && !reflect_is_function(bare) &&
           bare != std::meta::dealias(^^std::string_view) && bare != std::meta::dealias(^^mrb_value);
}

consteval std::meta::info reflect_receiver_type(const std::meta::info type, const std::meta::info function)
{
    if (reflect_skip(function) == 1) return reflect_bare(std::meta::type_of(std::meta::parameters_of(function)[0]));
    if (std::meta::is_constructor(function)) return std::meta::dealias(std::meta::parent_of(function));
    if (std::meta::is_class_member(function) && !std::meta::is_static_member(function)) return std::meta::dealias(type);
    return ^^void;
}

consteval bool reflect_releases(const std::meta::info function)
{
    if (std::meta::is_constructor(function) || (std::meta::is_class_member(function) && !std::meta::is_static_member(function)) || reflect_skip(function) != 0)
        return false;
    const std::vector<std::meta::info> parameters = std::meta::parameters_of(function);
    return parameters.size() == 1 && reflect_is_opaque_pointer(std::meta::type_of(parameters[0]));
}

consteval std::meta::info reflect_lent_class(const std::meta::info type)
{
    const std::meta::info bare = reflect_bare(type);
    if (std::meta::is_pointer_type(bare)) {
        const std::meta::info to = std::meta::dealias(std::meta::remove_cv(std::meta::remove_pointer(bare)));
        return std::meta::is_class_type(to) ? to : ^^void;
    }
    if (std::meta::is_lvalue_reference_type(type) && std::meta::is_class_type(bare) && reflect_is_object(type) && !reflect_is_shared_ptr(bare) && !reflect_is_function(bare) &&
        !reflect_is_variant(bare))
        return bare;
    return ^^void;
}

consteval bool reflect_releases_class(const std::meta::info function, const std::meta::info klass)
{
    return reflect_releases(function) && reflect_lent_class(std::meta::type_of(std::meta::parameters_of(function)[0])) == klass;
}

consteval bool reflect_has_receiver(const std::meta::info function)
{
    return std::meta::is_constructor(function) || reflect_skip(function) == 1 || (std::meta::is_class_member(function) && !std::meta::is_static_member(function));
}

consteval std::meta::info reflect_made_class(const std::meta::info function)
{
    for (const std::meta::info p : std::meta::parameters_of(function))
        if (reflect_is_output_parameter(std::meta::type_of(p)))
            return std::meta::dealias(std::meta::remove_cv(std::meta::remove_pointer(std::meta::remove_pointer(std::meta::dealias(std::meta::type_of(p))))));
    if (!std::meta::is_constructor(function) && reflect_is_opaque_pointer(std::meta::return_type_of(function)))
        return std::meta::dealias(std::meta::remove_cv(std::meta::remove_pointer(std::meta::dealias(std::meta::return_type_of(function)))));
    return ^^void;
}

consteval int reflect_output_position(const std::meta::info function)
{
    const std::vector<std::meta::info> parameters = reflect_given_parameters(function);
    for (std::size_t i = 0; i < parameters.size(); i++)
        if (reflect_is_output_parameter(std::meta::type_of(parameters[i]))) return static_cast<int>(i);
    return reflect_nowhere;
}

consteval bool reflect_answers_number(const std::meta::info function)
{
    if (std::meta::is_constructor(function)) return false;
    const std::meta::info r = std::meta::dealias(std::meta::remove_cv(std::meta::return_type_of(function)));
    return (std::meta::is_integral_type(r) && r != ^^bool) || std::meta::is_pointer_type(r);
}

consteval bool reflect_takes_only_copies(const std::meta::info function)
{
    return std::ranges::all_of(reflect_given_parameters(function), [](const std::meta::info p) { return reflect_crosses_as_copy(std::meta::type_of(p)); });
}

consteval bool reflect_involves(const std::meta::info function, const std::meta::info klass)
{
    if (reflect_made_class(function) == klass) return true;
    if (reflect_skip(function) == 1 && reflect_bare(std::meta::type_of(std::meta::parameters_of(function)[0])) == klass) return true;
    if (std::meta::is_class_member(function) && !std::meta::is_static_member(function) && std::meta::dealias(std::meta::parent_of(function)) == klass) return true;
    return std::ranges::any_of(reflect_given_parameters(function), [&](const std::meta::info p) { return reflect_lent_class(std::meta::type_of(p)) == klass; });
}

consteval std::string_view reflect_function_name(const std::meta::info function)
{
    if (std::meta::is_constructor(function)) return "initialize";
    if (std::meta::is_operator_function(function)) return reflect_operator_method(function);
    if (std::meta::has_identifier(function)) return std::meta::identifier_of(function);
    return {};
}

consteval std::vector<std::meta::info> reflect_functions_named(const std::meta::info klass, const std::string_view name)
{
    std::vector<std::meta::info> found;
    if (std::meta::is_complete_type(klass)) {
        std::vector<std::meta::info> scopes = reflect_bases(klass);
        scopes.insert(scopes.begin(), klass);
        for (const std::meta::info scope : scopes)
            for (const std::meta::info m : std::meta::members_of(scope, std::meta::access_context::current()))
                if (std::meta::is_function(m) && (scope == klass || !std::meta::is_constructor(m)) && reflect_function_name(m) == name) found.push_back(m);
    }
    if (!found.empty()) return found;
    for (const std::meta::info m : std::meta::members_of(std::meta::parent_of(klass), std::meta::access_context::current()))
        if (std::meta::is_function(m) && reflect_function_name(m) == name && reflect_involves(m, klass)) found.push_back(m);
    return found;
}

consteval std::size_t reflect_parameter_index(const std::meta::info function, const reflect_parameter given)
{
    const std::vector<std::meta::info> parameters = reflect_given_parameters(function);
    if (given.number >= 0) return static_cast<std::size_t>(given.number);
    const auto named = [&](const std::meta::info p) { return std::meta::has_identifier(p) && std::meta::identifier_of(p) == std::string_view(given.identifier); };
    return static_cast<std::size_t>(std::ranges::find_if(parameters, named) - parameters.begin());
}

consteval int reflect_position(const std::meta::info function, const reflect_parameter given)
{
    if (given.number < 0 && *given.identifier == '\0') return reflect_has_receiver(function) ? reflect_receiver : reflect_nowhere;
    const std::vector<std::meta::info> parameters = reflect_given_parameters(function);
    const std::size_t at = reflect_parameter_index(function, given);
    if (at >= parameters.size() || reflect_lent_class(std::meta::type_of(parameters[at])) == ^^void) return reflect_nowhere;
    return static_cast<int>(at);
}

consteval std::meta::info reflect_class_at(const std::meta::info type, const std::meta::info function, const int position)
{
    if (position == reflect_receiver) return reflect_receiver_type(type, function);
    return reflect_lent_class(std::meta::type_of(reflect_given_parameters(function)[static_cast<std::size_t>(position)]));
}

consteval std::span<const reflect_object_lifetime_word> reflect_words_of(const std::meta::info klass)
{
    return std::meta::extract<const std::span<const reflect_object_lifetime_word> &>(std::meta::substitute(^^reflect_object_lifetime_words, {std::meta::reflect_constant(klass)}));
}

consteval std::string_view reflect_object_lifetime_message(const std::meta::info klass, const std::string_view function, const std::string_view text)
{
    return std::define_static_string(std::string(std::meta::display_string_of(klass)) + ": " + std::string(function) + " " + std::string(text));
}

consteval std::string_view reflect_object_lifetime_error(const std::meta::info klass, const std::span<const reflect_object_lifetime_word> words)
{
    for (const reflect_object_lifetime_word &word : words) {
        if (word.word == reflect_word::shared_ownership) {
            for (const char *const name : {word.increment, word.decrement}) {
                const std::vector<std::meta::info> functions = reflect_functions_named(klass, name);
                const auto count = std::ranges::count_if(functions, [&](const std::meta::info f) { return reflect_releases_class(f, klass); });
                if (count == 0) return reflect_object_lifetime_message(klass, name, "takes no pointer to the class as its only parameter");
                if (count > 1) return reflect_object_lifetime_message(klass, name, "has more than one overload that takes the class as its only parameter");
            }
            continue;
        }
#if !defined(__GLIBC__)
        if (word.word == reflect_word::stack_reserve) return reflect_object_lifetime_message(klass, word.function, "has a stack_reserve, which reads the thread stack with pthread_getattr_np");
#endif
        const std::vector<std::meta::info> functions = reflect_functions_named(klass, word.function);
        if (functions.empty()) return reflect_object_lifetime_message(klass, word.function, "is no function of the class and no function beside it that takes or makes the class");
        std::size_t applies = 0;
        for (const std::meta::info function : functions) {
            switch (word.word) {
            case reflect_word::takes_ownership: {
                const int of = reflect_position(function, word.of);
                const int by = reflect_position(function, word.by);
                if (of == reflect_nowhere || by == reflect_nowhere) break;
                if (of == by) return reflect_object_lifetime_message(klass, word.function, "cannot make an object take ownership of itself");
                applies++;
                break;
            }
            case reflect_word::ends_lifetime:
            case reflect_word::retains:
                if (reflect_position(function, word.position) != reflect_nowhere) applies++;
                break;
            case reflect_word::errors:
                if (!reflect_answers_number(function)) return reflect_object_lifetime_message(klass, word.function, "answers no integer and no pointer");
                applies++;
                break;
            case reflect_word::threadsafe:
                if (!reflect_takes_only_copies(function)) return reflect_object_lifetime_message(klass, word.function, "takes an argument that points into the VM");
                applies++;
                break;
            case reflect_word::allocator: {
                if (reflect_made_class(function) != klass) break;
                const int output = reflect_output_position(function);
                const bool named = word.output_parameter.number >= 0 || *word.output_parameter.identifier != '\0';
                if (!named && output != reflect_nowhere) return reflect_object_lifetime_message(klass, word.function, "gives the object through a parameter, which output_parameter: names");
                if (named && std::cmp_not_equal(reflect_parameter_index(function, word.output_parameter), output))
                    return reflect_object_lifetime_message(klass, word.function, "has no pointer to a pointer to the class where output_parameter: points");
                applies++;
                break;
            }
            case reflect_word::deallocator:
                if (reflect_releases_class(function, klass)) applies++;
                break;
            case reflect_word::stack_reserve:
            case reflect_word::shared_ownership: applies++; break;
            }
        }
        if (applies > 0) continue;
        if (word.word == reflect_word::allocator) return reflect_object_lifetime_message(klass, word.function, "makes no object of the class");
        if (word.word == reflect_word::deallocator) return reflect_object_lifetime_message(klass, word.function, "takes no pointer to the class as its only parameter");
        return reflect_object_lifetime_message(klass, word.function, "has no pointer or reference to a class at the named parameter");
    }
    for (const reflect_object_lifetime_word &word : words) {
        if (word.word != reflect_word::allocator) continue;
        const auto frees = [&](const reflect_object_lifetime_word &d) { return d.word == reflect_word::deallocator && (*d.results_of == '\0' || std::string_view(d.results_of) == word.function); };
        if (std::ranges::none_of(words, frees)) return reflect_object_lifetime_message(klass, word.function, "is an allocator without a deallocator");
    }
    return {};
}

consteval void reflect_raise_on_object_lifetime_error(const std::meta::info klass)
{
    if (const std::string_view error = reflect_object_lifetime_error(klass, reflect_words_of(klass)); !error.empty()) throw error.data();
}

consteval std::vector<std::meta::info> reflect_declaring_classes(const std::meta::info type, const std::meta::info function)
{
    std::vector<std::meta::info> classes;
    const auto add = [&](const std::meta::info c) {
        if (std::meta::is_type(c) && std::meta::is_class_type(c) && !std::ranges::contains(classes, c)) classes.push_back(c);
    };
    if (std::meta::is_type(type)) add(std::meta::dealias(type));
    if (std::meta::is_class_member(function)) add(std::meta::dealias(std::meta::parent_of(function)));
    if (reflect_skip(function) == 1) add(reflect_bare(std::meta::type_of(std::meta::parameters_of(function)[0])));
    for (const std::meta::info p : reflect_given_parameters(function)) add(reflect_lent_class(std::meta::type_of(p)));
    add(reflect_made_class(function));
    return classes;
}

consteval const char *reflect_declared_twice(const std::meta::info function, const char *const word)
{
    return std::define_static_string(std::string(std::meta::display_string_of(function)) + ": " + word + " is declared twice");
}

consteval reflect_function_lifetime reflect_object_lifetime_for(const std::meta::info type, const std::meta::info function)
{
    reflect_function_lifetime lifetime{};
    std::vector<int> retained;
    const std::string_view name = reflect_function_name(function);
    for (const std::meta::info klass : reflect_declaring_classes(type, function)) {
        const std::span<const reflect_object_lifetime_word> words = reflect_words_of(klass);
        if (words.empty()) continue;
        reflect_raise_on_object_lifetime_error(klass);
        for (const reflect_object_lifetime_word &word : words) {
            if (word.word == reflect_word::shared_ownership) {
                if (name != word.decrement || !reflect_releases_class(function, klass)) continue;
                if (lifetime.ended != reflect_nowhere) throw reflect_declared_twice(function, "ends_lifetime");
                lifetime.name = word.decrement;
                lifetime.ended = 0;
                continue;
            }
            if (name != word.function || !std::ranges::contains(reflect_functions_named(klass, word.function), function)) continue;
            switch (word.word) {
            case reflect_word::takes_ownership: {
                const int of = reflect_position(function, word.of);
                const int by = reflect_position(function, word.by);
                if (of == reflect_nowhere || by == reflect_nowhere) continue;
                if (lifetime.of != reflect_nowhere) throw reflect_declared_twice(function, "takes_ownership");
                lifetime.of = of;
                lifetime.by = by;
                break;
            }
            case reflect_word::ends_lifetime: {
                const int ended = reflect_position(function, word.position);
                if (ended == reflect_nowhere) continue;
                if (lifetime.ended != reflect_nowhere) throw reflect_declared_twice(function, "ends_lifetime");
                lifetime.ended = ended;
                break;
            }
            case reflect_word::retains: {
                const int kept = reflect_position(function, word.position);
                if (kept == reflect_nowhere) continue;
                if (std::ranges::contains(retained, kept)) throw reflect_declared_twice(function, "retains");
                retained.push_back(kept);
                break;
            }
            case reflect_word::errors:
                if (lifetime.test != reflect_answer_test::none) throw reflect_declared_twice(function, "errors");
                lifetime.test = word.test;
                lifetime.expects_nil = word.expects_nil;
                lifetime.expected = word.expected;
                lifetime.sets_errno = word.sets_errno;
                break;
            case reflect_word::stack_reserve:
                if (lifetime.stack_reserve != 0) throw reflect_declared_twice(function, "stack_reserve");
                lifetime.stack_reserve = word.stack_reserve;
                break;
            case reflect_word::allocator:
                if (reflect_made_class(function) != klass) continue;
                if (lifetime.allocates) throw reflect_declared_twice(function, "allocator");
                lifetime.allocates = true;
                break;
            case reflect_word::deallocator:
                if (!reflect_releases_class(function, klass)) continue;
                if (lifetime.ended != reflect_nowhere && lifetime.ended != 0) throw reflect_declared_twice(function, "ends_lifetime");
                lifetime.ended = 0;
                break;
            case reflect_word::threadsafe:
            case reflect_word::shared_ownership: break;
            }
            lifetime.name = word.function;
        }
    }
    if (const std::meta::info made = reflect_made_class(function); made != ^^void)
        lifetime.shared = std::ranges::any_of(reflect_words_of(made), [](const reflect_object_lifetime_word &w) { return w.word == reflect_word::shared_ownership; });
    if (*lifetime.name == '\0') lifetime.name = std::define_static_string(name);
    lifetime.retained = std::define_static_array(retained);
    return lifetime;
}

consteval bool reflect_declares(const std::meta::info type, const std::meta::info function)
{
    for (const std::meta::info klass : reflect_declaring_classes(type, function)) {
        const std::span<const reflect_object_lifetime_word> words = reflect_words_of(klass);
        if (words.empty()) continue;
        reflect_raise_on_object_lifetime_error(klass);
        const std::string_view name = reflect_function_name(function);
        for (const reflect_object_lifetime_word &word : words) {
            if (word.word == reflect_word::shared_ownership ? name == word.decrement && reflect_releases_class(function, klass)
                                                            : name == word.function && std::ranges::contains(reflect_functions_named(klass, word.function), function))
                return true;
        }
    }
    return false;
}

consteval std::meta::info reflect_deallocator_for(const std::meta::info function)
{
    const std::meta::info made = reflect_made_class(function);
    if (made == ^^void) return ^^void;
    const std::span<const reflect_object_lifetime_word> words = reflect_words_of(made);
    const std::string_view name = reflect_function_name(function);
    if (std::ranges::none_of(words, [&](const reflect_object_lifetime_word &w) { return w.word == reflect_word::allocator && name == w.function; })) return ^^void;
    const auto named = std::ranges::find_if(words, [&](const reflect_object_lifetime_word &w) { return w.word == reflect_word::deallocator && *w.results_of != '\0' && name == w.results_of; });
    const auto general = std::ranges::find_if(words, [](const reflect_object_lifetime_word &w) { return w.word == reflect_word::deallocator && *w.results_of == '\0'; });
    const auto chosen = named != words.end() ? named : general;
    if (chosen == words.end()) return ^^void;
    for (const std::meta::info f : reflect_functions_named(made, chosen->function))
        if (reflect_releases_class(f, made)) return f;
    return ^^void;
}

consteval std::meta::info reflect_share_function(const std::meta::info function, const char *const reflect_object_lifetime_word::*const named)
{
    const std::meta::info made = reflect_made_class(function);
    if (made == ^^void) return ^^void;
    for (const reflect_object_lifetime_word &w : reflect_words_of(made)) {
        if (w.word != reflect_word::shared_ownership) continue;
        for (const std::meta::info f : reflect_functions_named(made, w.*named))
            if (reflect_releases_class(f, made)) return f;
    }
    return ^^void;
}

consteval bool reflect_makes_handle(const std::meta::info function)
{
    if (!std::meta::is_constructor(function) && reflect_is_opaque_pointer(std::meta::return_type_of(function))) return true;
    return std::ranges::any_of(std::meta::parameters_of(function), [](const std::meta::info p) { return reflect_is_output_parameter(std::meta::type_of(p)); });
}

consteval std::string_view reflect_missing_object_lifetime(const std::meta::info type, const std::meta::info function)
{
    if (!reflect_makes_handle(function)) return {};
    const reflect_function_lifetime lifetime = reflect_object_lifetime_for(type, function);
    if (lifetime.allocates || lifetime.shared) return {};
    const std::meta::info made = reflect_made_class(function);
    const int output = reflect_output_position(function);
    std::string where = "the result";
    std::string keyword;
    if (output != reflect_nowhere) {
        const std::meta::info parameter = reflect_given_parameters(function)[static_cast<std::size_t>(output)];
        const std::string identifier = std::meta::has_identifier(parameter) ? std::string(std::meta::identifier_of(parameter)) : std::string();
        std::string number;
        for (int n = output; n > 0 || number.empty(); n /= 10) number.insert(number.begin(), static_cast<char>('0' + n % 10));
        where = "parameter " + number + (identifier.empty() ? std::string() : " (" + identifier + ")");
        keyword = ", output_parameter: " + (identifier.empty() ? number : ":" + identifier);
    }
    return std::define_static_string(std::string(std::meta::display_string_of(function)) + ": " + where + " gives a pointer to " + std::string(std::meta::display_string_of(made)) +
                                     ", and no declaration says who frees it; spec.reflect_object_lifetime '" + std::string(std::meta::display_string_of(made)) +
                                     "' needs allocator :" + std::string(reflect_function_name(function)) + keyword + " and a deallocator, or shared_ownership");
}

consteval void reflect_raise_on_missing_object_lifetime(const std::meta::info type, const std::meta::info function)
{
    if (const std::string_view missing = reflect_missing_object_lifetime(type, function); !missing.empty()) throw missing.data();
}

template <std::meta::info Function>
void reflect_invoke(void *const object)
{
    using P = [:std::meta::remove_pointer(std::meta::dealias(std::meta::type_of(std::meta::parameters_of(Function)[0]))):];
    static_cast<void>([:Function:](static_cast<P *>(object)));
}

template <std::meta::info Type, std::meta::info Function>
inline constexpr reflect_function_lifetime reflect_object_lifetime_of = [] {
    reflect_function_lifetime lifetime = reflect_object_lifetime_for(Type, Function);
    constexpr std::meta::info deallocator = reflect_deallocator_for(Function);
    if constexpr (deallocator != ^^void) lifetime.deallocator = &reflect_invoke<deallocator>;
    constexpr std::meta::info increment = reflect_share_function(Function, &reflect_object_lifetime_word::increment);
    if constexpr (increment != ^^void) lifetime.increment = &reflect_invoke<increment>;
    constexpr std::meta::info decrement = reflect_share_function(Function, &reflect_object_lifetime_word::decrement);
    if constexpr (decrement != ^^void) lifetime.decrement = &reflect_invoke<decrement>;
    return lifetime;
}();

struct reflect_lifetimes {
    std::uintptr_t stack_end = 0;
    mrb_value output = mrb_undef_value();
};

inline mrb_sym reflect_lifetimes_key(mrb_state *const mrb)
{
    return MRB_SYM(__reflected_lifetimes__);
}

inline reflect_lifetimes &reflect_lifetimes_of(mrb_state *const mrb)
{
    return *static_cast<reflect_lifetimes *>(mrb_cptr(mrb_iv_get(mrb, mrb_obj_value(mrb->object_class), reflect_lifetimes_key(mrb))));
}

void reflect_before_declared_call(mrb_state *mrb, mrb_value self, const reflect_function_lifetime &declared);
mrb_value reflect_after_declared_call(mrb_state *mrb, mrb_value self, const reflect_function_lifetime &declared, mrb_value answer, mrb_value output, int error_number);

struct reflect_output_scope {
    reflect_lifetimes &lifetimes;
    mrb_value outer;
    reflect_output_scope(reflect_lifetimes &state) : lifetimes(state), outer(std::exchange(state.output, mrb_undef_value())) {}
    reflect_output_scope(const reflect_output_scope &) = delete;
    reflect_output_scope &operator=(const reflect_output_scope &) = delete;
    ~reflect_output_scope() { lifetimes.output = outer; }
};

template <class Call>
mrb_value reflect_call_declared(mrb_state *const mrb, const mrb_value self, const reflect_function_lifetime &declared, const Call &call)
{
    reflect_before_declared_call(mrb, self, declared);
    const reflect_output_scope scope(reflect_lifetimes_of(mrb));
    if (declared.sets_errno) errno = 0;
    const mrb_value answer = call();
    const int error_number = errno;
    return reflect_after_declared_call(mrb, self, declared, answer, scope.lifetimes.output, error_number);
}

[[noreturn]] inline void reflect_abort_from_other_thread()
{
    std::fputs("mruby-cpp-reflection: C++ called Ruby from a thread that is not the thread of the mrb_state\n", stderr);
    std::abort();
}

[[noreturn]] inline void reflect_abort_during_collection()
{
    std::fputs("mruby-cpp-reflection: C++ called Ruby while the garbage collector frees objects\n", stderr);
    std::abort();
}

using reflect_identities = std::unordered_multimap<const void *, reflect_lifetime_base *>;

struct reflect_lifetime_base {
    void *object = nullptr;
    bool alive = false;
    bool owned = false;
    bool adopted = false;
    void (*deallocator)(void *object) = nullptr;
    reflect_lifetime_base *taken_by = nullptr;
    std::vector<reflect_lifetime_base *> taken;
    reflect_lifetime_base *parent = nullptr;
    std::size_t index = 0;
    std::span<reflect_lifetime_base *> children;
    void *holder = nullptr;
    void *(*field)(void *holder) = nullptr;
    reflect_tracked_base *tracked = nullptr;
    const mrb_data_type *type = nullptr;
    RObject *ruby = nullptr;
    mrb_state *mrb = nullptr;
    reflect_identities *identities = nullptr;
    reflect_callbacks *callbacks = nullptr;
};

struct reflect_data_type : mrb_data_type {
    std::span<const reflect_upcast> upcasts;
    void *(*object_of)(void *data) = [](void *const data) { return static_cast<reflect_lifetime_base *>(data)->object; };
};

inline mrb_sym reflect_identities_key(mrb_state *const mrb)
{
    return MRB_SYM(__reflected_identities__);
}

inline reflect_identities &reflect_identity_map(mrb_state *const mrb)
{
    return *static_cast<reflect_identities *>(mrb_cptr(mrb_iv_get(mrb, mrb_obj_value(mrb->object_class), reflect_identities_key(mrb))));
}

inline void reflect_identity_set(reflect_lifetime_base &record)
{
    record.identities->emplace(record.object, &record);
}

inline void reflect_identity_erase(reflect_lifetime_base &record)
{
    const auto [first, last] = record.identities->equal_range(record.object);
    const auto found = std::ranges::find(first, last, &record, &reflect_identities::value_type::second);
    if (found != last) record.identities->erase(found);
}

inline reflect_lifetime_base *reflect_first_dependent(const reflect_lifetime_base &record)
{
    const auto child = std::ranges::find_if(record.children, [](const reflect_lifetime_base *const c) { return c != nullptr; });
    if (child != record.children.end()) return *child;
    if (!record.taken.empty()) return record.taken.back();
    return nullptr;
}

inline void reflect_unlink_taken_by(reflect_lifetime_base &record)
{
    if (record.taken_by == nullptr) return;
    std::vector<reflect_lifetime_base *> &siblings = record.taken_by->taken;
    if (!siblings.empty() && siblings.back() == &record) siblings.pop_back();
    else if (const auto found = std::ranges::find(siblings, &record); found != siblings.end()) siblings.erase(found);
    record.taken_by = nullptr;
}

inline void reflect_end_lifetime(reflect_lifetime_base &record)
{
    reflect_lifetime_base *at = &record;
    for (;;) {
        if (reflect_lifetime_base *const dependent = reflect_first_dependent(*at); dependent != nullptr) {
            at = dependent;
            continue;
        }
        if (at->alive) {
            at->alive = false;
            reflect_identity_erase(*at);
        }
        reflect_lifetime_base *const up = at->parent != nullptr ? at->parent : at->taken_by;
        if (at->parent != nullptr) at->parent->children.at(at->index) = nullptr;
        at->parent = nullptr;
        reflect_unlink_taken_by(*at);
        if (at == &record) return;
        at = up;
    }
}

inline bool reflect_alive(reflect_lifetime_base &record)
{
    std::size_t depth = 0;
    for (const reflect_lifetime_base *at = &record; at->parent != nullptr; at = at->parent) depth++;
    for (std::size_t level = depth + 1; level-- > 0;) {
        reflect_lifetime_base *at = &record;
        for (std::size_t up = 0; up < level; up++) at = at->parent;
        if (!at->alive) [[unlikely]] return false;
        if (at->field != nullptr && at->field(at->holder) != at->object) [[unlikely]] {
            reflect_end_lifetime(*at);
            return false;
        }
    }
    return true;
}

inline void reflect_end_replaced_children(reflect_lifetime_base &record)
{
    reflect_lifetime_base *root = &record;
    while (root->parent != nullptr) root = root->parent;
    reflect_lifetime_base *at = root;
    std::size_t next = 0;
    for (;;) {
        if (next < at->children.size()) {
            reflect_lifetime_base *const child = at->children[next];
            if (child == nullptr) next++;
            else if (child->field(child->holder) != child->object) {
                reflect_end_lifetime(*child);
                next++;
            } else {
                at = child;
                next = 0;
            }
            continue;
        }
        if (at == root) return;
        next = at->index + 1;
        at = at->parent;
    }
}

inline RObject *reflect_identity(mrb_state *const mrb, const void *const object, const mrb_data_type *const type)
{
    reflect_identities &map = reflect_identity_map(mrb);
    for (auto [first, last] = map.equal_range(object); first != last;) {
        reflect_lifetime_base &record = *first->second;
        if (record.type != type || mrb_object_dead_p(mrb, reinterpret_cast<RBasic *>(record.ruby))) {
            ++first;
            continue;
        }
        if (reflect_alive(record)) return record.ruby;
        std::tie(first, last) = map.equal_range(object);
    }
    return nullptr;
}

inline mrb_sym reflect_reflected_key(mrb_state *const mrb)
{
    return MRB_SYM(__reflected__);
}

inline bool reflect_reflected(mrb_state *const mrb, const mrb_value v)
{
    for (RClass *c = mrb_obj_class(mrb, v); c != nullptr; c = c->super)
        if (mrb_obj_iv_defined(mrb, reinterpret_cast<RObject *>(c), reflect_reflected_key(mrb))) return true;
    return false;
}

inline reflect_lifetime_base *reflect_record(mrb_state *const mrb, const mrb_value v)
{
    if (mrb_type(v) != MRB_TT_CDATA || DATA_PTR(v) == nullptr || DATA_TYPE(v) == nullptr || !reflect_reflected(mrb, v)) return nullptr;
    return static_cast<reflect_lifetime_base *>(DATA_PTR(v));
}

template <std::size_t Count>
std::array<reflect_lifetime_base *, Count + 1> reflect_call_records(mrb_state *const mrb, const mrb_value self)
{
    std::array<reflect_lifetime_base *, Count + 1> records{reflect_record(mrb, self)};
    const std::span<const mrb_value> argv(mrb_get_argv(mrb), static_cast<std::size_t>(mrb_get_argc(mrb)));
    for (std::size_t i = 0; i < Count && i < argv.size(); i++) records.at(i + 1) = reflect_record(mrb, argv[i]);
    return records;
}

template <std::size_t Count>
using reflect_call_end = std::unique_ptr<std::array<reflect_lifetime_base *, Count + 1>, decltype([](std::array<reflect_lifetime_base *, Count + 1> *const records) {
                                             for (reflect_lifetime_base *const record : *records)
                                                 if (record != nullptr && record->alive) reflect_end_replaced_children(*record);
                                         })>;

template <class T>
constexpr bool reflect_trackable = std::is_class_v<T> && std::has_virtual_destructor_v<T> && !std::is_final_v<T>;

struct reflect_tracked_base {
    reflect_lifetime_base *record = nullptr;
};

template <class T>
struct reflect_tracked : T, reflect_tracked_base {
    template <class... A>
    explicit reflect_tracked(A &&...args) : T(std::forward<A>(args)...)
    {
    }
    ~reflect_tracked()
    {
        if (record == nullptr) return;
        reflect_end_lifetime(*record);
        record->tracked = nullptr;
    }
};

consteval bool reflect_is_unique_ptr(const std::meta::info type)
{
    const std::meta::info t = std::meta::dealias(std::meta::remove_cvref(type));
    return std::meta::has_template_arguments(t) && std::meta::template_of(t) == ^^std::unique_ptr;
}

consteval bool reflect_owns_through_pointer(const std::meta::info field)
{
    const std::meta::info bare = reflect_bare(std::meta::type_of(field));
    return !std::meta::is_reference_type(std::meta::type_of(field)) && reflect_is_unique_ptr(bare) &&
           std::meta::is_class_type(std::meta::dealias(std::meta::template_arguments_of(bare)[0]));
}

consteval bool reflect_owns(const std::meta::info field)
{
    if (std::meta::is_reference_type(std::meta::type_of(field))) return false;
    const std::meta::info bare = reflect_bare(std::meta::type_of(field));
    return std::meta::is_class_type(bare) && !reflect_is_variant(bare) && !reflect_is_shared_ptr(bare) && !reflect_is_view(bare);
}

consteval std::size_t reflect_owning_field_count(const std::meta::info type)
{
    std::vector<std::meta::info> scopes = reflect_bases(type);
    scopes.push_back(std::meta::dealias(type));
    std::size_t count = 0;
    for (const std::meta::info scope : scopes) {
        if (!std::meta::is_class_type(scope) || !std::meta::is_complete_type(scope)) continue;
        for (const std::meta::info m : std::meta::nonstatic_data_members_of(scope, std::meta::access_context::current()))
            if (std::meta::has_identifier(m) && reflect_result_supported(std::meta::type_of(m)) && reflect_owns(m)) count++;
    }
    return count;
}

template <class T>
struct reflect_lifetime : reflect_lifetime_base {
    std::array<reflect_lifetime_base *, reflect_owning_field_count(std::meta::dealias(^^T))> owning{};
};

template <class T>
const reflect_data_type &reflect_data_type_of();

template <class T>
std::span<const reflect_upcast> reflect_upcasts()
{
    static constexpr auto bases = std::define_static_array(reflect_bases(^^T));
    static const auto upcasts = [] {
        std::array<reflect_upcast, bases.size()> table{};
        std::size_t at = 0;
        template for (constexpr std::meta::info base : bases) {
            using B = [:base:];
            if constexpr (requires(T *p) { static_cast<B *>(p); })
                table[at] = {&reflect_data_type_of<B>(), [](void *const p) -> void * { return static_cast<B *>(static_cast<T *>(p)); }};
            at++;
        }
        return table;
    }();
    return upcasts;
}

template <class T>
const reflect_data_type &reflect_data_type_of()
{
    static constexpr auto name = std::define_static_string(reflect_class_name(^^T));
    static const reflect_data_type type{{name, [](mrb_state *, void *const p) {
                                             if (p == nullptr) return;
                                             reflect_lifetime_base *const record = static_cast<reflect_lifetime_base *>(p);
                                             const bool destroys = record->alive && record->owned;
                                             void *const object = record->object;
                                             reflect_end_lifetime(*record);
                                             if (destroys && record->deallocator != nullptr) record->deallocator(object);
                                             else if (destroys) {
                                                 if constexpr (std::meta::is_complete_type(^^T)) {
                                                     if constexpr (std::is_destructible_v<T>) {
                                                         if constexpr (reflect_trackable<T>) delete static_cast<reflect_tracked<T> *>(static_cast<T *>(object));
                                                         else delete static_cast<T *>(object);
                                                     }
                                                 }
                                             }
                                             if (record->tracked != nullptr) record->tracked->record = nullptr;
                                             delete static_cast<reflect_lifetime<T> *>(record);
                                         }},
                                        reflect_upcasts<T>()};
    return type;
}

template <class T>
reflect_lifetime_base &reflect_new_lifetime(mrb_state *const mrb, const mrb_value self)
{
    if (mrb_type(self) != MRB_TT_CDATA || DATA_PTR(self) != nullptr) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "the object is already initialized");
    reflect_identities &identities = reflect_identity_map(mrb);
    reflect_lifetime<T> *const record = new reflect_lifetime<T>{};
    record->children = record->owning;
    record->type = &reflect_data_type_of<T>();
    record->ruby = mrb_obj_ptr(self);
    record->mrb = mrb;
    record->identities = &identities;
    record->callbacks = &reflect_callbacks_of(mrb);
    mrb_data_init(self, record, &reflect_data_type_of<T>());
    return *record;
}

struct reflect_undefined_call : std::exception {
    const char *what() const noexcept override { return "no linked library defines this function"; }
};

struct reflect_definition {
    using registration = void (*)(reflect_definition &, RClass *);
    mrb_state *mrb;
    std::vector<std::pair<registration, RClass *>> pending;
    explicit reflect_definition(mrb_state *const state) : mrb(state) {}
    reflect_definition(const reflect_definition &) = delete;
    reflect_definition &operator=(const reflect_definition &) = delete;
    void finish()
    {
        while (!pending.empty()) {
            const auto [registration, klass] = pending.front();
            pending.erase(pending.begin());
            registration(*this, klass);
        }
    }
};

struct reflect_options {
    bool templates = false;
    bool nested_types = false;
    bool virtual_overriders = false;
};

template <std::meta::info Type>
struct reflect_virtual_overrider;

consteval bool reflect_virtual_signature_equal(const std::meta::info a, const std::meta::info b)
{
    if (std::meta::identifier_of(a) != std::meta::identifier_of(b) || std::meta::is_const(a) != std::meta::is_const(b)) return false;
    const std::vector<std::meta::info> pa = std::meta::parameters_of(a);
    const std::vector<std::meta::info> pb = std::meta::parameters_of(b);
    if (pa.size() != pb.size()) return false;
    for (std::size_t i = 0; i < pa.size(); i++)
        if (std::meta::dealias(std::meta::type_of(pa[i])) != std::meta::dealias(std::meta::type_of(pb[i]))) return false;
    return true;
}

consteval bool reflect_virtual_function_supported(const std::meta::info f)
{
    if (!std::meta::is_function(f) || !std::meta::is_virtual(f) || std::meta::is_destructor(f) || !std::meta::has_identifier(f)) return false;
    if (std::meta::is_private(f) || std::meta::is_deleted(f) || std::meta::is_volatile(f) || std::meta::is_rvalue_reference_qualified(f)) return false;
    if (std::meta::is_vararg_function(f) || std::meta::is_noexcept(f)) return false;
    for (const std::meta::info p : std::meta::parameters_of(f))
        if (!reflect_result_supported(std::meta::type_of(p))) return false;
    const std::meta::info r = std::meta::return_type_of(f);
    if (std::meta::is_reference_type(r) || std::meta::is_pointer_type(std::meta::dealias(r)) || reflect_is_view(reflect_bare(r)) ||
        reflect_bare(r) == std::meta::dealias(^^std::string_view))
        return false;
    return r == ^^void || reflect_parameter_supported(r);
}

consteval std::vector<std::meta::info> reflect_virtual_functions(const std::meta::info type)
{
    std::vector<std::meta::info> seen;
    std::vector<std::meta::info> order{std::meta::dealias(type)};
    for (std::size_t i = 0; i < order.size(); i++)
        for (const std::meta::info b : std::meta::bases_of(order[i], std::meta::access_context::unchecked()))
            if (std::meta::is_public(b) && !reflect_reserved(std::meta::type_of(b))) order.push_back(std::meta::dealias(std::meta::type_of(b)));
    for (const std::meta::info t : order)
        for (const std::meta::info m : std::meta::members_of(t, std::meta::access_context::unchecked()))
            if (std::meta::is_function(m) && std::meta::is_virtual(m) && !std::meta::is_destructor(m) && std::meta::has_identifier(m) &&
                std::ranges::none_of(seen, [&](const std::meta::info s) { return reflect_virtual_signature_equal(s, m); }))
                seen.push_back(m);
    std::vector<std::meta::info> functions;
    for (const std::meta::info f : seen) {
        if (!std::meta::is_final(f) && reflect_virtual_function_supported(f)) functions.push_back(f);
        else if (std::meta::is_pure_virtual(f)) return {};
    }
    return functions;
}

consteval bool reflect_overridable(const std::meta::info type)
{
    const std::meta::info t = std::meta::dealias(type);
    return std::meta::is_class_type(t) && std::meta::is_complete_type(t) && !std::meta::is_final_type(t) && std::meta::has_virtual_destructor(t) &&
           (std::meta::has_identifier(t) || std::meta::has_template_arguments(t)) && !reflect_virtual_functions(t).empty();
}

consteval std::string reflect_decimal(const std::size_t n)
{
    std::array<char, 24> digits{};
    return {digits.data(), std::to_chars(digits.data(), digits.data() + digits.size(), n).ptr};
}

consteval std::string reflect_virtual_overriders_text(const std::span<const std::meta::info> classes)
{
    std::string text;
    for (const std::meta::info c : classes) {
        if (!std::meta::is_type(c) || !reflect_overridable(c)) continue;
        const std::meta::info t = std::meta::dealias(c);
        const std::string display = std::string(std::meta::display_string_of(t));
        const std::vector<std::meta::info> functions = reflect_virtual_functions(t);
        text += "template <> struct mrb_cpp_reflector::reflect_virtual_overrider<^^" + display + "> {\n";
        text += "    using base = " + display + ";\n";
        text += "    static constexpr auto functions = std::define_static_array(mrb_cpp_reflector::reflect_virtual_functions(^^base));\n";
        text += "    struct type final : mrb_cpp_reflector::reflect_tracked<base> {\n";
        text += "        using overridden = base;\n";
        text += "        using mrb_cpp_reflector::reflect_tracked<base>::reflect_tracked;\n";
        text += "        mutable std::array<bool, " + reflect_decimal(functions.size()) + "> running{};\n";
        for (std::size_t j = 0; j < functions.size(); j++) {
            const std::meta::info f = functions[j];
            const std::string at = "functions[" + reflect_decimal(j) + "]";
            const std::string result = "typename [:std::meta::return_type_of(" + at + "):]";
            std::string parameters, arguments;
            const std::size_t count = std::meta::parameters_of(f).size();
            for (std::size_t k = 0; k < count; k++) {
                if (k > 0) { parameters += ", "; arguments += ", "; }
                parameters += "typename [:std::meta::type_of(std::meta::parameters_of(" + at + ")[" + reflect_decimal(k) + "]):] a" + reflect_decimal(k);
                arguments += "a" + reflect_decimal(k);
            }
            const std::string name(std::meta::identifier_of(f));
            text += "        auto " + name + "(" + parameters + ")" + (std::meta::is_const(f) ? " const" : "") +
                    (std::meta::is_lvalue_reference_qualified(f) ? " &" : "") + (std::meta::is_noexcept(f) ? " noexcept" : "") + " -> " + result + " override\n";
            text += "        {\n";
            if (std::meta::is_pure_virtual(f))
                text += "            const auto base = [&]() -> " + result + " { throw mrb_cpp_reflector::reflect_undefined_call(); };\n";
            else
                text += "            const auto base = [&]() -> " + result + " { return this->overridden::" + name + "(" + arguments + "); };\n";
            text += "            return mrb_cpp_reflector::reflect_call_virtual_overrider<" + at + ">(*this, running[" + reflect_decimal(j) + "], base" +
                    (count > 0 ? ", " + arguments : "") + ");\n";
            text += "        }\n";
        }
        text += "    };\n};\n";
    }
    return text;
}

template <auto Classes>
[[gnu::section(".mrb_cpp_reflector_virtual_overriders"), gnu::used]] static constexpr auto reflect_virtual_overriders_printed = [] {
    constexpr std::string_view body = std::define_static_string(reflect_virtual_overriders_text(Classes));
    std::array<char, body.size()> text{};
    std::ranges::copy(body, text.begin());
    return text;
}();

template <std::meta::info Type, reflect_options Options = reflect_options{}, auto Instances = std::array<std::meta::info, 0>{}>
RClass *reflect_define_class(reflect_definition &definition, RClass *super);

template <std::meta::info Type>
RClass *reflect_define_enum(reflect_definition &definition, RClass *under);

template <std::meta::info Bare>
mrb_sym reflect_class_key(mrb_state *const mrb)
{
    static constexpr std::string_view key = std::define_static_string("reflected class " + std::string(std::meta::display_string_of(Bare)));
    return mrb_intern_static(mrb, key.data(), key.size());
}

template <std::meta::info Bare>
mrb_sym reflect_module_key(mrb_state *const mrb)
{
    static constexpr std::string_view key = std::define_static_string("reflected module " + std::string(std::meta::display_string_of(Bare)));
    return mrb_intern_static(mrb, key.data(), key.size());
}

template <std::meta::info Type>
RClass *reflect_module(mrb_state *const mrb)
{
    const mrb_value found = mrb_iv_get(mrb, mrb_obj_value(mrb->object_class), reflect_module_key<std::meta::dealias(std::meta::remove_cvref(Type))>(mrb));
    return mrb_nil_p(found) ? nullptr : mrb_class_ptr(found);
}

template <std::meta::info Type>
RClass *reflect_class(reflect_definition &definition)
{
    mrb_state *const mrb = definition.mrb;
    constexpr std::meta::info bare = std::meta::dealias(std::meta::remove_cvref(Type));
    const mrb_sym key = reflect_class_key<bare>(mrb);
    if (!mrb_iv_defined(mrb, mrb_obj_value(mrb->object_class), key)) {
        if constexpr (std::meta::is_enum_type(bare)) reflect_define_enum<bare>(definition, mrb->object_class);
        else reflect_define_class<bare>(definition, mrb->object_class);
    }
    return mrb_class_ptr(mrb_iv_get(mrb, mrb_obj_value(mrb->object_class), key));
}

template <std::meta::info Type>
RClass *reflect_class(mrb_state *const mrb)
{
    reflect_definition definition(mrb);
    RClass *const klass = reflect_class<Type>(definition);
    definition.finish();
    return klass;
}

template <class T>
T *reflect_ptr(mrb_state *const mrb, const mrb_value v)
{
    if (mrb_type(v) != MRB_TT_CDATA || DATA_PTR(v) == nullptr || DATA_TYPE(v) == nullptr) return nullptr;
    if (DATA_TYPE(v) != &reflect_data_type_of<T>() && !reflect_reflected(mrb, v)) [[unlikely]] return nullptr;
    const reflect_data_type *const type = static_cast<const reflect_data_type *>(DATA_TYPE(v));
    reflect_lifetime_base &record = *static_cast<reflect_lifetime_base *>(DATA_PTR(v));
    if (type == &reflect_data_type_of<T>()) {
        if (!reflect_alive(record)) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "the lifetime of the C++ object has ended");
        return static_cast<T *>(type->object_of(&record));
    }
    for (const reflect_upcast &upcast : type->upcasts)
        if (upcast.base == &reflect_data_type_of<T>()) {
            if (!reflect_alive(record)) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "the lifetime of the C++ object has ended");
            return static_cast<T *>(upcast.to_base(type->object_of(&record)));
        }
    return nullptr;
}

template <class T>
T &reflect_receiver_or_raise(mrb_state *const mrb, const mrb_value self)
{
    T *const object = reflect_ptr<T>(mrb, self);
    if (object == nullptr) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
    return *object;
}

inline const mrb_data_type &reflect_data_type_share()
{
    static const mrb_data_type type{"shared", [](mrb_state *, void *const p) { delete static_cast<std::shared_ptr<void> *>(p); }};
    return type;
}

inline mrb_sym reflect_share_key(mrb_state *const mrb)
{
    return MRB_SYM(__reflected_share__);
}

template <class E>
std::shared_ptr<E> reflect_share_of(mrb_state *const mrb, const mrb_value v)
{
    if (mrb_type(v) != MRB_TT_CDATA) return nullptr;
    const mrb_value keeper = mrb_iv_get(mrb, v, reflect_share_key(mrb));
    if (mrb_nil_p(keeper)) return nullptr;
    E *const p = reflect_ptr<E>(mrb, v);
    if (p == nullptr) return nullptr;
    return std::shared_ptr<E>(*static_cast<std::shared_ptr<void> *>(DATA_PTR(keeper)), p);
}

template <class T>
bool reflect_shares(mrb_state *const mrb, const mrb_value v)
{
    if constexpr (reflect_is_shared_ptr(^^T)) return mrb_nil_p(v) || reflect_share_of<typename T::element_type>(mrb, v) != nullptr;
    else return false;
}

template <class T, class... A>
T *reflect_new(A &&...args)
{
    if constexpr (reflect_trackable<T>) return new reflect_tracked<T>(std::forward<A>(args)...);
    else return new T(std::forward<A>(args)...);
}

template <class T>
void reflect_adopt(mrb_state *const mrb, const mrb_value self, reflect_lifetime_base &record, T *const made)
{
    record.object = made;
    record.owned = true;
    record.adopted = true;
    record.alive = true;
    if constexpr (reflect_trackable<T>) {
        reflect_tracked<T> *const tracked = static_cast<reflect_tracked<T> *>(made);
        tracked->record = &record;
        record.tracked = tracked;
    }
    mrb_iv_remove(mrb, self, reflect_share_key(mrb));
    reflect_identity_set(record);
}

template <class T>
mrb_value reflect_object(mrb_state *const mrb, T &&value, const bool frozen = false)
{
    using U = std::remove_cvref_t<T>;
    RData *const data = mrb_data_object_alloc(mrb, reflect_class<std::meta::dealias(std::meta::remove_cvref(^^U))>(mrb), nullptr, &reflect_data_type_of<U>());
    const mrb_value object = mrb_obj_value(data);
    reflect_lifetime_base &record = reflect_new_lifetime<U>(mrb, object);
    reflect_adopt<U>(mrb, object, record, reflect_new<U>(std::forward<T>(value)));
    if (frozen) mrb_obj_freeze(mrb, object);
    return object;
}

template <class T>
RObject *reflect_known(mrb_state *const mrb, T *const object)
{
    if constexpr (!std::meta::is_complete_type(^^T)) {
    } else if constexpr (std::is_polymorphic_v<T>) {
        if (const reflect_tracked_base *const made = dynamic_cast<const reflect_tracked_base *>(object); made != nullptr && made->record != nullptr) {
            reflect_lifetime_base &record = *made->record;
            if (record.mrb == mrb && record.alive && !mrb_object_dead_p(mrb, reinterpret_cast<RBasic *>(record.ruby))) return record.ruby;
        }
    }
    return reflect_identity(mrb, object, &reflect_data_type_of<std::remove_cv_t<T>>());
}

template <class T>
mrb_value reflect_borrowed(mrb_state *const mrb, T *const ref, const mrb_value owner, const bool frozen)
{
    if (RObject *const known = reflect_known(mrb, ref); known != nullptr) return mrb_obj_value(known);
    RData *const data = mrb_data_object_alloc(mrb, reflect_class<std::meta::dealias(std::meta::remove_cvref(^^T))>(mrb), nullptr, &reflect_data_type_of<T>());
    const mrb_value object = mrb_obj_value(data);
    reflect_lifetime_base &record = reflect_new_lifetime<T>(mrb, object);
    record.object = ref;
    record.alive = true;
    mrb_iv_set(mrb, object, reflect_sym<kOwner>(mrb), owner);
    if (frozen) mrb_obj_freeze(mrb, object);
    reflect_identity_set(record);
    return object;
}

template <class T>
constexpr bool reflect_from_mrb = [] {
    namespace vc = mrbcpp::value_converter;
    if constexpr (!vc::convertible_from_mrb<T> || reflect_is_view(^^T)) return false;
    else if constexpr (vc::is_std_optional<T>::value || vc::is_std_vector<T>::value || vc::is_std_array<T>::value || vc::is_set_like_v<T>)
        return reflect_from_mrb<std::remove_cv_t<typename T::value_type>>;
    else if constexpr (vc::is_std_pair<T>::value)
        return reflect_from_mrb<std::remove_cv_t<typename T::first_type>> && reflect_from_mrb<std::remove_cv_t<typename T::second_type>>;
    else if constexpr (vc::is_map_like_v<T>)
        return reflect_from_mrb<std::remove_cv_t<typename T::key_type>> && reflect_from_mrb<std::remove_cv_t<typename T::mapped_type>>;
    else return true;
}();

template <class T, bool Move = false, bool Pointer = false, bool Mutates = false>
struct reflect_holder {
    static constexpr bool moves = Move;
    static constexpr bool pointer = Pointer;
    std::unique_ptr<T> temporary;
    T *ptr = nullptr;
    mrb_value lent = mrb_undef_value();
    void resolve(mrb_state *const mrb)
    {
        if (mrb_undef_p(lent)) return;
        if constexpr (Mutates) mrb_check_frozen(mrb, mrb_obj_ptr(lent));
        ptr = reflect_ptr<T>(mrb, lent);
        if (ptr == nullptr) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "wrong argument");
    }
};

struct reflect_gc_root {
    mrb_state *mrb;
    mrb_value object;
    std::thread::id thread;
    reflect_callbacks *callbacks;
    bool released = false;
    reflect_gc_root(mrb_state *const state, const mrb_value v) : mrb(state), object(v), thread(reflect_callbacks_of(state).thread), callbacks(&reflect_callbacks_of(state))
    {
        callbacks->unregister_released(mrb);
        callbacks->roots.insert(this);
        mrb_gc_register(mrb, object);
    }
    reflect_gc_root(const reflect_gc_root &) = delete;
    reflect_gc_root &operator=(const reflect_gc_root &) = delete;
};

inline void reflect_callbacks::unregister_released(mrb_state *const mrb)
{
    std::erase_if(roots, [mrb](reflect_gc_root *const root) {
        if (!root->released) return false;
        mrb_gc_unregister(mrb, root->object);
        delete root;
        return true;
    });
}

template <class R>
mrb_value reflect_result(mrb_state *mrb, mrb_value self, R &&value);

template <class E>
mrb_value reflect_enumerator(mrb_state *mrb, E value);

template <class Q>
Q *reflect_void_ptr(mrb_state *const mrb, const mrb_value v)
{
    if (mrb_nil_p(v)) return nullptr;
    if (void *const p = mrb_data_check_get_ptr(mrb, v, &mrb_void_pointer_type); p != nullptr) return p;
    if constexpr (std::is_const_v<Q>) {
        if (void *const p = mrb_data_check_get_ptr(mrb, v, &mrb_const_void_pointer_type); p != nullptr) [[likely]] return p;
        mrb_raise(mrb, E_TYPE_ERROR, "VoidPointer or ConstVoidPointer wanted");
    } else mrb_raise(mrb, E_TYPE_ERROR, "VoidPointer wanted");
    std::unreachable();
}

template <class Signature>
struct reflect_callable;

template <class R, class... A>
struct reflect_callable<R(A...)> {
    std::shared_ptr<reflect_gc_root> root;
    R operator()(A... args) const
    {
        if (std::this_thread::get_id() != root->thread) [[unlikely]] reflect_abort_from_other_thread();
        if (root->callbacks == nullptr) [[unlikely]] {
            if constexpr (std::is_void_v<R>) return;
            else if constexpr (std::is_default_constructible_v<R>) return R{};
            else throw std::bad_function_call();
        }
        mrb_state *const mrb = root->mrb;
        if (mrb->gc.collecting) [[unlikely]] reflect_abort_during_collection();
        const std::array<mrb_value, sizeof...(A)> argv{reflect_result(mrb, mrb_nil_value(), std::forward<A>(args))...};
        const mrb_value answer = mrb_proc_p(root->object) ? mrb_yield_argv(mrb, root->object, static_cast<mrb_int>(argv.size()), argv.data())
                                                          : mrb_funcall_argv(mrb, root->object, MRB_SYM(call), static_cast<mrb_int>(argv.size()), argv.data());
        if constexpr (std::is_void_v<R>) return;
        else if constexpr (std::is_pointer_v<R> && std::is_void_v<std::remove_pointer_t<R>>) return reflect_void_ptr<std::remove_pointer_t<R>>(mrb, answer);
        else if constexpr (std::is_class_v<std::remove_cvref_t<R>> && !reflect_from_mrb<std::remove_cvref_t<R>>) {
            std::remove_cvref_t<R> *const p = reflect_ptr<std::remove_cvref_t<R>>(mrb, answer);
            if (p == nullptr) mrb_raise(mrb, E_TYPE_ERROR, "call answered the wrong type");
            return *p;
        } else return mrb_value_to<std::remove_cvref_t<R>>(mrb, answer);
    }
};

template <class T>
std::unique_ptr<T> reflect_function_from(mrb_state *const mrb, const mrb_value v)
{
    if (!mrb_respond_to(mrb, v, MRB_SYM(call))) return nullptr;
    using Signature = [:std::meta::template_arguments_of(std::meta::dealias(^^T))[0]:];
    return std::make_unique<T>(reflect_callable<Signature>{std::shared_ptr<reflect_gc_root>(new reflect_gc_root(mrb, v), [](reflect_gc_root *const root) {
        if (std::this_thread::get_id() != root->thread) [[unlikely]] reflect_abort_from_other_thread();
        if (root->callbacks == nullptr) delete root;
        else root->released = true;
    })});
}

template <class T>
std::unique_ptr<T> reflect_implicit_conversion(mrb_state *mrb, mrb_value v);

template <class T>
bool reflect_implicitly_converts(mrb_state *mrb, mrb_value v);

template <std::meta::info Function, bool Converting = true>
bool reflect_get_args_match(mrb_state *mrb, std::span<const mrb_value> argv);

template <class A>
bool reflect_variant_exact(mrb_state *const mrb, const mrb_value v)
{
    if constexpr (std::same_as<A, bool>) return mrb_true_p(v) || mrb_false_p(v);
    else if constexpr (std::integral<A>) return mrb_integer_p(v);
    else if constexpr (std::floating_point<A>) return mrb_float_p(v);
    else if constexpr (std::same_as<A, std::string>) return mrb_string_p(v);
    else if constexpr (std::is_class_v<A> || std::is_enum_v<A>) return reflect_ptr<A>(mrb, v) != nullptr;
    else return false;
}

template <class A>
A reflect_variant_alternative(mrb_state *const mrb, const mrb_value v)
{
    if constexpr ((std::is_class_v<A> || std::is_enum_v<A>) && !std::same_as<A, std::string>) return *reflect_ptr<A>(mrb, v);
    else return mrb_value_to<A>(mrb, v);
}

template <class T>
std::optional<T> reflect_variant_from(mrb_state *const mrb, const mrb_value v)
{
    std::optional<T> made;
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        const auto exact = [&]<std::size_t J>() {
            using A = std::variant_alternative_t<J, T>;
            if (!made && reflect_variant_exact<A>(mrb, v)) made.emplace(std::in_place_index<J>, reflect_variant_alternative<A>(mrb, v));
        };
        const auto converted = [&]<std::size_t J>() {
            using A = std::variant_alternative_t<J, T>;
            namespace vc = mrbcpp::value_converter;
            if (made) return;
            mrb_value c = mrb_nil_value();
            const bool numeric = mrb_obj_is_kind_of(mrb, v, mrb_class_get_id(mrb, MRB_SYM(Numeric)));
            if constexpr (std::integral<A> && !std::same_as<A, bool>) {
                if (numeric) c = mrb_ensure_integer_type(mrb, v);
            } else if constexpr (std::floating_point<A>) {
                if (numeric) c = mrb_ensure_float_type(mrb, v);
            } else if constexpr (std::same_as<A, std::string>) c = mrb_type_convert_check(mrb, v, MRB_TT_STRING, MRB_SYM(to_str));
            else if constexpr (vc::is_std_vector<A>::value && reflect_from_mrb<A>) c = mrb_type_convert_check(mrb, v, MRB_TT_ARRAY, MRB_SYM(to_ary));
            else if constexpr (vc::is_map_like_v<A> && reflect_from_mrb<A>) c = mrb_type_convert_check(mrb, v, MRB_TT_HASH, MRB_SYM(to_hash));
            if constexpr (reflect_from_mrb<A>)
                if (!mrb_nil_p(c)) made.emplace(std::in_place_index<J>, mrb_value_to<A>(mrb, c));
        };
        (exact.template operator()<I>(), ...);
        (converted.template operator()<I>(), ...);
    }(std::make_index_sequence<std::variant_size_v<T>>{});
    return made;
}

template <class F>
struct reflect_function_pointer {
    F function;
};

template <class F>
const mrb_data_type &reflect_function_pointer_type()
{
    static constexpr auto name = std::define_static_string(std::meta::display_string_of(^^F));
    static const mrb_data_type type{name, [](mrb_state *, void *const p) { delete static_cast<reflect_function_pointer<F> *>(p); }};
    return type;
}

template <class T, bool Mutates>
struct reflect_opaque_argument {
    T *ptr = nullptr;
    mrb_value lent = mrb_undef_value();
    void resolve(mrb_state *const mrb)
    {
        if (mrb_undef_p(lent)) return;
        if constexpr (Mutates) mrb_check_frozen(mrb, mrb_obj_ptr(lent));
        ptr = reflect_ptr<std::remove_const_t<T>>(mrb, lent);
        if (ptr == nullptr) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "wrong argument");
    }
};

template <class T>
struct reflect_output {
    T *value = nullptr;
    reflect_lifetime_base *made = nullptr;
    reflect_output(reflect_lifetime_base &record) : made(&record) {}
    reflect_output(reflect_output &&other) noexcept : value(other.value), made(std::exchange(other.made, nullptr)) {}
    reflect_output &operator=(reflect_output &&) = delete;
    ~reflect_output()
    {
        if (made != nullptr) made->object = const_cast<void *>(static_cast<const void *>(value));
    }
};

template <std::meta::info ParameterType, bool Converting = true>
auto reflect_argument(mrb_state *const mrb, const mrb_value v)
{
    constexpr std::meta::info type = ParameterType;
    using T = [:reflect_bare(type):];
    if constexpr (reflect_is_variant(type)) {
        std::optional<T> made = reflect_variant_from<T>(mrb, v);
        if (!made) [[unlikely]] mrb_raisef(mrb, E_TYPE_ERROR, "%T fits no alternative of the variant", v);
        return std::move(*made);
    } else if constexpr (reflect_is_output_parameter(type)) {
        using P = [:std::meta::remove_pointer(std::meta::remove_pointer(std::meta::dealias(type))):];
        using U = std::remove_const_t<P>;
        if (!mrb_nil_p(v)) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "an output parameter takes nil");
        RData *const data = mrb_data_object_alloc(mrb, reflect_class<std::meta::dealias(^^U)>(mrb), nullptr, &reflect_data_type_of<U>());
        const mrb_value object = mrb_obj_value(data);
        reflect_lifetime_base &record = reflect_new_lifetime<U>(mrb, object);
        reflect_lifetimes_of(mrb).output = object;
        return reflect_output<P>(record);
    } else if constexpr (reflect_is_function_pointer(type)) {
        using F = [:std::meta::dealias(std::meta::remove_cv(type)):];
        void *const p = mrb_data_check_get_ptr(mrb, v, &reflect_function_pointer_type<F>());
        if (p == nullptr) [[unlikely]] mrb_raisef(mrb, E_TYPE_ERROR, "a FunctionPointer to %s wanted", reflect_function_pointer_type<F>().struct_name);
        return static_cast<const reflect_function_pointer<F> *>(p)->function;
    } else if constexpr (reflect_is_opaque_pointer(type)) {
        using P = [:std::meta::remove_pointer(std::meta::dealias(type)):];
        reflect_opaque_argument<P, reflect_mutates(type)> held;
        if (mrb_nil_p(v)) return held;
        if (reflect_ptr<std::remove_const_t<P>>(mrb, v) == nullptr) [[unlikely]]
            mrb_raisef(mrb, E_TYPE_ERROR, "%s wanted", std::define_static_string(reflect_class_name(std::meta::dealias(std::meta::remove_cv(^^P)))));
        held.lent = v;
        return held;
    } else
    if constexpr (std::meta::is_pointer_type(std::meta::dealias(type))) {
        using P = [:std::meta::dealias(std::meta::remove_cv(std::meta::remove_pointer(std::meta::dealias(type)))):];
        if constexpr (std::is_void_v<P>) return reflect_void_ptr<typename [:std::meta::remove_pointer(std::meta::dealias(type)):]>(mrb, v);
        else {
            reflect_holder<P, false, true, reflect_mutates(type)> held;
            if (mrb_nil_p(v)) return held;
            if (reflect_ptr<P>(mrb, v) == nullptr) mrb_raisef(mrb, E_TYPE_ERROR, "%s wanted", std::define_static_string(reflect_class_name(std::meta::dealias(^^P))));
            held.lent = v;
            return held;
        }
    } else if constexpr (std::meta::is_lvalue_reference_type(type)) {
        reflect_holder<T, false, false, reflect_mutates(type)> held;
        if (reflect_ptr<T>(mrb, v) != nullptr) {
            held.lent = v;
        } else if constexpr (!reflect_mutates(type) && reflect_from_mrb<T> && std::is_move_constructible_v<T>) {
            held.temporary = std::make_unique<T>(mrb_value_to<T>(mrb, v));
            held.ptr = held.temporary.get();
        } else {
            if constexpr (reflect_is_shared_ptr(type) && !reflect_mutates(type)) {
                if (mrb_nil_p(v)) held.temporary = std::make_unique<T>();
                else if (T share = reflect_share_of<typename T::element_type>(mrb, v); share != nullptr) held.temporary = std::make_unique<T>(std::move(share));
            } else if constexpr (reflect_is_function(type) && !reflect_mutates(type)) held.temporary = reflect_function_from<T>(mrb, v);
            else if constexpr (Converting && !reflect_mutates(type)) held.temporary = reflect_implicit_conversion<T>(mrb, v);
            if (held.temporary == nullptr) mrb_raisef(mrb, E_TYPE_ERROR, "%s wanted", std::define_static_string(reflect_class_name(std::meta::dealias(^^T))));
            held.ptr = held.temporary.get();
        }
        return held;
    } else {
        reflect_holder<T, true> held;
        if (T *const p = reflect_ptr<T>(mrb, v); p != nullptr) {
            held.temporary = std::make_unique<T>(*p);
        } else if constexpr (reflect_from_mrb<T>) {
            held.temporary = std::make_unique<T>(mrb_value_to<T>(mrb, v));
        } else {
            if constexpr (reflect_is_shared_ptr(type)) {
                if (mrb_nil_p(v)) held.temporary = std::make_unique<T>();
                else if (T share = reflect_share_of<typename T::element_type>(mrb, v); share != nullptr) held.temporary = std::make_unique<T>(std::move(share));
            } else if constexpr (reflect_is_function(type)) held.temporary = reflect_function_from<T>(mrb, v);
            else if constexpr (Converting) held.temporary = reflect_implicit_conversion<T>(mrb, v);
            if (held.temporary == nullptr) mrb_raisef(mrb, E_TYPE_ERROR, "%s wanted", std::define_static_string(reflect_class_name(std::meta::dealias(^^T))));
        }
        held.ptr = held.temporary.get();
        return held;
    }
}

template <class V>
struct reflect_lent_string {
    V value;
    mrb_state *mrb;
    mrb_value copy;
    reflect_lent_string(mrb_state *const state, const mrb_value string, const V view) : value(view), mrb(state), copy(string) { mrb_gc_register(mrb, copy); }
    reflect_lent_string(reflect_lent_string &&other) noexcept : value(other.value), mrb(other.mrb), copy(std::exchange(other.copy, mrb_nil_value())) {}
    reflect_lent_string &operator=(reflect_lent_string &&) = delete;
    ~reflect_lent_string() { mrb_gc_unregister(mrb, copy); }
};

template <class H>
void reflect_resolve(mrb_state *const mrb, H &held)
{
    if constexpr (requires { held.resolve(mrb); }) held.resolve(mrb);
}

template <class H>
decltype(auto) reflect_pass(H &held)
{
    if constexpr (requires { held.value; held.made; }) return &held.value;
    else if constexpr (requires { held.value; held.copy; }) return held.value;
    else if constexpr (requires { held.ptr; held.temporary; }) {
        if constexpr (H::pointer) return held.ptr;
        else if constexpr (H::moves) return std::move(*held.ptr);
        else return (*held.ptr);
    } else if constexpr (requires { held.ptr; held.lent; }) return held.ptr;
    else return std::move(held);
}

struct reflect_attributes {
    bool nonnull_all = false;
    std::span<const int> nonnull;
};

template <std::meta::info Function>
inline constexpr reflect_attributes reflect_attributes_of{};

template <std::meta::info Function>
consteval bool reflect_refuses_nil(const std::size_t parameter)
{
    constexpr reflect_attributes attributes = reflect_attributes_of<Function>;
    const std::meta::info type = std::meta::type_of(std::meta::parameters_of(Function)[parameter]);
    if (!std::meta::is_pointer_type(std::meta::dealias(type))) return false;
    return attributes.nonnull_all || std::ranges::contains(attributes.nonnull, static_cast<int>(parameter + 1));
}

template <std::meta::info Function, std::size_t Skip = 0, std::size_t Count = std::meta::parameters_of(Function).size() - Skip>
consteval auto reflect_get_args_format()
{
    const std::vector<std::meta::info> parameters = std::meta::parameters_of(Function);
    std::array<char, Count + 1> format{};
    std::size_t at = 0;
    for (std::size_t i = Skip; i < Skip + Count; i++) {
        const char letter = reflect_get_args_letter(std::meta::type_of(parameters[i]));
        if (letter == '\0') throw "no mrb_get_args format for this parameter type";
        format[at++] = letter;
    }
    return format;
}

template <std::meta::info Function, std::size_t Skip = 0, std::size_t Count = std::meta::parameters_of(Function).size() - Skip>
auto reflect_get_args(mrb_state *const mrb)
{
    [[maybe_unused]] constexpr auto format = reflect_get_args_format<Function, Skip, Count>();
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
        constexpr auto retrieving_type = []<std::meta::info P>() consteval {
            using T = [:reflect_bare(std::meta::type_of(P)):];
            if constexpr (reflect_is_object(std::meta::type_of(P)) || (std::meta::is_pointer_type(std::meta::dealias(std::meta::type_of(P))) && !std::same_as<T, const char *>))
                return std::type_identity<mrb_value>{};
            else if constexpr (std::same_as<T, std::string_view> || std::same_as<T, std::string>) return std::type_identity<std::pair<const char *, mrb_int>>{};
            else if constexpr (std::same_as<T, std::span<const mrb_value>>) return std::type_identity<std::pair<const mrb_value *, mrb_int>>{};
            else if constexpr (std::same_as<T, bool>) return std::type_identity<mrb_bool>{};
            else if constexpr (std::integral<T>) return std::type_identity<mrb_int>{};
            else if constexpr (std::floating_point<T>) return std::type_identity<mrb_float>{};
            else return std::type_identity<T>{};
        };
        std::tuple<typename decltype(retrieving_type.template operator()<std::meta::parameters_of(Function)[I + Skip]>())::type...> retrieved{};
        std::apply([&](auto *const... p) { ::mrb_get_args(mrb, format.data(), p...); },
                   std::tuple_cat([&]<std::size_t J>() {
                       auto &s = std::get<J>(retrieved);
                       if constexpr (requires { s.first; s.second; }) return std::tuple{&s.first, &s.second};
                       else return std::tuple{&s};
                   }.template operator()<I>()...));
        const auto converted = [&]<std::size_t J>() {
            auto &s = std::get<J>(retrieved);
            constexpr std::meta::info P = std::meta::parameters_of(Function)[J + Skip];
            using T = [:reflect_bare(std::meta::type_of(P)):];
            const std::span<const mrb_value> argv(mrb_get_argv(mrb), static_cast<std::size_t>(mrb_get_argc(mrb)));
            if constexpr (reflect_refuses_nil<Function>(J + Skip)) {
                if (mrb_nil_p(argv[J])) [[unlikely]] mrb_raisef(mrb, E_TYPE_ERROR, "parameter %d of %n is nonnull", static_cast<int>(J + Skip + 1), mrb_get_mid(mrb));
            }
            if constexpr (std::same_as<std::remove_cvref_t<decltype(s)>, mrb_value> && !std::same_as<T, mrb_value>) return reflect_argument<std::meta::type_of(P)>(mrb, s);
            else if constexpr (std::same_as<T, std::string_view>) {
                const mrb_value copy = mrb_str_byte_subseq(mrb, argv[J], 0, RSTRING_LEN(argv[J]));
                return reflect_lent_string<std::string_view>(mrb, copy, std::string_view(RSTRING_PTR(copy), static_cast<std::size_t>(RSTRING_LEN(copy))));
            } else if constexpr (std::same_as<T, const char *>) {
                const mrb_value copy = mrb_str_new(mrb, RSTRING_PTR(argv[J]), RSTRING_LEN(argv[J]));
                return reflect_lent_string<const char *>(mrb, copy, RSTRING_PTR(copy));
            }
            else if constexpr (std::same_as<T, std::string>) return std::string(RSTRING_PTR(argv[J]), static_cast<std::size_t>(RSTRING_LEN(argv[J])));
            else if constexpr (std::same_as<T, std::span<const mrb_value>>) {
                reflect_holder<std::vector<mrb_value>> held;
                held.temporary = std::make_unique<std::vector<mrb_value>>(std::from_range, argv.subspan(J));
                held.ptr = held.temporary.get();
                return held;
            }
            else if constexpr (std::integral<T> && !std::same_as<T, bool>) {
                if ((std::is_unsigned_v<T> && s < 0) || static_cast<mrb_int>(static_cast<T>(s)) != s) [[unlikely]] mrb_raisef(mrb, E_RANGE_ERROR, "integer %i does not fit", s);
                return static_cast<T>(s);
            } else if constexpr (std::floating_point<T> && sizeof(T) < sizeof(s)) {
                if (std::isfinite(s) && std::abs(s) > std::numeric_limits<T>::max()) [[unlikely]] mrb_raisef(mrb, E_RANGE_ERROR, "float %f does not fit", s);
                return static_cast<T>(s);
            } else return static_cast<T>(s);
        };
        std::tuple<decltype(converted.template operator()<I>())...> args(converted.template operator()<I>()...);
        (reflect_resolve(mrb, std::get<I>(args)), ...);
        return args;
    }(std::make_index_sequence<Count>{});
}

template <class P>
mrb_value reflect_shared_from(mrb_state *mrb, P *object);

template <class P>
mrb_value reflect_reference(mrb_state *const mrb, P *const object)
{
    using Q = std::remove_const_t<P>;
    if constexpr (!std::is_class_v<Q>) return reflect_result(mrb, mrb_nil_value(), static_cast<Q>(*object));
    else {
        if (const mrb_value shared = reflect_shared_from(mrb, object); !mrb_undef_p(shared)) return shared;
        if (RObject *const known = reflect_known(mrb, const_cast<Q *>(object)); known != nullptr) {
            if constexpr (!std::is_const_v<P> || !std::is_copy_constructible_v<Q> || std::is_abstract_v<Q>) return mrb_obj_value(known);
            else if (mrb_frozen_p(known)) return mrb_obj_value(known);
        }
        if constexpr (std::is_copy_constructible_v<Q> && !std::is_abstract_v<Q>) return reflect_object(mrb, static_cast<const Q &>(*object), true);
        else {
            mrb_raisef(mrb, E_TYPE_ERROR, "%s is kept by C++ and cannot be kept alive from Ruby", std::define_static_string(reflect_class_name(^^Q)));
            std::unreachable();
        }
    }
}

template <class P>
mrb_value reflect_shared_from(mrb_state *const mrb, P *const object)
{
    if constexpr (requires { object->weak_from_this().lock(); }) {
        if (auto whole = object->weak_from_this().lock(); whole != nullptr) return reflect_result(mrb, mrb_nil_value(), std::shared_ptr<P>(std::move(whole), object));
    }
    return mrb_undef_value();
}

template <class R>
mrb_value reflect_result(mrb_state *const mrb, const mrb_value self, R &&value)
{
    using T = std::remove_cvref_t<R>;
    constexpr bool frozen = std::is_const_v<std::remove_reference_t<R>>;
    if constexpr (reflect_is_shared_ptr(^^T)) {
        using E = typename T::element_type;
        if (value == nullptr) return mrb_nil_value();
        const mrb_value object = reflect_borrowed<std::remove_const_t<E>>(mrb, const_cast<std::remove_const_t<E> *>(value.get()), mrb_nil_value(), std::is_const_v<E>);
        if (mrb_nil_p(mrb_iv_get(mrb, object, reflect_share_key(mrb)))) {
            RData *const keeper = mrb_data_object_alloc(mrb, mrb->object_class, new std::shared_ptr<void>(std::const_pointer_cast<std::remove_const_t<E>>(value)), &reflect_data_type_share());
            mrb_iv_set(mrb, object, reflect_share_key(mrb), mrb_obj_value(keeper));
        }
        return object;
    }
    if constexpr (reflect_is_function(^^T)) {
        using Signature = [:std::meta::template_arguments_of(std::meta::dealias(^^T))[0]:];
        if (const reflect_callable<Signature> *const made = value.template target<reflect_callable<Signature>>(); made != nullptr) return made->root->object;
    }
    if constexpr (std::same_as<T, mrb_value>) return value;
    else if constexpr (reflect_is_variant(^^T))
        return std::visit([&](const auto &held) { return reflect_result(mrb, mrb_nil_value(), std::remove_cvref_t<decltype(held)>(held)); }, value);
    else if constexpr (std::is_enum_v<T>) return reflect_enumerator(mrb, static_cast<T>(value));
    else if constexpr (std::same_as<T, bool> || std::is_arithmetic_v<T>) return cpp_to_mrb_value(mrb, value);
    else if constexpr (std::same_as<T, std::strong_ordering> || std::same_as<T, std::weak_ordering> || std::same_as<T, std::partial_ordering>)
        return value < 0 ? mrb_fixnum_value(-1) : value > 0 ? mrb_fixnum_value(1) : value == 0 ? mrb_fixnum_value(0) : mrb_nil_value();
    else if constexpr (std::is_pointer_v<T> && std::is_function_v<std::remove_pointer_t<T>>) {
        if (value == nullptr) return mrb_nil_value();
        std::unique_ptr<reflect_function_pointer<T>> made(new reflect_function_pointer<T>{value});
        RData *const data = mrb_data_object_alloc(mrb, mrb_class_get_id(mrb, MRB_SYM(FunctionPointer)), made.get(), &reflect_function_pointer_type<T>());
        made.release();
        return mrb_obj_value(data);
    } else if constexpr (std::is_pointer_v<T>) {
        using P = std::remove_cv_t<std::remove_pointer_t<T>>;
        if constexpr (std::is_void_v<P>) {
            if (value == nullptr) return mrb_nil_value();
            constexpr bool constant = std::is_const_v<std::remove_pointer_t<T>>;
            RClass *const klass = mrb_class_get_id(mrb, constant ? MRB_SYM(ConstVoidPointer) : MRB_SYM(VoidPointer));
            return mrb_obj_value(mrb_data_object_alloc(mrb, klass, const_cast<void *>(static_cast<const void *>(value)),
                                                       constant ? &mrb_const_void_pointer_type : &mrb_void_pointer_type));
        } else if constexpr (std::same_as<P, char>) return value == nullptr ? mrb_nil_value() : mrb_str_new_cstr(mrb, value);
        else if constexpr (!std::meta::is_complete_type(^^P)) {
            if (value == nullptr) return mrb_nil_value();
            RData *const data = mrb_data_object_alloc(mrb, reflect_class<std::meta::dealias(^^P)>(mrb), nullptr, &reflect_data_type_of<P>());
            const mrb_value object = mrb_obj_value(data);
            reflect_new_lifetime<P>(mrb, object).object = const_cast<P *>(value);
            return object;
        }
        else if constexpr (mrbcpp::value_converter::is_std_pair<P>::value) return value == nullptr ? mrb_nil_value() : reflect_result(mrb, self, *value);
        else return value == nullptr ? mrb_nil_value() : reflect_reference(mrb, value);
    } else if constexpr (mrbcpp::value_converter::is_std_pair<T>::value) {
        const mrb_value pair = mrb_ary_new_capa(mrb, 2);
        mrb_ary_push(mrb, pair, reflect_result(mrb, self, value.first));
        mrb_ary_push(mrb, pair, reflect_result(mrb, self, value.second));
        return pair;
    } else if constexpr (reflect_is_view(^^T)) {
        using E = std::ranges::range_value_t<T>;
        if constexpr (std::same_as<std::remove_cv_t<E>, char> && std::ranges::contiguous_range<T> && std::ranges::sized_range<T>) {
            const std::string copied(std::ranges::data(value), std::ranges::size(value));
            return mrb_str_new(mrb, copied.data(), static_cast<mrb_int>(copied.size()));
        } else {
            std::vector<E> copied = std::ranges::to<std::vector<E>>(value);
            const mrb_value array = mrb_ary_new_capa(mrb, static_cast<mrb_int>(copied.size()));
            for (auto &&element : copied) mrb_ary_push(mrb, array, reflect_result(mrb, mrb_nil_value(), E(std::move(element))));
            return array;
        }
    } else if constexpr (std::is_lvalue_reference_v<R>) return reflect_reference(mrb, &value);
    else return reflect_object(mrb, std::move(value), frozen);
}

template <class Call>
mrb_value reflect_translate_exceptions(mrb_state *const mrb, const Call &call)
{
    RClass *kind = nullptr;
    int system_errno = 0;
    std::string what;
    try {
        return call();
    } catch (mrb_jmpbuf *) {
        throw;
#if defined(__GLIBCXX__)
    } catch (abi::__forced_unwind &) {
        throw;
#endif
    } catch (const reflect_undefined_call &e) {
        kind = E_NOTIMP_ERROR;
        what = e.what();
    } catch (const std::bad_alloc &e) {
        kind = mrb_exc_get_id(mrb, MRB_ERROR_SYM(NoMemoryError));
        what = e.what();
    } catch (const std::domain_error &e) {
#if defined(MRB_NO_FLOAT)
        kind = E_RANGE_ERROR;
#else
        kind = E_FLOATDOMAIN_ERROR;
#endif
        what = e.what();
    } catch (const std::invalid_argument &e) {
        kind = E_ARGUMENT_ERROR;
        what = e.what();
    } catch (const std::filesystem::filesystem_error &e) {
        if (mrb_class_defined_id(mrb, MRB_SYM(IOError))) kind = mrb_class_get_id(mrb, MRB_SYM(IOError));
        else if (e.code().category() == std::generic_category() || e.code().category() == std::system_category()) system_errno = e.code().value();
        else kind = E_RUNTIME_ERROR;
        what = e.what();
    } catch (const std::length_error &e) {
        kind = E_INDEX_ERROR;
        what = e.what();
    } catch (const std::out_of_range &e) {
        kind = E_INDEX_ERROR;
        what = e.what();
    } catch (const std::overflow_error &e) {
        kind = E_RANGE_ERROR;
        what = e.what();
    } catch (const std::range_error &e) {
        kind = E_RANGE_ERROR;
        what = e.what();
    } catch (const std::regex_error &e) {
        kind = E_REGEXP_ERROR;
        what = e.what();
    } catch (const std::system_error &e) {
        if (e.code().category() == std::generic_category() || e.code().category() == std::system_category()) system_errno = e.code().value();
        else kind = E_RUNTIME_ERROR;
        what = e.what();
    } catch (const std::underflow_error &e) {
        kind = E_RANGE_ERROR;
        what = e.what();
    } catch (const std::exception &e) {
        kind = E_RUNTIME_ERROR;
        what = e.what();
    } catch (...) {
        kind = E_RUNTIME_ERROR;
        what = "Unknown C++ exception thrown";
    }
    if (kind == nullptr) {
        errno = system_errno;
        mrb_sys_fail(mrb, what.c_str());
    }
    mrb_exc_raise(mrb, mrb_exc_new(mrb, kind, what.data(), static_cast<mrb_int>(what.size())));
    std::unreachable();
}

consteval bool reflect_in_std(const std::meta::info function)
{
    const std::meta::info type = std::meta::dealias(std::meta::parent_of(function));
    if (!std::meta::is_type(type)) return false;
    std::meta::info scope = std::meta::parent_of(std::meta::has_template_arguments(type) ? std::meta::template_of(type) : type);
    while (std::meta::is_namespace(scope) && scope != ^^::) {
        if (scope == ^^std) return true;
        scope = std::meta::parent_of(scope);
    }
    return false;
}

template <std::meta::info Function, class T, class... A>
void reflect_raise_on_hardened_precondition(mrb_state *const mrb, const T &object, const A &...arguments)
{
    if constexpr (reflect_in_std(Function)) {
        if constexpr (std::meta::is_operator_function(Function) && std::meta::operator_of(Function) == std::meta::operators::op_square_brackets &&
                      sizeof...(A) == 1 && (std::is_integral_v<A> && ...) && requires { object.size(); }) {
            if (((std::cmp_less(arguments, 0) || !std::cmp_less(arguments, object.size())) || ...)) [[unlikely]]
                mrb_raise(mrb, E_INDEX_ERROR, "index out of range");
        } else if constexpr (std::meta::has_identifier(Function) && requires { object.empty(); }) {
            constexpr std::string_view name = std::meta::identifier_of(Function);
            if constexpr (name == "front" || name == "back" || name == "pop_front" || name == "pop_back") {
                if (object.empty()) [[unlikely]] mrb_raise(mrb, E_INDEX_ERROR, "empty container");
            } else if constexpr ((name == "remove_prefix" || name == "remove_suffix" || name == "first" || name == "last") && sizeof...(A) == 1 &&
                                 (std::is_integral_v<A> && ...) && requires { object.size(); }) {
                if (((std::cmp_less(arguments, 0) || std::cmp_greater(arguments, object.size())) || ...)) [[unlikely]]
                    mrb_raise(mrb, E_INDEX_ERROR, "count out of range");
            } else if constexpr (name == "subspan" && (sizeof...(A) == 1 || sizeof...(A) == 2) && (std::is_integral_v<A> && ...) && requires { object.size(); }) {
                const std::tuple<const A &...> given(arguments...);
                const auto offset = std::get<0>(given);
                if (std::cmp_less(offset, 0) || std::cmp_greater(offset, object.size())) [[unlikely]] mrb_raise(mrb, E_INDEX_ERROR, "offset out of range");
                if constexpr (sizeof...(A) == 2) {
                    const auto count = std::get<1>(given);
                    if (std::cmp_not_equal(count, std::dynamic_extent) && (std::cmp_less(count, 0) || std::cmp_greater(count, object.size() - static_cast<std::size_t>(offset))))
                        [[unlikely]] mrb_raise(mrb, E_INDEX_ERROR, "count out of range");
                }
            }
        }
    }
}

template <std::size_t Count>
struct reflect_virtual_call {
    mrb_state *mrb;
    bool &running;
    int arena;
    std::array<reflect_lifetime_base *, Count> lent{};
    std::size_t lent_count = 0;
    ~reflect_virtual_call()
    {
        for (reflect_lifetime_base *const record : lent)
            if (record != nullptr) reflect_end_lifetime(*record);
        running = false;
        mrb_gc_arena_restore(mrb, arena);
    }
    template <class P>
    mrb_value lend(P *const object, const bool frozen)
    {
        using Q = std::remove_const_t<P>;
        if (RObject *const known = reflect_known(mrb, const_cast<Q *>(object)); known != nullptr) return mrb_obj_value(known);
        RData *const data = mrb_data_object_alloc(mrb, reflect_class<std::meta::dealias(^^Q)>(mrb), nullptr, &reflect_data_type_of<Q>());
        const mrb_value lent_object = mrb_obj_value(data);
        reflect_lifetime_base &record = reflect_new_lifetime<Q>(mrb, lent_object);
        record.object = const_cast<Q *>(object);
        record.alive = true;
        lent.at(lent_count++) = &record;
        if (frozen) mrb_obj_freeze(mrb, lent_object);
        reflect_identity_set(record);
        return lent_object;
    }
    template <class A>
    mrb_value argument(A &value)
    {
        using B = std::remove_cvref_t<A>;
        if constexpr (std::is_pointer_v<B> && std::is_class_v<std::remove_cv_t<std::remove_pointer_t<B>>>) {
            if (value == nullptr) return mrb_nil_value();
            return lend(value, std::is_const_v<std::remove_pointer_t<B>>);
        } else if constexpr (std::is_class_v<B> && reflect_is_object(^^B)) {
            return lend(&value, std::is_const_v<std::remove_reference_t<A>>);
        } else return reflect_result(mrb, mrb_nil_value(), value);
    }
};

template <std::meta::info Function, class Self, class Base, class... A>
auto reflect_call_virtual_overrider(Self &self, bool &running, const Base &base, A &...arguments) -> typename [:std::meta::return_type_of(Function):]
{
    if (running || self.record == nullptr || !self.record->alive) return base();
    if (std::this_thread::get_id() != self.record->callbacks->thread) [[unlikely]] reflect_abort_from_other_thread();
    if (self.record->callbacks->closed) return base();
    mrb_state *const mrb = self.record->mrb;
    if (mrb->gc.collecting || mrb_object_dead_p(mrb, reinterpret_cast<RBasic *>(self.record->ruby))) return base();
    const mrb_value object = mrb_obj_value(self.record->ruby);
    const mrb_sym name = reflect_intern<Function>(mrb);
    RClass *owner = mrb_class(mrb, object);
    const mrb_method_t method = mrb_method_search_vm(mrb, &owner, name);
    if (MRB_METHOD_UNDEF_P(method) || MRB_METHOD_CFUNC_P(method)) return base();
    running = true;
    reflect_virtual_call<sizeof...(A)> call{mrb, running, mrb_gc_arena_save(mrb)};
    const std::array<mrb_value, sizeof...(A)> argv{call.argument(arguments)...};
    const mrb_value answer = mrb_funcall_argv(mrb, object, name, static_cast<mrb_int>(argv.size()), argv.data());
    if constexpr (std::meta::return_type_of(Function) == ^^void) return;
    else {
        auto held = reflect_argument<std::meta::return_type_of(Function)>(mrb, answer);
        reflect_resolve(mrb, held);
        return reflect_pass(held);
    }
}

template <std::meta::info Type, std::meta::info Function, std::size_t Count = std::meta::parameters_of(Function).size() - reflect_skip(Function)>
mrb_value reflect_call(mrb_state *const mrb, const mrb_value self)
{
    std::array records = reflect_call_records<Count>(mrb, self);
    const reflect_call_end<Count> ended(&records);
    if constexpr (reflect_skip(Function) == 1) {
        constexpr std::meta::info operand = std::meta::type_of(std::meta::parameters_of(Function)[0]);
        using O = [:reflect_bare(operand):];
        if constexpr (reflect_mutates(operand)) mrb_check_frozen(mrb, mrb_obj_ptr(self));
        if (reflect_ptr<O>(mrb, self) == nullptr) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
        return reflect_translate_exceptions(mrb, [&] {
            auto args = reflect_get_args<Function, 1, Count>(mrb);
            if constexpr (reflect_mutates(operand)) mrb_check_frozen(mrb, mrb_obj_ptr(self));
            O *const object = reflect_ptr<O>(mrb, self);
            if (object == nullptr) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
            if constexpr (std::meta::return_type_of(Function) == ^^void) {
                std::apply([&](auto &...held) { [:Function:](*object, reflect_pass(held)...); }, args);
                return mrb_nil_value();
            } else if constexpr (std::meta::is_reference_type(std::meta::return_type_of(Function)) &&
                                 reflect_bare(std::meta::return_type_of(Function)) == std::meta::dealias(^^O)) {
                auto &answer = std::apply([&](auto &...held) -> decltype(auto) { return [:Function:](*object, reflect_pass(held)...); }, args);
                return &answer == object ? self : reflect_result(mrb, self, answer);
            } else {
                return std::apply([&](auto &...held) -> mrb_value { return reflect_result(mrb, self, [:Function:](*object, reflect_pass(held)...)); }, args);
            }
        });
    } else if constexpr (!std::meta::is_class_member(Function)) {
        return reflect_translate_exceptions(mrb, [&] {
            auto args = reflect_get_args<Function, 0, Count>(mrb);
            if constexpr (std::meta::return_type_of(Function) == ^^void) {
                std::apply([&](auto &...held) { [:Function:](reflect_pass(held)...); }, args);
                return mrb_nil_value();
            } else {
                return std::apply([&](auto &...held) -> mrb_value { return reflect_result(mrb, self, [:Function:](reflect_pass(held)...)); }, args);
            }
        });
    } else {
        using T = [:std::meta::dealias(Type):];
        if constexpr (std::meta::is_constructor(Function) && requires { typename T::overridden; }) {
            using B = typename T::overridden;
            reflect_lifetime_base &record = reflect_new_lifetime<B>(mrb, self);
            return reflect_translate_exceptions(mrb, [&] {
                auto args = reflect_get_args<Function, 0, Count>(mrb);
                reflect_adopt<B>(mrb, self, record, static_cast<B *>(std::apply([&](auto &...held) { return new T(reflect_pass(held)...); }, args)));
                return self;
            });
        } else if constexpr (std::meta::is_constructor(Function)) {
            reflect_lifetime_base &record = reflect_new_lifetime<T>(mrb, self);
            return reflect_translate_exceptions(mrb, [&] {
                auto args = reflect_get_args<Function, 0, Count>(mrb);
                reflect_adopt<T>(mrb, self, record, std::apply([&](auto &...held) { return reflect_new<T>(reflect_pass(held)...); }, args));
                return self;
            });
        } else if constexpr (std::meta::is_static_member(Function)) {
            return reflect_translate_exceptions(mrb, [&] {
                auto args = reflect_get_args<Function, 0, Count>(mrb);
                if constexpr (std::meta::return_type_of(Function) == ^^void) {
                    std::apply([&](auto &...held) { [:Function:](reflect_pass(held)...); }, args);
                    return mrb_nil_value();
                } else {
                    return std::apply([&](auto &...held) -> mrb_value { return reflect_result(mrb, self, [:Function:](reflect_pass(held)...)); }, args);
                }
            });
        } else {
            if constexpr (!std::meta::is_const(Function)) mrb_check_frozen(mrb, mrb_obj_ptr(self));
            if (reflect_ptr<T>(mrb, self) == nullptr) mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
            const mrb_value answer = reflect_translate_exceptions(mrb, [&] {
                auto args = reflect_get_args<Function, 0, Count>(mrb);
                if constexpr (!std::meta::is_const(Function)) mrb_check_frozen(mrb, mrb_obj_ptr(self));
                T *const object = reflect_ptr<T>(mrb, self);
                if (object == nullptr) mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
                if constexpr (reflect_is_coroutine(std::meta::return_type_of(Function))) {
                    if (!static_cast<reflect_lifetime_base *>(DATA_PTR(self))->owned) [[unlikely]]
                        mrb_raise(mrb, E_TYPE_ERROR, "a member coroutine needs a receiver that Ruby owns");
                }
                std::apply([&](auto &...held) { reflect_raise_on_hardened_precondition<Function>(mrb, *object, reflect_pass(held)...); }, args);
                if constexpr (std::meta::return_type_of(Function) == ^^void) {
                    std::apply([&](auto &...held) { object->[:Function:](reflect_pass(held)...); }, args);
                    return mrb_nil_value();
                } else if constexpr (std::meta::is_reference_type(std::meta::return_type_of(Function)) &&
                                     reflect_bare(std::meta::return_type_of(Function)) == std::meta::dealias(^^T)) {
                    auto &answer = std::apply([&](auto &...held) -> decltype(auto) { return object->[:Function:](reflect_pass(held)...); }, args);
                    return &answer == object ? self : reflect_result(mrb, self, answer);
                } else {
                    const mrb_value answer = std::apply([&](auto &...held) -> mrb_value { return reflect_result(mrb, self, object->[:Function:](reflect_pass(held)...)); }, args);
                    if constexpr (reflect_is_coroutine(std::meta::return_type_of(Function))) mrb_iv_set(mrb, answer, MRB_SYM(__reflected_receiver__), self);
                    return answer;
                }
            });
            return answer;
        }
    }
}

template <std::meta::info Function, bool Converting>
bool reflect_get_args_match(mrb_state *const mrb, const std::span<const mrb_value> argv)
{
    constexpr std::size_t letters = std::meta::parameters_of(Function).size() - reflect_skip(Function);
    constexpr bool rest = reflect_rest(Function);
    constexpr std::size_t required = rest ? letters - 1 : reflect_required(Function);
    if (argv.size() < required || (!rest && argv.size() > letters)) return false;
    bool fits = true;
    std::size_t at = 0;
    template for (constexpr std::meta::info P : std::define_static_array(reflect_given_parameters(Function))) {
        if (fits && at < argv.size() && !(rest && at == letters - 1)) {
            const mrb_value v = argv[at];
            constexpr char letter = reflect_get_args_letter(std::meta::type_of(P));
            if constexpr (letter == 'i' || letter == 'f') fits = mrb_integer_p(v) || mrb_float_p(v);
            else if constexpr (letter == 's' || letter == 'z') fits = mrb_string_p(v);
            else if constexpr (letter == 'n') fits = mrb_symbol_p(v) || mrb_string_p(v);
            else if constexpr (letter == 'c') fits = mrb_class_p(v) || mrb_module_p(v);
            else if constexpr (letter == 'o' && reflect_is_void_pointer(std::meta::type_of(P))) {
                fits = mrb_nil_p(v) || mrb_data_check_get_ptr(mrb, v, &mrb_void_pointer_type) != nullptr;
                if constexpr (std::meta::is_const_type(std::meta::remove_pointer(reflect_bare(std::meta::type_of(P)))))
                    fits = fits || mrb_data_check_get_ptr(mrb, v, &mrb_const_void_pointer_type) != nullptr;
            } else if constexpr (letter == 'o' && reflect_is_variant(std::meta::type_of(P))) {
                fits = reflect_variant_from<typename [:reflect_bare(std::meta::type_of(P)):]>(mrb, v).has_value();
            } else if constexpr (letter == 'o' && reflect_is_object(std::meta::type_of(P))) {
                using T = [:reflect_bare(std::meta::type_of(P)):];
                fits = reflect_ptr<T>(mrb, v) != nullptr || ((!reflect_mutates(std::meta::type_of(P)) && reflect_from_mrb<T>) ||
                                                        (Converting && !reflect_mutates(std::meta::type_of(P)) && reflect_implicitly_converts<T>(mrb, v)) ||
                                                        (reflect_is_function(std::meta::type_of(P)) && mrb_respond_to(mrb, v, MRB_SYM(call))) ||
                                                        reflect_shares<T>(mrb, v));
                if (fits && reflect_mutates(std::meta::type_of(P))) fits = !mrb_frozen_p(mrb_obj_ptr(v));
            }
        }
        at++;
    }
    return fits;
}

template <class T>
bool reflect_implicitly_converts(mrb_state *const mrb, const mrb_value v)
{
    bool converts = false;
    template for (constexpr std::meta::info constructor : reflect_converting_constructors<^^T>())
        converts = converts || reflect_get_args_match<constructor, false>(mrb, std::span<const mrb_value>(&v, 1));
    return converts;
}

template <class T>
std::unique_ptr<T> reflect_implicit_conversion(mrb_state *const mrb, const mrb_value v)
{
    std::unique_ptr<T> made;
    template for (constexpr std::meta::info constructor : reflect_converting_constructors<^^T>()) {
        if (made == nullptr && reflect_get_args_match<constructor, false>(mrb, std::span<const mrb_value>(&v, 1))) {
            constexpr std::meta::info P = std::meta::parameters_of(constructor)[0];
            using U = [:reflect_bare(std::meta::type_of(P)):];
            if constexpr (std::meta::is_pointer_type(std::meta::dealias(std::meta::type_of(P))) &&
                          std::meta::dealias(std::meta::remove_cv(std::meta::remove_pointer(std::meta::dealias(std::meta::type_of(P))))) == ^^char)
                made = std::make_unique<T>(mrb_string_cstr(mrb, v));
            else if constexpr (reflect_is_object(std::meta::type_of(P)) || std::meta::is_pointer_type(std::meta::dealias(std::meta::type_of(P)))) {
                auto held = reflect_argument<std::meta::type_of(P), false>(mrb, v);
                reflect_resolve(mrb, held);
                made = std::make_unique<T>(reflect_pass(held));
            } else made = std::make_unique<T>(mrb_value_to<U>(mrb, v));
        }
    }
    return made;
}

template <std::meta::info Type, std::meta::info Function>
mrb_value reflect_call_arity(mrb_state *mrb, mrb_value self);

template <std::meta::info Type, std::meta::info Function>
mrb_value reflect_call_given(mrb_state *const mrb, const mrb_value self)
{
    reflect_raise_on_missing_object_lifetime(Type, Function);
    if constexpr (reflect_makes_handle(Function) || reflect_declares(Type, Function))
        return reflect_call_declared(mrb, self, reflect_object_lifetime_of<Type, Function>, [&] { return reflect_call_arity<Type, Function>(mrb, self); });
    else return reflect_call_arity<Type, Function>(mrb, self);
}

template <std::meta::info Type, std::meta::info Function>
mrb_value reflect_call_arity(mrb_state *const mrb, const mrb_value self)
{
    constexpr std::size_t total = std::meta::parameters_of(Function).size() - reflect_skip(Function);
    if constexpr (reflect_rest(Function)) return reflect_call<Type, Function, total>(mrb, self);
    else {
        const std::size_t given = static_cast<std::size_t>(mrb_get_argc(mrb));
        mrb_value answer = mrb_undef_value();
        template for (constexpr std::size_t count : std::views::iota(reflect_required(Function), total + 1)) {
            if (mrb_undef_p(answer) && given == count) answer = reflect_call<Type, Function, count>(mrb, self);
        }
        if (mrb_undef_p(answer)) mrb_argnum_error(mrb, static_cast<mrb_int>(given), static_cast<int>(reflect_required(Function)), static_cast<int>(total));
        return answer;
    }
}

template <std::meta::info Function>
bool reflect_get_args_exact(mrb_state *const mrb, const std::span<const mrb_value> argv)
{
    constexpr std::size_t total = std::meta::parameters_of(Function).size() - reflect_skip(Function);
    if constexpr (reflect_rest(Function) || reflect_takes_block(Function)) return false;
    else {
        if (argv.size() < reflect_required(Function) || argv.size() > total) return false;
        bool exact = true;
        template for (constexpr std::size_t I : std::views::iota(std::size_t{0}, total)) {
            using A = [:reflect_bare(std::meta::type_of(std::meta::parameters_of(Function)[I + reflect_skip(Function)])):];
            if (I < argv.size()) {
                if constexpr (std::same_as<A, mrb_value>) {}
                else if constexpr (std::is_pointer_v<A> && std::is_class_v<std::remove_cv_t<std::remove_pointer_t<A>>>)
                    exact = exact && (mrb_nil_p(argv[I]) || reflect_ptr<std::remove_cv_t<std::remove_pointer_t<A>>>(mrb, argv[I]) != nullptr);
                else exact = exact && reflect_variant_exact<A>(mrb, argv[I]);
            }
        }
        return exact;
    }
}

template <std::meta::info Type, std::meta::info... Overloads>
mrb_value reflect_dispatch(mrb_state *const mrb, const mrb_value self)
{
    if constexpr (sizeof...(Overloads) == 1) return reflect_call_given<Type, Overloads...>(mrb, self);
    else {
        const std::vector<mrb_value> given(std::from_range, std::span<const mrb_value>(mrb_get_argv(mrb), static_cast<std::size_t>(mrb_get_argc(mrb))));
        const std::span<const mrb_value> argv(given);
        mrb_value answer = mrb_undef_value();
        template for (constexpr std::meta::info Function : std::array{Overloads...}) {
            if (mrb_undef_p(answer) && (std::meta::is_const(Function) || !mrb_frozen_p(mrb_obj_ptr(self))) &&
                reflect_get_args_exact<Function>(mrb, argv) && reflect_get_args_match<Function>(mrb, argv))
                answer = reflect_call_given<Type, Function>(mrb, self);
        }
        template for (constexpr std::meta::info Function : std::array{Overloads...}) {
            if (mrb_undef_p(answer) && (std::meta::is_const(Function) || !mrb_frozen_p(mrb_obj_ptr(self))) &&
                reflect_get_args_match<Function>(mrb, argv))
                answer = reflect_call_given<Type, Function>(mrb, self);
        }
        if (mrb_undef_p(answer) && mrb_frozen_p(mrb_obj_ptr(self))) {
            template for (constexpr std::meta::info Function : std::array{Overloads...}) {
                if (!std::meta::is_const(Function) && reflect_get_args_match<Function>(mrb, argv)) mrb_check_frozen(mrb, mrb_obj_ptr(self));
            }
        }
        if (mrb_undef_p(answer)) [[unlikely]] {
            constexpr auto counts = [] consteval {
                std::size_t fewest = SIZE_MAX, most = 0;
                bool unbounded = false;
                for (const std::meta::info f : {Overloads...}) {
                    const std::size_t total = std::meta::parameters_of(f).size() - reflect_skip(f) - (reflect_takes_block(f) ? 1 : 0);
                    fewest = std::min(fewest, reflect_rest(f) ? total - 1 : reflect_required(f));
                    most = std::max(most, total);
                    unbounded = unbounded || reflect_rest(f);
                }
                return std::array{fewest, unbounded ? SIZE_MAX : most};
            }();
            bool fits = false;
            template for (constexpr std::meta::info Function : std::array{Overloads...}) {
                constexpr std::size_t total = std::meta::parameters_of(Function).size() - reflect_skip(Function) - (reflect_takes_block(Function) ? 1 : 0);
                constexpr std::size_t required = reflect_rest(Function) ? total - 1 : reflect_required(Function);
                fits = fits || (argv.size() >= required && (reflect_rest(Function) || argv.size() <= total));
            }
            if (fits) mrb_raisef(mrb, E_TYPE_ERROR, "no overload of '%n' takes these argument types", mrb_get_mid(mrb));
            mrb_argnum_error(mrb, mrb_get_argc(mrb), static_cast<int>(counts[0]), counts[1] == SIZE_MAX ? -1 : static_cast<int>(counts[1]));
        }
        return answer;
    }
}

consteval std::string_view reflect_standard_name(const std::meta::info function)
{
    if (!std::meta::is_operator_function(function)) return {};
    switch (std::meta::operator_of(function)) {
    case std::meta::operators::op_spaceship: return "compare_three_way";
    case std::meta::operators::op_equals_equals: return "equal_to";
    case std::meta::operators::op_exclamation_equals: return "not_equal_to";
    case std::meta::operators::op_less: return "less";
    case std::meta::operators::op_greater: return "greater";
    case std::meta::operators::op_less_equals: return "less_equal";
    case std::meta::operators::op_greater_equals: return "greater_equal";
    default: return {};
    }
}

consteval bool reflect_answers_as_ruby(const std::meta::info function)
{
    const std::meta::info r = std::meta::dealias(std::meta::remove_cv(std::meta::return_type_of(function)));
    if (std::meta::operator_of(function) == std::meta::operators::op_spaceship)
        return r == std::meta::dealias(^^std::strong_ordering) || r == std::meta::dealias(^^std::weak_ordering) || r == std::meta::dealias(^^std::partial_ordering);
    return r == ^^bool;
}

consteval bool reflect_predicate(const std::meta::info function)
{
    return !std::meta::is_operator_function(function) && !std::meta::is_constructor(function) && std::meta::has_identifier(function) &&
           std::meta::dealias(std::meta::remove_cv(std::meta::return_type_of(function))) == ^^bool;
}

consteval std::string reflect_predicate_name(const std::meta::info function)
{
    std::string_view name = std::meta::identifier_of(function);
    if (name.starts_with("is_") && name.size() > 3) name.remove_prefix(3);
    return std::string(name) + "?";
}

template <std::meta::info Type, std::meta::info... Overloads>
void reflect_define_method(mrb_state *const mrb, RClass *const klass, const mrb_sym name)
{
    constexpr std::meta::info first = std::array{Overloads...}[0];
    constexpr std::string_view standard = reflect_standard_name(first);
    mrb_sym defined = name;
    if constexpr (!standard.empty()) {
        constexpr mrb_sym presym = reflect_presym(standard.data());
        defined = presym != 0 ? presym : mrb_intern_static(mrb, standard.data(), standard.size());
    }
    static constexpr std::array arities{(reflect_takes_block(Overloads) ? std::meta::parameters_of(Overloads).size() : std::size_t{0})...};
    if constexpr (std::meta::is_operator_function(first) && (std::meta::operator_of(first) == std::meta::operators::op_equals_equals ||
                                                             std::meta::operator_of(first) == std::meta::operators::op_exclamation_equals ||
                                                             std::meta::operator_of(first) == std::meta::operators::op_spaceship)) {
        ::mrb_define_method_id(mrb, klass, defined, [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            const std::vector<mrb_value> given(std::from_range, std::span<const mrb_value>(mrb_get_argv(mrb), static_cast<std::size_t>(mrb_get_argc(mrb))));
            const std::span<const mrb_value> argv(given);
            using R = [:reflect_skip(first) == 1 ? reflect_bare(std::meta::type_of(std::meta::parameters_of(first)[0])) : std::meta::dealias(Type):];
            if (reflect_ptr<R>(mrb, self) == nullptr) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
            if (argv.size() == 1 && !(reflect_get_args_match<Overloads>(mrb, argv) || ...)) {
                if constexpr (std::meta::operator_of(first) == std::meta::operators::op_equals_equals) return mrb_false_value();
                else if constexpr (std::meta::operator_of(first) == std::meta::operators::op_exclamation_equals) return mrb_true_value();
                else return mrb_nil_value();
            }
            return reflect_dispatch<Type, Overloads...>(mrb, self);
        }, MRB_ARGS_REQ(1));
    } else if constexpr (std::ranges::any_of(arities, [](const std::size_t n) { return n > 0; })) {
        ::mrb_define_method_id(mrb, klass, defined, [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            mrb_value *argv;
            mrb_int argc;
            mrb_value block;
            mrb_get_args(mrb, "*&", &argv, &argc, &block);
            if (!mrb_nil_p(block) && std::ranges::contains(arities, static_cast<std::size_t>(argc) + 1)) {
                std::vector<mrb_value> with_block(argv, argv + argc);
                with_block.push_back(block);
                return mrb_funcall_argv(mrb, self, mrb_get_mid(mrb), argc + 1, with_block.data());
            }
            return reflect_dispatch<Type, Overloads...>(mrb, self);
        }, MRB_ARGS_ANY() | MRB_ARGS_BLOCK());
    } else if constexpr (sizeof...(Overloads) == 1) {
        constexpr std::size_t total = std::meta::parameters_of(first).size() - reflect_skip(first);
        constexpr mrb_aspec aspec = reflect_rest(first) ? (MRB_ARGS_REQ(total - 1) | MRB_ARGS_REST())
                                                        : (MRB_ARGS_REQ(reflect_required(first)) | MRB_ARGS_OPT(total - reflect_required(first)));
        ::mrb_define_method_id(mrb, klass, defined,
                               [](mrb_state *const mrb, const mrb_value self) { return reflect_call_given<Type, first>(mrb, self); }, aspec);
    } else {
        ::mrb_define_method_id(mrb, klass, defined, [](mrb_state *const mrb, const mrb_value self) { return reflect_dispatch<Type, Overloads...>(mrb, self); },
                               MRB_ARGS_ANY());
    }
    if constexpr (!standard.empty() && (reflect_answers_as_ruby(Overloads) && ...)) ::mrb_define_alias_id(mrb, klass, name, defined);
    if constexpr ((reflect_predicate(Overloads) && ...)) {
        constexpr auto predicate = std::define_static_string(reflect_predicate_name(first));
        constexpr mrb_sym presym = reflect_presym(predicate);
        ::mrb_define_alias_id(mrb, klass, presym != 0 ? presym : mrb_intern_static(mrb, predicate, std::string_view(predicate).size()), defined);
    }
}

template <std::meta::info Field>
void *reflect_field_address(void *const holder)
{
    using T = [:std::meta::parent_of(Field):];
    auto &field = static_cast<T *>(holder)->[:Field:];
    if constexpr (reflect_owns_through_pointer(Field)) return const_cast<void *>(static_cast<const void *>(field.get()));
    else return const_cast<void *>(static_cast<const void *>(std::addressof(field)));
}

template <std::meta::info Field>
mrb_value reflect_child(mrb_state *const mrb, const mrb_value self)
{
    using T = [:std::meta::parent_of(Field):];
    constexpr std::meta::info bare = reflect_bare(std::meta::type_of(Field));
    constexpr std::meta::info held = reflect_owns_through_pointer(Field) ? std::meta::template_arguments_of(bare)[0] : std::meta::type_of(Field);
    using C = [:std::meta::dealias(std::meta::remove_cv(held)):];
    constexpr bool constant = std::meta::is_const_type(held) || std::meta::is_const_type(std::meta::type_of(Field));
    const bool frozen = constant || mrb_frozen_p(mrb_obj_ptr(self));
    T *const object = reflect_ptr<T>(mrb, self);
    if (object == nullptr) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
    reflect_lifetime_base &parent = *static_cast<reflect_lifetime_base *>(DATA_PTR(self));
    void *const address = reflect_field_address<Field>(object);
    if (address == nullptr) return mrb_nil_value();
    for (reflect_lifetime_base *const child : parent.children) {
        if (child == nullptr || child->field != &reflect_field_address<Field>) continue;
        if (child->object == address && !mrb_object_dead_p(mrb, reinterpret_cast<RBasic *>(child->ruby))) {
            if (frozen) mrb_obj_freeze(mrb, mrb_obj_value(child->ruby));
            return mrb_obj_value(child->ruby);
        }
        reflect_end_lifetime(*child);
        break;
    }
    RData *const data = mrb_data_object_alloc(mrb, reflect_class<std::meta::dealias(^^C)>(mrb), nullptr, &reflect_data_type_of<C>());
    const mrb_value made = mrb_obj_value(data);
    reflect_lifetime_base &record = reflect_new_lifetime<C>(mrb, made);
    T *const now = reflect_ptr<T>(mrb, self);
    if (now == nullptr) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
    void *const current = reflect_field_address<Field>(now);
    if (current == nullptr) return mrb_nil_value();
    const std::size_t vacant = static_cast<std::size_t>(std::ranges::find(parent.children, nullptr) - parent.children.begin());
    parent.children.at(vacant) = &record;
    record.parent = &parent;
    record.index = vacant;
    record.holder = now;
    record.field = &reflect_field_address<Field>;
    record.object = current;
    record.alive = true;
    mrb_iv_set(mrb, made, reflect_sym<kOwner>(mrb), self);
    if (frozen) mrb_obj_freeze(mrb, made);
    reflect_identity_set(record);
    return made;
}

template <std::meta::info Field>
void reflect_define_field(mrb_state *const mrb, RClass *const klass)
{
    using T = [:std::meta::parent_of(Field):];
    ::mrb_define_method_id(mrb, klass, reflect_intern<Field>(mrb), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        return reflect_translate_exceptions(mrb, [&]() -> mrb_value {
            T *const object = reflect_ptr<T>(mrb, self);
            if (object == nullptr) mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
            using F = [:reflect_bare(std::meta::type_of(Field)):];
            if constexpr (reflect_is_variant(^^F)) return reflect_result(mrb, self, object->[:Field:]);
            else if constexpr (std::is_array_v<F>) {
                const mrb_value array = mrb_ary_new_capa(mrb, static_cast<mrb_int>(std::extent_v<F>));
                for (const auto &element : object->[:Field:]) mrb_ary_push(mrb, array, reflect_result(mrb, mrb_nil_value(), std::remove_cvref_t<decltype(element)>(element)));
                return array;
            } else if constexpr (reflect_owns(Field)) return reflect_child<Field>(mrb, self);
            else if constexpr (std::meta::is_bit_field(Field)) return reflect_result(mrb, self, static_cast<F>(object->[:Field:]));
            else return reflect_result(mrb, self, object->[:Field:]);
        });
    }, MRB_ARGS_NONE());
    if constexpr (!std::meta::is_const_type(std::meta::type_of(Field)) && !reflect_is_view(reflect_bare(std::meta::type_of(Field))) &&
                  (std::is_copy_assignable_v<typename [:reflect_bare(std::meta::type_of(Field)):]> || std::is_array_v<typename [:reflect_bare(std::meta::type_of(Field)):]>)) {
        constexpr std::string_view name = std::meta::identifier_of(Field);
        constexpr auto setter = std::define_static_string(std::string(name) + "=");
        constexpr mrb_sym presym = reflect_presym(setter);
        const mrb_sym sym = presym != 0 ? presym : mrb_intern_static(mrb, setter, name.size() + 1);
        ::mrb_define_method_id(mrb, klass, sym, [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            std::array records = reflect_call_records<1>(mrb, self);
            const reflect_call_end<1> ended(&records);
            return reflect_translate_exceptions(mrb, [&]() -> mrb_value {
                mrb_check_frozen(mrb, mrb_obj_ptr(self));
                if (reflect_ptr<T>(mrb, self) == nullptr) mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
                mrb_value v;
                mrb_get_args(mrb, "o", &v);
                using F = [:reflect_bare(std::meta::type_of(Field)):];
                const auto receiver = [&]() -> T & {
                    mrb_check_frozen(mrb, mrb_obj_ptr(self));
                    return reflect_receiver_or_raise<T>(mrb, self);
                };
                if constexpr (reflect_is_variant(^^F)) {
                    std::optional<F> made = reflect_variant_from<F>(mrb, v);
                    if (!made) [[unlikely]] mrb_raisef(mrb, E_TYPE_ERROR, "%T fits no alternative of the variant", v);
                    receiver().[:Field:] = std::move(*made);
                } else if constexpr (std::is_array_v<F>) {
                    using E = std::remove_extent_t<F>;
                    if (!mrb_array_p(v) || RARRAY_LEN(v) != static_cast<mrb_int>(std::extent_v<F>)) [[unlikely]]
                        mrb_raisef(mrb, E_ARGUMENT_ERROR, "an Array of %d elements wanted", static_cast<int>(std::extent_v<F>));
                    std::array<E, std::extent_v<F>> converted{};
                    for (std::size_t i = 0; i < converted.size(); i++) {
                        const mrb_value element = mrb_ary_entry(v, static_cast<mrb_int>(i));
                        if (E *const p = reflect_ptr<E>(mrb, element); p != nullptr) converted[i] = *p;
                        else if constexpr (reflect_from_mrb<E>) converted[i] = mrb_value_to<E>(mrb, element);
                        else mrb_raise(mrb, E_TYPE_ERROR, "wrong type");
                    }
                    std::ranges::copy(converted, std::ranges::begin(receiver().[:Field:]));
                } else if constexpr (std::is_pointer_v<F> && std::is_void_v<std::remove_pointer_t<F>>) {
                    F const address = reflect_void_ptr<std::remove_pointer_t<F>>(mrb, v);
                    receiver().[:Field:] = address;
                } else if (reflect_ptr<F>(mrb, v) != nullptr) {
                    T &object = receiver();
                    object.[:Field:] = reflect_receiver_or_raise<F>(mrb, v);
                } else if constexpr (reflect_from_mrb<F>) {
                    F converted = mrb_value_to<F>(mrb, v);
                    receiver().[:Field:] = std::move(converted);
                } else mrb_raise(mrb, E_TYPE_ERROR, "wrong type");
                return v;
            });
        }, MRB_ARGS_REQ(1));
    }
}

template <class T>
concept reflect_bytes = std::ranges::contiguous_range<T> && std::same_as<std::remove_cv_t<std::ranges::range_value_t<T>>, char>;

template <class T>
concept reflect_map = requires { typename T::key_type; typename T::mapped_type; } && std::ranges::range<T> &&
                      requires { std::tuple_size<std::remove_cvref_t<std::ranges::range_reference_t<T>>>::value; };

template <std::meta::info Type, std::meta::info Function>
void reflect_define_conversion_function(mrb_state *const mrb, RClass *const klass)
{
    using T = [:std::meta::dealias(Type):];
    using R = [:reflect_bare(std::meta::return_type_of(Function)):];
    constexpr auto convert = [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        if constexpr (!std::meta::is_const(Function)) mrb_check_frozen(mrb, mrb_obj_ptr(self));
        T *const object = reflect_ptr<T>(mrb, self);
        if (object == nullptr) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
        return reflect_translate_exceptions(mrb, [&] {
            const R value = object->[:Function:]();
            if constexpr (std::same_as<R, const char *>) return value == nullptr ? mrb_nil_value() : mrb_str_new_cstr(mrb, value);
            else if constexpr (std::is_arithmetic_v<R>) return cpp_to_mrb_value(mrb, value);
            else return mrb_str_new(mrb, value.data(), static_cast<mrb_int>(value.size()));
        });
    };
    constexpr bool implicit = !std::meta::is_explicit(Function);
    if constexpr (std::is_integral_v<R>) {
        ::mrb_define_method_id(mrb, klass, MRB_SYM(to_i), convert, MRB_ARGS_NONE());
        if constexpr (implicit) ::mrb_define_method_id(mrb, klass, MRB_SYM(to_int), convert, MRB_ARGS_NONE());
    } else if constexpr (std::is_floating_point_v<R>) {
        ::mrb_define_method_id(mrb, klass, MRB_SYM(to_f), convert, MRB_ARGS_NONE());
    } else {
        ::mrb_define_method_id(mrb, klass, MRB_SYM(to_s), convert, MRB_ARGS_NONE());
        if constexpr (implicit) ::mrb_define_method_id(mrb, klass, MRB_SYM(to_str), convert, MRB_ARGS_NONE());
    }
}

template <class T>
struct reflect_single_pass {
    T range;
    std::optional<std::ranges::iterator_t<T>> at;
    bool running = false;
};

template <class T>
reflect_single_pass<T> &reflect_iteration(mrb_state *const mrb, const mrb_value self)
{
    static constexpr mrb_data_type type{"iteration", [](mrb_state *, void *const p) { delete static_cast<reflect_single_pass<T> *>(p); }};
    const mrb_sym key = MRB_SYM(__reflected_iteration__);
    const mrb_value held = mrb_iv_get(mrb, self, key);
    if (void *const p = mrb_data_check_get_ptr(mrb, held, &type); p != nullptr) [[likely]] {
        reflect_single_pass<T> &pass = *static_cast<reflect_single_pass<T> *>(p);
        if (pass.running) [[unlikely]] mrb_raise(mrb, mrb_class_get_id(mrb, MRB_SYM(CppCoroutineError)), "double resume");
        return pass;
    }
    mrb_check_frozen(mrb, mrb_obj_ptr(self));
    T *const object = reflect_ptr<T>(mrb, self);
    if (object == nullptr) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
    std::unique_ptr<reflect_single_pass<T>> made(new reflect_single_pass<T>{std::move(*object), std::nullopt, true});
    RData *const data = mrb_data_object_alloc(mrb, mrb->object_class, made.get(), &type);
    reflect_single_pass<T> &pass = *made.release();
    mrb_iv_set(mrb, self, key, mrb_obj_value(data));
    const std::unique_ptr<bool, decltype([](bool *const running) { *running = false; })> resumed(&pass.running);
    pass.at.emplace(pass.range.begin());
    return pass;
}

template <class T>
void reflect_define_conversions(mrb_state *const mrb, RClass *const klass)
{
    if constexpr (reflect_bytes<T>) {
        constexpr auto to_s = [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            const T &object = reflect_receiver_or_raise<T>(mrb, self);
            return mrb_str_new(mrb, std::ranges::data(object), static_cast<mrb_int>(std::ranges::size(object)));
        };
        ::mrb_define_method_id(mrb, klass, reflect_sym<kToS>(mrb), to_s, MRB_ARGS_NONE());
    } else if constexpr (reflect_map<T>) {
        constexpr auto to_h = [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            return reflect_translate_exceptions(mrb, [&]() -> mrb_value {
                T *const object = reflect_ptr<T>(mrb, self);
                if (object == nullptr) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
                const mrb_value pairs = mrb_ary_new(mrb);
                for (auto &&[key, mapped] : *object) {
                    if constexpr (reflect_bytes<std::remove_cvref_t<decltype(key)>>)
                        mrb_ary_push(mrb, pairs, mrb_str_new(mrb, std::ranges::data(key), static_cast<mrb_int>(std::ranges::size(key))));
                    else
                        mrb_ary_push(mrb, pairs, reflect_result(mrb, self, key));
                    mrb_ary_push(mrb, pairs, reflect_result(mrb, self, mapped));
                }
                const mrb_value hash = mrb_hash_new_capa(mrb, RARRAY_LEN(pairs) / 2);
                for (mrb_int i = 0; i < RARRAY_LEN(pairs); i += 2) mrb_hash_set(mrb, hash, RARRAY_PTR(pairs)[i], RARRAY_PTR(pairs)[i + 1]);
                return hash;
            });
        };
        ::mrb_define_method_id(mrb, klass, reflect_sym<kToH>(mrb), to_h, MRB_ARGS_NONE());
    } else if constexpr (std::ranges::input_range<T> && !std::ranges::forward_range<T>) {
        constexpr auto each = [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            mrb_value block;
            mrb_get_args(mrb, "&!", &block);
            return reflect_translate_exceptions(mrb, [&] {
                reflect_single_pass<T> &pass = reflect_iteration<T>(mrb, self);
                pass.running = true;
                const std::unique_ptr<bool, decltype([](bool *const running) { *running = false; })> resumed(&pass.running);
                while (pass.at && *pass.at != pass.range.end()) {
                    std::ranges::range_value_t<T> value(**pass.at);
                    ++*pass.at;
                    const mrb_value yielded = reflect_result(mrb, mrb_nil_value(), std::move(value));
                    pass.running = false;
                    mrb_yield(mrb, block, yielded);
                    pass.running = true;
                }
                return self;
            });
        };
        constexpr auto next = [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            return reflect_translate_exceptions(mrb, [&] {
                reflect_single_pass<T> &pass = reflect_iteration<T>(mrb, self);
                pass.running = true;
                const std::unique_ptr<bool, decltype([](bool *const running) { *running = false; })> resumed(&pass.running);
                if (!pass.at || *pass.at == pass.range.end()) [[unlikely]] mrb_raise(mrb, mrb_class_get_id(mrb, MRB_SYM(StopIteration)), "iteration reached an end");
                std::ranges::range_value_t<T> value(**pass.at);
                ++*pass.at;
                return reflect_result(mrb, mrb_nil_value(), std::move(value));
            });
        };
        constexpr auto to_a = [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            return reflect_translate_exceptions(mrb, [&] {
                reflect_single_pass<T> &pass = reflect_iteration<T>(mrb, self);
                pass.running = true;
                const std::unique_ptr<bool, decltype([](bool *const running) { *running = false; })> resumed(&pass.running);
                const mrb_value array = mrb_ary_new(mrb);
                while (pass.at && *pass.at != pass.range.end()) {
                    std::ranges::range_value_t<T> value(**pass.at);
                    ++*pass.at;
                    mrb_ary_push(mrb, array, reflect_result(mrb, mrb_nil_value(), std::move(value)));
                }
                return array;
            });
        };
        ::mrb_define_method_id(mrb, klass, reflect_sym<kEach>(mrb), each, MRB_ARGS_BLOCK());
        ::mrb_define_method_id(mrb, klass, MRB_SYM(next), next, MRB_ARGS_NONE());
        ::mrb_define_method_id(mrb, klass, reflect_sym<kToA>(mrb), to_a, MRB_ARGS_NONE());
        if (mrb_class_defined_id(mrb, reflect_sym<kEnumerable>(mrb)))
            mrb_include_module(mrb, klass, mrb_module_get_id(mrb, reflect_sym<kEnumerable>(mrb)));
    } else if constexpr (std::ranges::range<T>) {
        constexpr auto to_a = [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            return reflect_translate_exceptions(mrb, [&]() -> mrb_value {
                T *const object = reflect_ptr<T>(mrb, self);
                if (object == nullptr) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
                const mrb_value array = mrb_ary_new(mrb);
                for (auto &&element : *object) mrb_ary_push(mrb, array, reflect_result(mrb, self, element));
                return array;
            });
        };
        ::mrb_define_method_id(mrb, klass, reflect_sym<kToA>(mrb), to_a, MRB_ARGS_NONE());
        if constexpr (std::ranges::random_access_range<T> && std::ranges::sized_range<T>) {
            constexpr auto each = [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
                return reflect_translate_exceptions(mrb, [&]() -> mrb_value {
                    mrb_value block;
                    mrb_get_args(mrb, "&", &block);
                    if (mrb_nil_p(block)) return mrb_funcall_id(mrb, self, MRB_SYM(to_enum), 1, mrb_symbol_value(reflect_sym<kEach>(mrb)));
                    for (std::size_t i = 0;; ++i) {
                        T *const object = reflect_ptr<T>(mrb, self);
                        if (object == nullptr) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
                        if (i >= std::ranges::size(*object)) return self;
                        mrb_yield(mrb, block, reflect_result(mrb, self, std::ranges::begin(*object)[i]));
                    }
                });
            };
            ::mrb_define_method_id(mrb, klass, reflect_sym<kEach>(mrb), each, MRB_ARGS_BLOCK());
            if (mrb_class_defined_id(mrb, reflect_sym<kEnumerable>(mrb)))
                mrb_include_module(mrb, klass, mrb_module_get_id(mrb, reflect_sym<kEnumerable>(mrb)));
        }
    }
}

template <class T>
void reflect_define_replace(mrb_state *const mrb, RClass *const klass)
{
    ::mrb_define_method_id(mrb, klass, reflect_sym<kReplace>(mrb), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        std::array records = reflect_call_records<1>(mrb, self);
        const reflect_call_end<1> ended(&records);
        return reflect_translate_exceptions(mrb, [&]() -> mrb_value {
            mrb_check_frozen(mrb, mrb_obj_ptr(self));
            if (reflect_ptr<T>(mrb, self) == nullptr) mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
            mrb_value v;
            mrb_get_args(mrb, "o", &v);
            if (reflect_ptr<T>(mrb, v) != nullptr) {
                const T &source = reflect_receiver_or_raise<T>(mrb, v);
                reflect_receiver_or_raise<T>(mrb, self) = source;
            } else if constexpr (reflect_from_mrb<T>) {
                T converted = mrb_value_to<T>(mrb, v);
                mrb_check_frozen(mrb, mrb_obj_ptr(self));
                reflect_receiver_or_raise<T>(mrb, self) = std::move(converted);
            } else mrb_raise(mrb, E_TYPE_ERROR, "wrong type");
            return self;
        });
    }, MRB_ARGS_REQ(1));
}

template <std::meta::info Member>
void reflect_define_static_data_member(mrb_state *const mrb, RClass *const scope, RClass *const singleton)
{
    using G = [:reflect_bare(std::meta::type_of(Member)):];
    if constexpr (std::is_class_v<G>)
        ::mrb_define_const_id(mrb, scope, reflect_intern<Member>(mrb),
                              reflect_borrowed<G>(mrb, const_cast<G *>(&[:Member:]), mrb_obj_value(scope), std::meta::is_const_type(std::meta::type_of(Member))));
    ::mrb_define_method_id(mrb, singleton, reflect_intern<Member>(mrb), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        using F = [:reflect_bare(std::meta::type_of(Member)):];
        if constexpr (std::is_class_v<F>) return mrb_const_get(mrb, self, reflect_intern<Member>(mrb));
        else return reflect_result(mrb, self, [:Member:]);
    }, MRB_ARGS_NONE());
    if constexpr (!std::meta::is_const_type(std::meta::type_of(Member)) && std::is_copy_assignable_v<typename [:reflect_bare(std::meta::type_of(Member)):]>) {
        constexpr auto setter = std::define_static_string(std::string(std::meta::identifier_of(Member)) + "=");
        constexpr mrb_sym presym = reflect_presym(setter);
        const mrb_sym sym = presym != 0 ? presym : mrb_intern_static(mrb, setter, std::meta::identifier_of(Member).size() + 1);
        ::mrb_define_method_id(mrb, singleton, sym, [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            using F = [:reflect_bare(std::meta::type_of(Member)):];
            mrb_value v;
            mrb_get_args(mrb, "o", &v);
            if constexpr (std::is_pointer_v<F> && std::is_void_v<std::remove_pointer_t<F>>) [:Member:] = reflect_void_ptr<std::remove_pointer_t<F>>(mrb, v);
            else if (F *const p = reflect_ptr<F>(mrb, v); p != nullptr) [:Member:] = *p;
            else if constexpr (reflect_from_mrb<F>) [:Member:] = mrb_value_to<F>(mrb, v);
            else mrb_raise(mrb, E_TYPE_ERROR, "wrong type");
            return v;
        }, MRB_ARGS_REQ(1));
    }
}

template <std::meta::info Type>
consteval mrb_aspec reflect_call_aspec()
{
    std::vector<std::meta::info> calls;
    for (const std::meta::info m : reflect_members<Type>())
        if (std::meta::is_operator_function(m) && std::meta::operator_of(m) == std::meta::operators::op_parentheses) calls.push_back(m);
    if (calls.size() != 1) return MRB_ARGS_ANY();
    const std::size_t total = std::meta::parameters_of(calls[0]).size();
    return MRB_ARGS_REQ(reflect_required(calls[0])) | MRB_ARGS_OPT(total - reflect_required(calls[0]));
}

template <std::meta::info Type, std::meta::info Subscript>
void reflect_define_element_assignment(mrb_state *const mrb, RClass *const methods)
{
    ::mrb_define_method_id(mrb, methods, MRB_OPSYM(aset), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        std::array records = reflect_call_records<2>(mrb, self);
        const reflect_call_end<2> ended(&records);
        return reflect_translate_exceptions(mrb, [&]() -> mrb_value {
            using T = [:std::meta::dealias(Type):];
            using E = [:reflect_bare(std::meta::return_type_of(Subscript)):];
            mrb_check_frozen(mrb, mrb_obj_ptr(self));
            if (reflect_ptr<T>(mrb, self) == nullptr) mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
            mrb_value index, v;
            mrb_get_args(mrb, "oo", &index, &v);
            auto held = reflect_argument<std::meta::type_of(std::meta::parameters_of(Subscript)[0])>(mrb, index);
            std::optional<E> converted;
            if constexpr (std::is_pointer_v<E> && std::is_void_v<std::remove_pointer_t<E>>) converted.emplace(reflect_void_ptr<std::remove_pointer_t<E>>(mrb, v));
            else if (reflect_ptr<E>(mrb, v) != nullptr) {}
            else if constexpr (reflect_from_mrb<E>) converted.emplace(mrb_value_to<E>(mrb, v));
            else mrb_raise(mrb, E_TYPE_ERROR, "wrong type");
            reflect_resolve(mrb, held);
            mrb_check_frozen(mrb, mrb_obj_ptr(self));
            T &object = reflect_receiver_or_raise<T>(mrb, self);
            reflect_raise_on_hardened_precondition<Subscript>(mrb, object, reflect_pass(held));
            E &element = object.[:Subscript:](reflect_pass(held));
            if (converted) element = std::move(*converted);
            else element = reflect_receiver_or_raise<E>(mrb, v);
            return v;
        });
    }, MRB_ARGS_REQ(2));
}

template <std::meta::info Namespace>
consteval auto reflect_type_aliases_computed()
{
    std::vector<std::meta::info> aliases;
    for (const std::meta::info m : std::meta::members_of(Namespace, std::meta::access_context::current()))
        if (std::meta::is_type_alias(m) && std::meta::has_identifier(m) && !std::meta::identifier_of(m).starts_with("_") &&
            !std::meta::identifier_of(m).contains("__"))
            aliases.push_back(m);
    return std::define_static_array(aliases);
}

template <std::meta::info Namespace>
inline constexpr auto reflect_type_aliases_cached = reflect_type_aliases_computed<Namespace>();

consteval std::vector<std::meta::info> reflect_alias_scopes(const std::meta::info type)
{
    std::vector<std::meta::info> scopes;
    for (std::meta::info scope = std::meta::parent_of(std::meta::template_of(std::meta::dealias(type))); std::meta::is_namespace(scope);
         scope = std::meta::parent_of(scope)) {
        scopes.push_back(scope);
        if (!std::meta::has_parent(scope) || (std::meta::has_identifier(scope) && !std::meta::identifier_of(scope).starts_with("__"))) break;
    }
    return scopes;
}

template <std::meta::info Type>
void reflect_register_specialization(reflect_definition &definition, RClass *const klass)
{
    mrb_state *const mrb = definition.mrb;
    constexpr std::meta::info t = std::meta::dealias(Type);
    static constexpr std::string_view display = std::define_static_string(std::meta::display_string_of(t));
    mrb_obj_iv_set(mrb, reinterpret_cast<RObject *>(klass), MRB_SYM(__classname__), mrb_str_new_static(mrb, display.data(), display.size()));
    bool named_by_alias = false;
    template for (constexpr std::meta::info scope : std::define_static_array(reflect_alias_scopes(t))) {
        template for (constexpr std::meta::info alias : reflect_type_aliases_cached<scope>) {
            if constexpr (std::meta::dealias(alias) == t) {
                RClass *alias_outer = mrb->object_class;
                template for (constexpr std::meta::info named : std::define_static_array(reflect_namespaces(alias)))
                    alias_outer = ::mrb_define_module_under_id(mrb, alias_outer, reflect_intern<named>(mrb));
                static constexpr auto alias_name = std::define_static_string(reflect_class_name(alias));
                mrb_define_const(mrb, alias_outer, alias_name, mrb_obj_value(klass));
                if (!named_by_alias) {
                    mrb_value path = mrb_str_dup(mrb, mrb_class_path(mrb, alias_outer));
                    mrb_str_cat_lit(mrb, path, "::");
                    mrb_str_cat_cstr(mrb, path, alias_name);
                    mrb_obj_iv_set(mrb, reinterpret_cast<RObject *>(klass), MRB_SYM(__classname__), path);
                    named_by_alias = true;
                }
            }
        }
    }
}

template <std::meta::info Type>
RClass *reflect_enclosing_scope(reflect_definition &definition, RClass *const under)
{
    mrb_state *const mrb = definition.mrb;
    RClass *outer = under;
    template for (constexpr std::meta::info scope : std::define_static_array(reflect_namespaces(Type)))
        outer = ::mrb_define_module_under_id(mrb, outer, reflect_intern<scope>(mrb));
    constexpr std::meta::info enclosing = std::meta::parent_of(std::meta::dealias(Type));
    if constexpr (std::meta::is_type(enclosing) && std::meta::is_class_type(enclosing) && !reflect_reserved(enclosing)) outer = reflect_class<enclosing>(definition);
    return outer;
}

template <std::meta::info Type>
RClass *reflect_define_enum(reflect_definition &definition, RClass *const under)
{
    mrb_state *const mrb = definition.mrb;
    using E = [:std::meta::dealias(Type):];
    RClass *const outer = reflect_enclosing_scope<Type>(definition, under);
    const mrb_sym name = reflect_intern<Type>(mrb);
    if (mrb_const_defined_at(mrb, mrb_obj_value(outer), name)) [[unlikely]]
        mrb_raisef(mrb, E_NAME_ERROR, "%n is already defined in %C", name, outer);
    RClass *const klass = ::mrb_define_class_under_id(mrb, outer, name, mrb->object_class);
    MRB_SET_INSTANCE_TT(klass, MRB_TT_CDATA);
    mrb_undef_class_method_id(mrb, klass, MRB_SYM(new));
    mrb_iv_set(mrb, mrb_obj_value(klass), reflect_reflected_key(mrb), mrb_true_value());
    mrb_iv_set(mrb, mrb_obj_value(mrb->object_class), reflect_class_key<std::meta::dealias(Type)>(mrb), mrb_obj_value(klass));
    RClass *const methods = ::mrb_define_module_under_id(mrb, klass, reflect_sym<kInstanceMethods>(mrb));
    mrb_iv_set(mrb, mrb_obj_value(mrb->object_class), reflect_module_key<std::meta::dealias(Type)>(mrb), mrb_obj_value(methods));
    constexpr auto to_i = [](mrb_state *const mrb, const mrb_value self) -> mrb_value { return mrb_convert_number(mrb, reflect_receiver_or_raise<E>(mrb, self)); };
    ::mrb_define_method_id(mrb, methods, MRB_SYM(to_i), to_i, MRB_ARGS_NONE());
    if constexpr (!std::meta::is_scoped_enum_type(std::meta::dealias(Type))) ::mrb_define_method_id(mrb, methods, MRB_SYM(to_int), to_i, MRB_ARGS_NONE());
    ::mrb_define_method_id(mrb, methods, MRB_OPSYM(cmp), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        return reflect_translate_exceptions(mrb, [&]() -> mrb_value {
            const E value = reflect_receiver_or_raise<E>(mrb, self);
            if constexpr (!std::meta::is_scoped_enum_type(std::meta::dealias(Type))) {
                if (const mrb_value n = mrb_get_arg1(mrb); mrb_integer_p(n)) {
                    const std::underlying_type_t<E> u = std::to_underlying(value);
                    return mrb_fixnum_value(std::cmp_less(u, mrb_integer(n)) ? -1 : std::cmp_greater(u, mrb_integer(n)) ? 1 : 0);
                }
            }
            const E *const other = reflect_ptr<E>(mrb, mrb_get_arg1(mrb));
            if (other == nullptr) return mrb_nil_value();
            return mrb_fixnum_value(value < *other ? -1 : value > *other ? 1 : 0);
        });
    }, MRB_ARGS_REQ(1));
    constexpr auto equal = [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        const E *const other = reflect_ptr<E>(mrb, mrb_get_arg1(mrb));
        return mrb_bool_value(other != nullptr && *other == reflect_receiver_or_raise<E>(mrb, self));
    };
    ::mrb_define_method_id(mrb, methods, MRB_OPSYM(eq), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        return reflect_translate_exceptions(mrb, [&]() -> mrb_value {
            if constexpr (!std::meta::is_scoped_enum_type(std::meta::dealias(Type))) {
                if (const mrb_value n = mrb_get_arg1(mrb); mrb_integer_p(n)) return mrb_bool_value(std::cmp_equal(std::to_underlying(reflect_receiver_or_raise<E>(mrb, self)), mrb_integer(n)));
            }
            const E *const other = reflect_ptr<E>(mrb, mrb_get_arg1(mrb));
            return mrb_bool_value(other != nullptr && *other == reflect_receiver_or_raise<E>(mrb, self));
        });
    }, MRB_ARGS_REQ(1));
    ::mrb_define_method_id(mrb, methods, MRB_SYM_Q(eql), equal, MRB_ARGS_REQ(1));
    ::mrb_define_method_id(mrb, methods, MRB_SYM(hash), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        return reflect_translate_exceptions(mrb, [&]() -> mrb_value {
            return reflect_translate_exceptions(mrb, [&]() -> mrb_value {
                return mrb_int_value(mrb, static_cast<mrb_int>(std::hash<E>{}(reflect_receiver_or_raise<E>(mrb, self))));
            });
        });
    }, MRB_ARGS_NONE());
    ::mrb_define_method_id(mrb, methods, MRB_SYM(to_s), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        const E value = reflect_receiver_or_raise<E>(mrb, self);
        template for (constexpr std::meta::info enumerator : std::define_static_array(std::meta::enumerators_of(std::meta::dealias(Type)))) {
            if (value == [:enumerator:]) return mrb_str_new_static(mrb, std::meta::identifier_of(enumerator).data(), std::meta::identifier_of(enumerator).size());
        }
        return mrb_funcall_id(mrb, mrb_convert_number(mrb, value), reflect_sym<kToS>(mrb), 0);
    }, MRB_ARGS_NONE());
    ::mrb_define_method_id(mrb, methods, MRB_SYM(inspect), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        return mrb_format(mrb, "#<%C %v>", mrb_obj_class(mrb, self), mrb_funcall_id(mrb, self, reflect_sym<kToS>(mrb), 0));
    }, MRB_ARGS_NONE());
    mrb_include_module(mrb, klass, mrb_module_get_id(mrb, MRB_SYM(Comparable)));
    mrb_include_module(mrb, klass, methods);
    template for (constexpr std::meta::info enumerator : std::define_static_array(std::meta::enumerators_of(std::meta::dealias(Type)))) {
        static constexpr std::string_view constant = std::define_static_string(reflect_class_name(enumerator));
        mrb_define_const(mrb, klass, constant.data(), reflect_object(mrb, E{[:enumerator:]}, true));
    }
    return klass;
}

template <class E>
mrb_value reflect_enumerator(mrb_state *const mrb, const E value)
{
    RClass *const klass = reflect_class<^^E>(mrb);
    template for (constexpr std::meta::info enumerator : std::define_static_array(std::meta::enumerators_of(^^E))) {
        static constexpr std::string_view constant = std::define_static_string(reflect_class_name(enumerator));
        if (value == [:enumerator:]) return mrb_const_get(mrb, mrb_obj_value(klass), mrb_intern_static(mrb, constant.data(), constant.size()));
    }
    return reflect_object(mrb, value, true);
}

template <class T>
T *reflect_comparable_operand(mrb_state *const mrb, const mrb_value v, std::optional<T> &made)
{
    if (T *const p = reflect_ptr<T>(mrb, v); p != nullptr) return p;
    if constexpr (reflect_from_mrb<T>) {
        if (reflect_variant_exact<T>(mrb, v)) return &made.emplace(mrb_value_to<T>(mrb, v));
    }
    return nullptr;
}

template <std::meta::info Type, bool DeclaresEquals, bool DeclaresSpaceship>
void reflect_define_comparisons(mrb_state *const mrb, RClass *const methods)
{
    using T = [:std::meta::dealias(Type):];
    if constexpr (std::equality_comparable<T> && !DeclaresEquals) {
        ::mrb_define_method_id(mrb, methods, MRB_OPSYM(eq), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            mrb_value v;
            mrb_get_args(mrb, "o", &v);
            std::optional<T> made;
            const T *const other = reflect_comparable_operand<T>(mrb, v, made);
            return mrb_bool_value(other != nullptr && reflect_receiver_or_raise<T>(mrb, self) == *other);
        }, MRB_ARGS_REQ(1));
    }
    if constexpr (std::three_way_comparable<T> && !DeclaresSpaceship) {
        ::mrb_define_method_id(mrb, methods, MRB_OPSYM(cmp), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            mrb_value v;
            mrb_get_args(mrb, "o", &v);
            std::optional<T> made;
            const T *const other = reflect_comparable_operand<T>(mrb, v, made);
            if (other == nullptr) return mrb_nil_value();
            return reflect_result(mrb, self, reflect_receiver_or_raise<T>(mrb, self) <=> *other);
        }, MRB_ARGS_REQ(1));
        mrb_include_module(mrb, methods, mrb_module_get_id(mrb, MRB_SYM(Comparable)));
    }
    if constexpr (std::equality_comparable<T> && std::is_default_constructible_v<std::hash<T>>) {
        ::mrb_define_method_id(mrb, methods, MRB_SYM_Q(eql), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            mrb_value v;
            mrb_get_args(mrb, "o", &v);
            const T *const other = reflect_ptr<T>(mrb, v);
            return mrb_bool_value(other != nullptr && reflect_receiver_or_raise<T>(mrb, self) == *other);
        }, MRB_ARGS_REQ(1));
        ::mrb_define_method_id(mrb, methods, MRB_SYM(hash), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            return mrb_int_value(mrb, static_cast<mrb_int>(std::hash<T>{}(reflect_receiver_or_raise<T>(mrb, self))));
        }, MRB_ARGS_NONE());
    }
}

template <std::meta::info Type>
RClass *reflect_define_opaque_class(reflect_definition &definition, RClass *const under)
{
    mrb_state *const mrb = definition.mrb;
    RClass *const outer = reflect_enclosing_scope<Type>(definition, under);
    const mrb_sym name = reflect_intern<Type>(mrb);
    if (mrb_const_defined_at(mrb, mrb_obj_value(outer), name)) [[unlikely]] mrb_raisef(mrb, E_NAME_ERROR, "%n is already defined in %C", name, outer);
    RClass *const klass = ::mrb_define_class_under_id(mrb, outer, name, mrb->object_class);
    MRB_SET_INSTANCE_TT(klass, MRB_TT_CDATA);
    MRB_UNDEF_ALLOCATOR(klass);
    mrb_undef_class_method_id(mrb, klass, MRB_SYM(new));
    mrb_iv_set(mrb, mrb_obj_value(klass), reflect_reflected_key(mrb), mrb_true_value());
    mrb_iv_set(mrb, mrb_obj_value(mrb->object_class), reflect_class_key<std::meta::dealias(Type)>(mrb), mrb_obj_value(klass));
    RClass *const methods = ::mrb_define_module_under_id(mrb, klass, reflect_sym<kInstanceMethods>(mrb));
    mrb_iv_set(mrb, mrb_obj_value(mrb->object_class), reflect_module_key<std::meta::dealias(Type)>(mrb), mrb_obj_value(methods));
    mrb_include_module(mrb, klass, methods);
    return klass;
}

template <std::meta::info Type, reflect_options Options, auto Instances>
RClass *reflect_define_complete_class(reflect_definition &definition, RClass *under);

template <std::meta::info Type, reflect_options Options, auto Instances>
RClass *reflect_define_class(reflect_definition &definition, RClass *const under)
{
    if constexpr (std::meta::is_complete_type(std::meta::dealias(Type))) return reflect_define_complete_class<Type, Options, Instances>(definition, under);
    else return reflect_define_opaque_class<Type>(definition, under);
}

template <std::meta::info Type, reflect_options Options, auto Instances>
RClass *reflect_define_complete_class(reflect_definition &definition, RClass *const under)
{
    mrb_state *const mrb = definition.mrb;
    using T = [:std::meta::dealias(Type):];
    RClass *const outer = reflect_enclosing_scope<Type>(definition, under);
    static constexpr auto direct = std::define_static_array(reflect_direct_bases(Type));
    RClass *superclass = mrb->object_class;
    if constexpr (direct.size() > 0) superclass = reflect_class<direct[0]>(definition);
    RClass *klass;
    if constexpr (std::meta::has_template_arguments(std::meta::dealias(Type))) {
        klass = mrb_class_new(mrb, superclass);
        definition.pending.emplace_back(&reflect_register_specialization<std::meta::dealias(Type)>, klass);
    } else if constexpr (std::meta::has_identifier(std::meta::dealias(Type))) {
        const mrb_sym name = reflect_intern<Type>(mrb);
        if (mrb_const_defined_at(mrb, mrb_obj_value(outer), name))
            mrb_raisef(mrb, E_NAME_ERROR, "%n is already defined in %C", name, outer);
        klass = ::mrb_define_class_under_id(mrb, outer, name, superclass);
    } else {
        klass = mrb_class_new(mrb, superclass);
    }
    MRB_SET_INSTANCE_TT(klass, MRB_TT_CDATA);
    mrb_iv_set(mrb, mrb_obj_value(klass), reflect_reflected_key(mrb), mrb_true_value());
    mrb_iv_set(mrb, mrb_obj_value(mrb->object_class), reflect_class_key<std::meta::dealias(std::meta::remove_cvref(Type))>(mrb), mrb_obj_value(klass));
    RClass *const methods = ::mrb_define_module_under_id(mrb, klass, reflect_sym<kInstanceMethods>(mrb));
    mrb_iv_set(mrb, mrb_obj_value(mrb->object_class), reflect_module_key<std::meta::dealias(std::meta::remove_cvref(Type))>(mrb), mrb_obj_value(methods));
    template for (constexpr std::meta::info base : direct) {
        reflect_class<base>(definition);
        mrb_include_module(mrb, methods, reflect_module<base>(mrb));
    }
    if constexpr (Options.virtual_overriders && requires { typename reflect_virtual_overrider<std::meta::dealias(Type)>::type; } &&
                  reflect_constructors<Type>().size() > 0) {
        MRB_DEFINE_ALLOCATOR(klass);
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            reflect_define_method<^^typename reflect_virtual_overrider<std::meta::dealias(Type)>::type, reflect_constructors<Type>()[I]...>(
                mrb, klass, reflect_sym<kInitialize>(mrb));
        }(std::make_index_sequence<reflect_constructors<Type>().size()>{});
    } else if constexpr (!std::is_abstract_v<T> && reflect_constructors<Type>().size() > 0) {
        MRB_DEFINE_ALLOCATOR(klass);
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            reflect_define_method<Type, reflect_constructors<Type>()[I]...>(mrb, klass, reflect_sym<kInitialize>(mrb));
        }(std::make_index_sequence<reflect_constructors<Type>().size()>{});
    } else if constexpr (std::is_default_constructible_v<T> && !std::is_abstract_v<T>) {
        MRB_DEFINE_ALLOCATOR(klass);
        ::mrb_define_method_id(mrb, klass, reflect_sym<kInitialize>(mrb),
                               [](mrb_state *const mrb, const mrb_value self) {
                                   reflect_lifetime_base &record = reflect_new_lifetime<T>(mrb, self);
                                   return reflect_translate_exceptions(mrb, [&] {
                                       reflect_adopt<T>(mrb, self, record, reflect_new<T>());
                                       return self;
                                   });
                               }, MRB_ARGS_NONE());
    } else {
        MRB_UNDEF_ALLOCATOR(klass);
    }
    if constexpr (std::is_copy_constructible_v<T> && std::is_destructible_v<T> && !std::is_abstract_v<T>) {
        ::mrb_define_method_id(mrb, klass, MRB_SYM(initialize_copy), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            mrb_value original;
            mrb_get_args(mrb, "o", &original);
            if (reflect_ptr<T>(mrb, original) == nullptr) mrb_raise(mrb, E_TYPE_ERROR, "wrong original");
            reflect_lifetime_base &record = reflect_new_lifetime<T>(mrb, self);
            T *const source = reflect_ptr<T>(mrb, original);
            if (source == nullptr) mrb_raise(mrb, E_TYPE_ERROR, "wrong original");
            return reflect_translate_exceptions(mrb, [&] {
                if constexpr (Options.virtual_overriders && requires { typename reflect_virtual_overrider<std::meta::dealias(Type)>::type; })
                    reflect_adopt<T>(mrb, self, record, static_cast<T *>(new typename reflect_virtual_overrider<std::meta::dealias(Type)>::type(static_cast<const T &>(*source))));
                else reflect_adopt<T>(mrb, self, record, reflect_new<T>(static_cast<const T &>(*source)));
                return self;
            });
        }, MRB_ARGS_REQ(1));
    } else {
        ::mrb_define_method_id(mrb, klass, MRB_SYM(initialize_copy), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            mrb_raisef(mrb, E_TYPE_ERROR, "can't copy %s", std::define_static_string(reflect_class_name(std::meta::dealias(Type))));
            std::unreachable();
        }, MRB_ARGS_REQ(1));
    }
    static constexpr auto instance_methods = std::define_static_array([] consteval {
        std::vector<std::meta::info> all = reflect_merged(reflect_members<Type>(), Instances, false);
        for (const std::meta::info f : reflect_operators_for<Type>())
            if (std::ranges::find(all, f) == all.end()) all.push_back(f);
        return all;
    }());
    template for (constexpr std::meta::info member : instance_methods) {
        if constexpr (reflect_first_of_its_name(instance_methods, member)) {
            static constexpr auto overloads = std::define_static_array(reflect_overloads_in(instance_methods, member));
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                reflect_define_method<Type, overloads[I]...>(mrb, methods, reflect_intern<member>(mrb));
            }(std::make_index_sequence<overloads.size()>{});
        }
    }
    constexpr auto declares = [](const std::meta::operators op) consteval {
        return std::ranges::any_of(instance_methods, [op](const std::meta::info m) { return std::meta::is_operator_function(m) && std::meta::operator_of(m) == op; });
    };
    if constexpr (std::ranges::any_of(instance_methods, [](const std::meta::info m) {
                      return std::meta::is_operator_function(m) && std::meta::operator_of(m) == std::meta::operators::op_spaceship && reflect_answers_as_ruby(m);
                  }))
        mrb_include_module(mrb, methods, mrb_module_get_id(mrb, MRB_SYM(Comparable)));
    reflect_define_comparisons<Type, declares(std::meta::operators::op_equals_equals), declares(std::meta::operators::op_spaceship)>(mrb, methods);
    template for (constexpr std::meta::info member : reflect_members<Type>()) {
        if constexpr (std::meta::is_operator_function(member) && std::meta::operator_of(member) == std::meta::operators::op_square_brackets &&
                      std::meta::parameters_of(member).size() == 1 && !std::meta::is_const(member) &&
                      std::meta::is_lvalue_reference_type(std::meta::return_type_of(member)) &&
                      !std::meta::is_const_type(std::meta::remove_reference(std::meta::return_type_of(member))))
            reflect_define_element_assignment<Type, member>(mrb, methods);
    }
    template for (constexpr std::meta::info field : reflect_fields<Type>())
        reflect_define_field<field>(mrb, methods);
    template for (constexpr std::meta::info function : reflect_conversion_functions<Type>())
        reflect_define_conversion_function<Type, function>(mrb, methods);
    RClass *const singleton = mrb_class_ptr(mrb_singleton_class(mrb, mrb_obj_value(klass)));
    static constexpr auto class_methods = std::define_static_array(reflect_merged(reflect_static_functions<Type>(), Instances, true));
    template for (constexpr std::meta::info function : class_methods) {
        if constexpr (reflect_first_of_its_name(class_methods, function)) {
            static constexpr auto overloads = std::define_static_array(reflect_overloads_in(class_methods, function));
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                reflect_define_method<Type, overloads[I]...>(mrb, singleton, reflect_intern<function>(mrb));
            }(std::make_index_sequence<overloads.size()>{});
        }
    }
    template for (constexpr std::meta::info member : reflect_static_data_members<Type>())
        reflect_define_static_data_member<member>(mrb, klass, singleton);
    mrb_include_module(mrb, klass, methods);
    if constexpr (std::ranges::any_of(reflect_members<Type>(), [](const std::meta::info m) {
                      return std::meta::is_operator_function(m) && std::meta::operator_of(m) == std::meta::operators::op_parentheses;
                  })) {
        ::mrb_define_method_id(mrb, methods, MRB_SYM(to_proc), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            RProc *const proc = mrb_proc_new_cfunc_with_env(mrb, [](mrb_state *const mrb, const mrb_value) -> mrb_value {
                mrb_value *argv;
                mrb_int argc;
                mrb_get_args(mrb, "*", &argv, &argc);
                return mrb_funcall_argv(mrb, mrb_cfunc_env_get(mrb, 0), MRB_SYM(call), argc, argv);
            }, 1, &self);
            proc->flags |= MRB_PROC_STRICT;
            mrb_proc_set_cfunc_aspec(proc, reflect_call_aspec<Type>());
            return mrb_obj_value(proc);
        }, MRB_ARGS_NONE());
    }
    reflect_define_conversions<T>(mrb, klass);
    if constexpr (std::is_copy_assignable_v<T>) reflect_define_replace<T>(mrb, klass);
    template for (constexpr std::meta::info nested : std::define_static_array(Options.nested_types ? reflect_nested_types(Type) : reflect_nested_types_used<Type>())) {
        if (!mrb_iv_defined(mrb, mrb_obj_value(mrb->object_class), reflect_class_key<nested>(mrb))) {
            if constexpr (std::meta::is_enum_type(nested)) reflect_define_enum<nested>(definition, mrb->object_class);
            else reflect_define_class<nested, Options>(definition, mrb->object_class);
        }
    }
    return klass;
}

template <std::meta::info Operand, std::meta::info Namespace, auto Instances>
void reflect_define_operators(reflect_definition &definition)
{
    mrb_state *const mrb = definition.mrb;
    reflect_class<Operand>(definition);
    RClass *const methods = reflect_module<Operand>(mrb);
    static constexpr auto added = std::define_static_array([] consteval {
        std::vector<std::meta::info> found;
        for (const std::meta::info f : reflect_free_operators(Namespace, Instances))
            if (reflect_operand_class(f) == Operand) found.push_back(f);
        return found;
    }());
    static constexpr auto own = std::define_static_array([] consteval {
        std::vector<std::meta::info> all(reflect_members<Operand>().begin(), reflect_members<Operand>().end());
        for (const std::meta::info f : reflect_operators_for<Operand>()) all.push_back(f);
        for (const std::meta::info f : added)
            if (std::ranges::find(all, f) == all.end()) all.push_back(f);
        return all;
    }());
    template for (constexpr std::meta::info function : added) {
        if constexpr (reflect_first_of_its_name(added, function)) {
            static constexpr auto overloads = std::define_static_array(reflect_overloads_in(own, function));
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                reflect_define_method<Operand, overloads[I]...>(mrb, methods, reflect_intern<function>(mrb));
            }(std::make_index_sequence<overloads.size()>{});
        }
    }
}

template <class... T>
struct reflect_types {
};

template <std::meta::info Function>
inline constexpr std::span<const std::meta::info> reflect_varargs_lists{};

template <std::meta::info Function, class Fixed, class... Rest>
struct reflect_varargs_instance;

template <std::meta::info Function, class... Fixed, class... Rest>
struct reflect_varargs_instance<Function, reflect_types<Fixed...>, Rest...> {
    static auto call(Fixed... fixed, Rest... rest) -> typename [:std::meta::return_type_of(Function):] { return [:Function:](fixed..., rest...); }
};

consteval std::vector<std::meta::info> reflect_varargs_functions(const std::meta::info scope)
{
    std::vector<std::meta::info> found;
    for (const std::meta::info m : std::meta::members_of(scope, std::meta::access_context::current()))
        if (std::meta::is_function(m) && std::meta::is_vararg_function(m) && std::meta::has_identifier(m) && !std::meta::identifier_of(m).starts_with("_"))
            found.push_back(m);
    return found;
}

consteval std::vector<std::meta::info> reflect_varargs_calls(const std::meta::info function, const std::span<const std::meta::info> lists)
{
    std::vector<std::meta::info> fixed;
    for (const std::meta::info p : std::meta::parameters_of(function)) fixed.push_back(std::meta::type_of(p));
    const std::meta::info fixed_types = std::meta::substitute(^^reflect_types, fixed);
    std::vector<std::meta::info> calls;
    for (const std::meta::info list : lists) {
        std::vector<std::meta::info> arguments{std::meta::reflect_constant(function), fixed_types};
        for (const std::meta::info a : std::meta::template_arguments_of(list)) arguments.push_back(a);
        const std::meta::info instance = std::meta::substitute(^^reflect_varargs_instance, arguments);
        for (const std::meta::info m : std::meta::members_of(instance, std::meta::access_context::unchecked()))
            if (std::meta::is_function(m) && std::meta::has_identifier(m) && std::meta::identifier_of(m) == "call") {
                if (!reflect_call_supported(m)) throw "reflect_varargs names a type that no Ruby value converts to";
                calls.push_back(m);
            }
    }
    return calls;
}

template <std::meta::info Namespace, auto Instances = std::array<std::meta::info, 0>{}, bool Listed = true>
RClass *reflect_define_namespace(reflect_definition &definition, RClass *const under)
{
    mrb_state *const mrb = definition.mrb;
    RClass *outer = under;
    template for (constexpr std::meta::info scope : std::define_static_array(reflect_namespaces(Namespace)))
        outer = ::mrb_define_module_under_id(mrb, outer, reflect_intern<scope>(mrb));
    RClass *const module = ::mrb_define_module_under_id(mrb, outer, reflect_intern<Namespace>(mrb));
    RClass *const singleton = mrb_class_ptr(mrb_singleton_class(mrb, mrb_obj_value(module)));
    static constexpr auto functions = std::define_static_array([] consteval {
        std::vector<std::meta::info> all;
        for (const std::meta::info f : reflect_merged(Listed ? reflect_members<Namespace>() : std::span<const std::meta::info>{}, Instances, false))
            if (reflect_skip(f) == 0) all.push_back(f);
        return all;
    }());
    static constexpr auto variables = std::define_static_array([] consteval {
        std::vector<std::meta::info> all = Listed ? reflect_variables(Namespace) : std::vector<std::meta::info>{};
        for (const std::meta::info v : Instances)
            if (std::meta::is_variable(v) && std::ranges::find(all, v) == all.end()) all.push_back(v);
        return all;
    }());
    template for (constexpr std::meta::info variable : variables)
        reflect_define_static_data_member<variable>(mrb, module, singleton);
    template for (constexpr std::meta::info function : functions) {
        if constexpr (reflect_first_of_its_name(functions, function)) {
            static constexpr auto overloads = std::define_static_array(reflect_overloads_in(functions, function));
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                reflect_define_method<Namespace, overloads[I]...>(mrb, singleton, reflect_intern<function>(mrb));
            }(std::make_index_sequence<overloads.size()>{});
        }
    }
    template for (constexpr std::meta::info function : std::define_static_array(Listed ? reflect_varargs_functions(Namespace) : std::vector<std::meta::info>{})) {
        static constexpr auto calls = std::define_static_array(reflect_varargs_calls(function, reflect_varargs_lists<function>));
        if constexpr (calls.empty()) {
            ::mrb_define_method_id(mrb, singleton, reflect_intern<function>(mrb), [](mrb_state *const mrb, const mrb_value) -> mrb_value {
                mrb_raisef(mrb, E_NOTIMP_ERROR, "the arguments of %n after its named parameters are not declared", mrb_get_mid(mrb));
                std::unreachable();
            }, MRB_ARGS_ANY());
        } else {
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                reflect_define_method<std::meta::parent_of(calls[0]), calls[I]...>(mrb, singleton, reflect_intern<function>(mrb));
            }(std::make_index_sequence<calls.size()>{});
        }
    }
    template for (constexpr std::meta::info operand : std::define_static_array(reflect_operand_classes(reflect_free_operators(Namespace, Instances))))
        reflect_define_operators<operand, Namespace, Instances>(definition);
    return module;
}

template <auto Classes, reflect_options Options = reflect_options{}>
void reflect_define(mrb_state *const mrb, RClass *const under = nullptr)
{
#if defined(MRB_CPP_REFLECTOR_GENERATE)
    if constexpr (Options.virtual_overriders) static_cast<void>(reflect_virtual_overriders_printed<Classes>);
    return;
#endif
    reflect_definition definition(mrb);
    template for (constexpr std::meta::info type : std::define_static_array(reflect_scopes(Classes))) {
        constexpr auto instances = reflect_instances<type, Classes, Options.templates>();
        if constexpr (std::meta::is_namespace(type))
            reflect_define_namespace<type, instances, std::ranges::find(Classes, type) != Classes.end()>(definition, under != nullptr ? under : mrb->object_class);
        else if (!mrb_iv_defined(mrb, mrb_obj_value(mrb->object_class), reflect_class_key<std::meta::dealias(std::meta::remove_cvref(type))>(mrb)))
        {
            if constexpr (std::meta::is_enum_type(std::meta::dealias(type))) reflect_define_enum<type>(definition, under != nullptr ? under : mrb->object_class);
            else reflect_define_class<type, Options, instances>(definition, under != nullptr ? under : mrb->object_class);
        }
    }
    definition.finish();
}

}

#endif
