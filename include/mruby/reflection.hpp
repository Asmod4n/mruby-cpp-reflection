#pragma once
#if defined(__cpp_impl_reflection)

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/data.h>
#include <mruby/error.h>
#include <mruby/hash.h>
#include <mruby/proc.h>
#include <mruby/string.h>
#include <mruby/variable.h>

#include <mruby/cpp_helpers.hpp>
#include <mruby/cpp_to_mrb_value.hpp>
#include <mruby/mrb_value_to_cpp.hpp>
#include <mruby/reflect_presyms.hpp>

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <meta>
#include <new>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
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

template <class T>
const mrb_data_type &reflect_data_type_owned()
{
    static constexpr auto name = std::define_static_string(reflect_class_name(^^T));
    static const mrb_data_type type{name, [](mrb_state *const mrb, void *const p) {
                                        static_cast<T *>(p)->~T();
                                        mrb_free(mrb, p);
                                    }};
    return type;
}

template <class T>
const mrb_data_type &reflect_data_type_borrowed()
{
    static constexpr auto name = std::define_static_string(reflect_class_name(^^T));
    static const mrb_data_type type{name, nullptr};
    return type;
}

template <std::meta::info Type>
RClass *reflect_define_class(mrb_state *mrb, RClass *super);

template <std::meta::info Bare>
RClass *&reflect_class_slot()
{
    static RClass *klass = nullptr;
    return klass;
}

template <std::meta::info Bare>
RClass *&reflect_module_slot()
{
    static RClass *module = nullptr;
    return module;
}

template <std::meta::info Type>
RClass *reflect_class(mrb_state *const mrb)
{
    RClass *&slot = reflect_class_slot<std::meta::dealias(std::meta::remove_cvref(Type))>();
    if (slot == nullptr) reflect_define_class<std::meta::dealias(std::meta::remove_cvref(Type))>(mrb, mrb->object_class);
    return slot;
}

struct reflect_upcast {
    const mrb_data_type *type;
    void *(*to_base)(void *);
};

template <class T>
std::vector<reflect_upcast> &reflect_upcasts()
{
    static std::vector<reflect_upcast> upcasts;
    return upcasts;
}

template <class T>
T *reflect_ptr(mrb_state *const mrb, const mrb_value v)
{
    void *const owned = mrb_data_check_get_ptr(mrb, v, &reflect_data_type_owned<T>());
    if (owned != nullptr) return static_cast<T *>(owned);
    void *const borrowed = mrb_data_check_get_ptr(mrb, v, &reflect_data_type_borrowed<T>());
    if (borrowed != nullptr || mrb_type(v) != MRB_TT_CDATA || DATA_PTR(v) == nullptr) return static_cast<T *>(borrowed);
    for (const reflect_upcast &upcast : reflect_upcasts<T>())
        if (upcast.type == DATA_TYPE(v)) return static_cast<T *>(upcast.to_base(DATA_PTR(v)));
    return nullptr;
}

template <class T>
mrb_value reflect_object(mrb_state *const mrb, T &&value, const bool frozen = false)
{
    using U = std::remove_cvref_t<T>;
    RData *const data = mrb_data_object_alloc(mrb, reflect_class<^^U>(mrb), nullptr, &reflect_data_type_owned<U>());
    U *const kept = static_cast<U *>(mrb_malloc(mrb, sizeof(U)));
    new (kept) U(std::forward<T>(value));
    data->data = kept;
    if (frozen) mrb_obj_freeze(mrb, mrb_obj_value(data));
    return mrb_obj_value(data);
}

template <class T>
mrb_value reflect_borrowed(mrb_state *const mrb, T *const ref, const mrb_value owner, const bool frozen)
{
    RData *const data = mrb_data_object_alloc(mrb, reflect_class<^^T>(mrb), ref, &reflect_data_type_borrowed<T>());
    const mrb_value object = mrb_obj_value(data);
    mrb_iv_set(mrb, object, reflect_sym<kOwner>(mrb), owner);
    if (frozen) mrb_obj_freeze(mrb, mrb_obj_value(data));
    return object;
}

template <class T>
mrb_value reflect_view(mrb_state *const mrb, T view, const mrb_value owner, const bool frozen)
{
    const mrb_value object = reflect_object<T>(mrb, std::move(view), frozen);
    mrb_iv_set(mrb, object, reflect_sym<kOwner>(mrb), owner);
    return object;
}

template <class T, bool Move = false>
struct reflect_holder {
    static constexpr bool moves = Move;
    std::unique_ptr<T> temporary;
    T *ptr = nullptr;
};

template <class T>
std::unique_ptr<T> reflect_implicit_conversion(mrb_state *mrb, mrb_value v);

template <class T>
bool reflect_implicitly_converts(mrb_state *mrb, mrb_value v);

template <std::meta::info Function, bool Converting = true>
bool reflect_get_args_match(mrb_state *mrb, std::span<const mrb_value> argv);

template <std::meta::info Parameter, bool Converting = true>
auto reflect_argument(mrb_state *const mrb, const mrb_value v)
{
    constexpr std::meta::info type = std::meta::type_of(Parameter);
    using T = [:reflect_bare(type):];
    if constexpr (std::meta::is_pointer_type(std::meta::dealias(type))) {
        using P = [:std::meta::dealias(std::meta::remove_cv(std::meta::remove_pointer(std::meta::dealias(type)))):];
        if (mrb_nil_p(v)) return static_cast<P *>(nullptr);
        P *const p = reflect_ptr<P>(mrb, v);
        if (p == nullptr) mrb_raisef(mrb, E_TYPE_ERROR, "%s wanted", std::define_static_string(reflect_class_name(std::meta::dealias(^^P))));
        if constexpr (reflect_mutates(type)) mrb_check_frozen(mrb, mrb_obj_ptr(v));
        return p;
    } else if constexpr (std::meta::is_lvalue_reference_type(type)) {
        reflect_holder<T> held;
        held.ptr = reflect_ptr<T>(mrb, v);
        if (held.ptr != nullptr) {
            if constexpr (reflect_mutates(type)) mrb_check_frozen(mrb, mrb_obj_ptr(v));
        } else if constexpr (!reflect_mutates(type) && mrbcpp::value_converter::convertible_from_mrb<T> && std::is_move_constructible_v<T>) {
            held.temporary = std::make_unique<T>(mrb_value_to<T>(mrb, v));
            held.ptr = held.temporary.get();
        } else {
            if constexpr (Converting && !reflect_mutates(type)) held.temporary = reflect_implicit_conversion<T>(mrb, v);
            if (held.temporary == nullptr) mrb_raisef(mrb, E_TYPE_ERROR, "%s wanted", std::define_static_string(reflect_class_name(std::meta::dealias(^^T))));
            held.ptr = held.temporary.get();
        }
        return held;
    } else {
        reflect_holder<T, true> held;
        if (T *const p = reflect_ptr<T>(mrb, v); p != nullptr) {
            held.temporary = std::make_unique<T>(*p);
        } else if constexpr (mrbcpp::value_converter::convertible_from_mrb<T>) {
            held.temporary = std::make_unique<T>(mrb_value_to<T>(mrb, v));
        } else {
            if constexpr (Converting) held.temporary = reflect_implicit_conversion<T>(mrb, v);
            if (held.temporary == nullptr) mrb_raisef(mrb, E_TYPE_ERROR, "%s wanted", std::define_static_string(reflect_class_name(std::meta::dealias(^^T))));
        }
        held.ptr = held.temporary.get();
        return held;
    }
}

template <class H>
decltype(auto) reflect_pass(H &held)
{
    if constexpr (requires { held.ptr; held.temporary; }) {
        if constexpr (H::moves) return std::move(*held.ptr);
        else return (*held.ptr);
    } else return std::move(held);
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
    constexpr auto format = reflect_get_args_format<Function, Skip, Count>();
    return [&]<std::size_t... I>(std::index_sequence<I...>) {
        constexpr auto slot = []<std::meta::info P>() consteval {
            using T = [:reflect_bare(std::meta::type_of(P)):];
            if constexpr (reflect_is_object(std::meta::type_of(P)) || std::meta::is_pointer_type(std::meta::dealias(std::meta::type_of(P))) && !std::same_as<T, const char *>)
                return std::type_identity<mrb_value>{};
            else if constexpr (std::same_as<T, std::string_view> || std::same_as<T, std::string>) return std::type_identity<std::pair<const char *, mrb_int>>{};
            else if constexpr (std::same_as<T, std::span<const mrb_value>>) return std::type_identity<std::pair<const mrb_value *, mrb_int>>{};
            else if constexpr (std::same_as<T, bool>) return std::type_identity<mrb_bool>{};
            else if constexpr (std::integral<T>) return std::type_identity<mrb_int>{};
            else if constexpr (std::floating_point<T>) return std::type_identity<mrb_float>{};
            else return std::type_identity<T>{};
        };
        std::tuple<typename decltype(slot.template operator()<std::meta::parameters_of(Function)[I + Skip]>())::type...> slots{};
        std::apply([&](auto *const... p) { ::mrb_get_args(mrb, format.data(), p...); },
                   std::tuple_cat([&]<std::size_t J>() {
                       auto &s = std::get<J>(slots);
                       if constexpr (requires { s.first; s.second; }) return std::tuple{&s.first, &s.second};
                       else return std::tuple{&s};
                   }.template operator()<I>()...));
        const auto converted = [&]<std::size_t J>() {
            auto &s = std::get<J>(slots);
            constexpr std::meta::info P = std::meta::parameters_of(Function)[J + Skip];
            using T = [:reflect_bare(std::meta::type_of(P)):];
            if constexpr (std::same_as<std::remove_cvref_t<decltype(s)>, mrb_value> && !std::same_as<T, mrb_value>) return reflect_argument<P>(mrb, s);
            else if constexpr (std::same_as<T, std::string_view>) return std::string_view(s.first, static_cast<std::size_t>(s.second));
            else if constexpr (std::same_as<T, std::string>) return std::string(s.first, static_cast<std::size_t>(s.second));
            else if constexpr (std::same_as<T, std::span<const mrb_value>>) return std::span(s.first, static_cast<std::size_t>(s.second));
            else return static_cast<T>(s);
        };
        return std::tuple<decltype(converted.template operator()<I>())...>(converted.template operator()<I>()...);
    }(std::make_index_sequence<Count>{});
}

template <class R>
mrb_value reflect_result(mrb_state *const mrb, const mrb_value self, R &&value)
{
    using T = std::remove_cvref_t<R>;
    constexpr bool frozen = std::is_const_v<std::remove_reference_t<R>>;
    if constexpr (std::same_as<T, mrb_value>) return value;
    else if constexpr (std::same_as<T, bool> || std::is_arithmetic_v<T>) return cpp_to_mrb_value(mrb, value);
    else if constexpr (std::same_as<T, std::strong_ordering> || std::same_as<T, std::weak_ordering> || std::same_as<T, std::partial_ordering>)
        return value < 0 ? mrb_fixnum_value(-1) : value > 0 ? mrb_fixnum_value(1) : value == 0 ? mrb_fixnum_value(0) : mrb_nil_value();
    else if constexpr (std::is_pointer_v<T>) {
        using P = std::remove_cv_t<std::remove_pointer_t<T>>;
        if constexpr (std::same_as<P, char>) return value == nullptr ? mrb_nil_value() : mrb_str_new_cstr(mrb, value);
        else if constexpr (mrbcpp::value_converter::is_std_pair<P>::value) return value == nullptr ? mrb_nil_value() : reflect_result(mrb, self, *value);
        else return value == nullptr ? mrb_nil_value() : reflect_borrowed<P>(mrb, const_cast<P *>(value), self, std::is_const_v<std::remove_pointer_t<T>>);
    } else if constexpr (mrbcpp::value_converter::is_std_pair<T>::value) {
        const mrb_value pair = mrb_ary_new_capa(mrb, 2);
        mrb_ary_push(mrb, pair, reflect_result(mrb, self, value.first));
        mrb_ary_push(mrb, pair, reflect_result(mrb, self, value.second));
        return pair;
    } else if constexpr (std::is_lvalue_reference_v<R>) return reflect_borrowed<T>(mrb, const_cast<T *>(&value), self, frozen);
    else if constexpr (reflect_is_view(^^T)) return reflect_view<T>(mrb, std::move(value), self, frozen);
    else return reflect_object<T>(mrb, std::move(value), frozen);
}

template <class Call>
mrb_value reflect_translate_exceptions(mrb_state *const mrb, const Call &call)
{
    RClass *kind;
    std::string what;
    try {
        return call();
    } catch (const std::invalid_argument &e) {
        kind = E_ARGUMENT_ERROR;
        what = e.what();
    } catch (const std::out_of_range &e) {
        kind = E_INDEX_ERROR;
        what = e.what();
    } catch (const std::range_error &e) {
        kind = E_RANGE_ERROR;
        what = e.what();
    } catch (const std::overflow_error &e) {
        kind = E_RANGE_ERROR;
        what = e.what();
    } catch (const std::underflow_error &e) {
        kind = E_RANGE_ERROR;
        what = e.what();
    } catch (const std::exception &e) {
        kind = E_RUNTIME_ERROR;
        what = e.what();
    }
    mrb_exc_raise(mrb, mrb_exc_new(mrb, kind, what.data(), static_cast<mrb_int>(what.size())));
}

template <std::meta::info Type, std::meta::info Function, std::size_t Count = std::meta::parameters_of(Function).size()>
mrb_value reflect_call(mrb_state *const mrb, const mrb_value self)
{
    using T = [:std::meta::dealias(Type):];
    if constexpr (std::meta::is_constructor(Function)) {
        auto args = reflect_get_args<Function, 0, Count>(mrb);
        T *const kept = static_cast<T *>(mrb_malloc(mrb, sizeof(T)));
        return reflect_translate_exceptions(mrb, [&] {
            try {
                std::apply([&](auto &...held) { new (kept) T(reflect_pass(held)...); }, args);
            } catch (...) {
                mrb_free(mrb, kept);
                throw;
            }
            mrb_data_init(self, kept, &reflect_data_type_owned<T>());
            return self;
        });
    } else if constexpr (std::meta::is_static_member(Function)) {
        auto args = reflect_get_args<Function, 0, Count>(mrb);
        return reflect_translate_exceptions(mrb, [&] {
            if constexpr (std::meta::return_type_of(Function) == ^^void) {
                std::apply([&](auto &...held) { [:Function:](reflect_pass(held)...); }, args);
                return mrb_nil_value();
            } else {
                return std::apply([&](auto &...held) -> mrb_value { return reflect_result(mrb, self, [:Function:](reflect_pass(held)...)); }, args);
            }
        });
    } else {
        if constexpr (!std::meta::is_const(Function)) mrb_check_frozen(mrb, mrb_obj_ptr(self));
        T *const object = reflect_ptr<T>(mrb, self);
        if (object == nullptr) mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
        auto args = reflect_get_args<Function, 0, Count>(mrb);
        return reflect_translate_exceptions(mrb, [&] {
            if constexpr (std::meta::return_type_of(Function) == ^^void) {
                std::apply([&](auto &...held) { object->[:Function:](reflect_pass(held)...); }, args);
                return mrb_nil_value();
            } else if constexpr (std::meta::is_reference_type(std::meta::return_type_of(Function)) &&
                                 reflect_bare(std::meta::return_type_of(Function)) == std::meta::dealias(^^T)) {
                std::apply([&](auto &...held) { object->[:Function:](reflect_pass(held)...); }, args);
                return self;
            } else {
                return std::apply([&](auto &...held) -> mrb_value { return reflect_result(mrb, self, object->[:Function:](reflect_pass(held)...)); }, args);
            }
        });
    }
}

template <std::meta::info Function, bool Converting>
bool reflect_get_args_match(mrb_state *const mrb, const std::span<const mrb_value> argv)
{
    constexpr auto format = reflect_get_args_format<Function>();
    constexpr std::size_t letters = std::meta::parameters_of(Function).size();
    constexpr bool rest = reflect_rest(Function);
    constexpr std::size_t required = rest ? letters - 1 : reflect_required(Function);
    if (argv.size() < required || (!rest && argv.size() > letters)) return false;
    bool fits = true;
    std::size_t at = 0;
    template for (constexpr std::meta::info P : std::define_static_array(std::meta::parameters_of(Function))) {
        if (fits && at < argv.size() && !(rest && at == letters - 1)) {
            const mrb_value v = argv[at];
            constexpr char letter = reflect_get_args_letter(std::meta::type_of(P));
            if constexpr (letter == 'i' || letter == 'f') fits = mrb_integer_p(v) || mrb_float_p(v);
            else if constexpr (letter == 's' || letter == 'z') fits = mrb_string_p(v);
            else if constexpr (letter == 'n') fits = mrb_symbol_p(v) || mrb_string_p(v);
            else if constexpr (letter == 'c') fits = mrb_class_p(v) || mrb_module_p(v);
            else if constexpr (letter == 'o' && reflect_is_object(std::meta::type_of(P))) {
                using T = [:reflect_bare(std::meta::type_of(P)):];
                fits = reflect_ptr<T>(mrb, v) != nullptr || (!reflect_mutates(std::meta::type_of(P)) && mrbcpp::value_converter::convertible_from_mrb<T> ||
                                                        (Converting && !reflect_mutates(std::meta::type_of(P)) && reflect_implicitly_converts<T>(mrb, v)));
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
                auto held = reflect_argument<P, false>(mrb, v);
                made = std::make_unique<T>(reflect_pass(held));
            } else made = std::make_unique<T>(mrb_value_to<U>(mrb, v));
        }
    }
    return made;
}

template <std::meta::info Type, std::meta::info Function>
mrb_value reflect_call_given(mrb_state *const mrb, const mrb_value self)
{
    constexpr std::size_t total = std::meta::parameters_of(Function).size();
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

template <std::meta::info Type, std::meta::info... Overloads>
void reflect_define_method(mrb_state *const mrb, RClass *const klass, const mrb_sym name)
{
    constexpr std::meta::info first = std::array{Overloads...}[0];
    if constexpr (sizeof...(Overloads) == 1) {
        constexpr std::size_t total = std::meta::parameters_of(first).size();
        constexpr mrb_aspec aspec = reflect_rest(first) ? (MRB_ARGS_REQ(total - 1) | MRB_ARGS_REST())
                                                        : (MRB_ARGS_REQ(reflect_required(first)) | MRB_ARGS_OPT(total - reflect_required(first)));
        ::mrb_define_method_id(mrb, klass, name,
                               [](mrb_state *const mrb, const mrb_value self) { return reflect_call_given<Type, first>(mrb, self); }, aspec);
    } else {
        ::mrb_define_method_id(mrb, klass, name,
                               [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            const std::span<const mrb_value> argv(mrb_get_argv(mrb), static_cast<std::size_t>(mrb_get_argc(mrb)));
            mrb_value answer = mrb_undef_value();
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
            if (mrb_undef_p(answer)) mrb_argnum_error(mrb, mrb_get_argc(mrb), 0, -1);
            return answer;
        }, MRB_ARGS_ANY());
    }
}

template <std::meta::info Field>
void reflect_define_field(mrb_state *const mrb, RClass *const klass)
{
    using T = [:std::meta::parent_of(Field):];
    ::mrb_define_method_id(mrb, klass, reflect_intern<Field>(mrb), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        T *const object = reflect_ptr<T>(mrb, self);
        if (object == nullptr) mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
        return reflect_result(mrb, self, object->[:Field:]);
    }, MRB_ARGS_NONE());
    if constexpr (!std::meta::is_const_type(std::meta::type_of(Field)) && std::is_copy_assignable_v<typename [:reflect_bare(std::meta::type_of(Field)):]>) {
        constexpr std::string_view name = std::meta::identifier_of(Field);
        constexpr auto setter = std::define_static_string(std::string(name) + "=");
        constexpr mrb_sym presym = reflect_presym(setter);
        const mrb_sym sym = presym != 0 ? presym : mrb_intern_static(mrb, setter, name.size() + 1);
        ::mrb_define_method_id(mrb, klass, sym, [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            mrb_check_frozen(mrb, mrb_obj_ptr(self));
            T *const object = reflect_ptr<T>(mrb, self);
            if (object == nullptr) mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
            mrb_value v;
            mrb_get_args(mrb, "o", &v);
            using F = [:reflect_bare(std::meta::type_of(Field)):];
            if (F *const p = reflect_ptr<F>(mrb, v); p != nullptr) object->[:Field:] = *p;
            else if constexpr (mrbcpp::value_converter::convertible_from_mrb<F>) object->[:Field:] = mrb_value_to<F>(mrb, v);
            else mrb_raise(mrb, E_TYPE_ERROR, "wrong type");
            return v;
        }, MRB_ARGS_REQ(1));
    }
}

template <class T>
concept reflect_bytes = std::ranges::contiguous_range<T> && std::same_as<std::remove_cv_t<std::ranges::range_value_t<T>>, char>;

template <class T>
concept reflect_map = requires { typename T::key_type; typename T::mapped_type; } && std::ranges::range<T>;

template <class T>
void reflect_define_conversions(mrb_state *const mrb, RClass *const klass)
{
    if constexpr (reflect_bytes<T>) {
        constexpr auto to_s = [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            const T *const object = reflect_ptr<T>(mrb, self);
            return mrb_str_new(mrb, std::ranges::data(*object), static_cast<mrb_int>(std::ranges::size(*object)));
        };
        ::mrb_define_method_id(mrb, klass, reflect_sym<kToS>(mrb), to_s, MRB_ARGS_NONE());
    } else if constexpr (reflect_map<T>) {
        constexpr auto to_h = [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            T *const object = reflect_ptr<T>(mrb, self);
            const mrb_value hash = mrb_hash_new_capa(mrb, static_cast<mrb_int>(object->size()));
            for (auto &[key, mapped] : *object) {
                if constexpr (reflect_bytes<std::remove_cvref_t<decltype(key)>>)
                    mrb_hash_set(mrb, hash, mrb_str_new(mrb, std::ranges::data(key), static_cast<mrb_int>(std::ranges::size(key))), reflect_result(mrb, self, mapped));
                else
                    mrb_hash_set(mrb, hash, reflect_result(mrb, self, key), reflect_result(mrb, self, mapped));
            }
            return hash;
        };
        ::mrb_define_method_id(mrb, klass, reflect_sym<kToH>(mrb), to_h, MRB_ARGS_NONE());
    } else if constexpr (std::ranges::range<T>) {
        constexpr auto each = [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            mrb_value block;
            mrb_get_args(mrb, "&!", &block);
            T *const object = reflect_ptr<T>(mrb, self);
            for (auto &element : *object) mrb_yield(mrb, block, reflect_result(mrb, self, element));
            return self;
        };
        constexpr auto to_a = [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            T *const object = reflect_ptr<T>(mrb, self);
            const mrb_value array = mrb_ary_new(mrb);
            for (auto &element : *object) mrb_ary_push(mrb, array, reflect_result(mrb, self, element));
            return array;
        };
        ::mrb_define_method_id(mrb, klass, reflect_sym<kEach>(mrb), each, MRB_ARGS_BLOCK());
        ::mrb_define_method_id(mrb, klass, reflect_sym<kToA>(mrb), to_a, MRB_ARGS_NONE());
        if (mrb_class_defined_id(mrb, reflect_sym<kEnumerable>(mrb)))
            mrb_include_module(mrb, klass, mrb_module_get_id(mrb, reflect_sym<kEnumerable>(mrb)));
    }
}

template <class T>
void reflect_define_replace(mrb_state *const mrb, RClass *const klass)
{
    ::mrb_define_method_id(mrb, klass, reflect_sym<kReplace>(mrb), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        mrb_check_frozen(mrb, mrb_obj_ptr(self));
        T *const object = reflect_ptr<T>(mrb, self);
        if (object == nullptr) mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
        mrb_value v;
        mrb_get_args(mrb, "o", &v);
        if (T *const p = reflect_ptr<T>(mrb, v); p != nullptr) *object = *p;
        else if constexpr (mrbcpp::value_converter::convertible_from_mrb<T>) *object = mrb_value_to<T>(mrb, v);
        else mrb_raise(mrb, E_TYPE_ERROR, "wrong type");
        return self;
    }, MRB_ARGS_REQ(1));
}

template <std::meta::info Member>
void reflect_define_static_data_member(mrb_state *const mrb, RClass *const singleton)
{
    ::mrb_define_method_id(mrb, singleton, reflect_intern<Member>(mrb), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        return reflect_result(mrb, self, [:Member:]);
    }, MRB_ARGS_NONE());
    if constexpr (!std::meta::is_const_type(std::meta::type_of(Member)) && std::is_copy_assignable_v<typename [:reflect_bare(std::meta::type_of(Member)):]>) {
        constexpr auto setter = std::define_static_string(std::string(std::meta::identifier_of(Member)) + "=");
        constexpr mrb_sym presym = reflect_presym(setter);
        const mrb_sym sym = presym != 0 ? presym : mrb_intern_static(mrb, setter, std::meta::identifier_of(Member).size() + 1);
        ::mrb_define_method_id(mrb, singleton, sym, [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            using F = [:reflect_bare(std::meta::type_of(Member)):];
            mrb_value v;
            mrb_get_args(mrb, "o", &v);
            if (F *const p = reflect_ptr<F>(mrb, v); p != nullptr) [:Member:] = *p;
            else if constexpr (mrbcpp::value_converter::convertible_from_mrb<F>) [:Member:] = mrb_value_to<F>(mrb, v);
            else mrb_raise(mrb, E_TYPE_ERROR, "wrong type");
            return v;
        }, MRB_ARGS_REQ(1));
    }
}

template <std::meta::info Type, std::meta::info Subscript>
void reflect_define_element_assignment(mrb_state *const mrb, RClass *const methods)
{
    ::mrb_define_method_id(mrb, methods, mrb_intern_lit(mrb, "[]="), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        using T = [:std::meta::dealias(Type):];
        using E = [:reflect_bare(std::meta::return_type_of(Subscript)):];
        mrb_check_frozen(mrb, mrb_obj_ptr(self));
        T *const object = reflect_ptr<T>(mrb, self);
        if (object == nullptr) mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
        mrb_value index, v;
        mrb_get_args(mrb, "oo", &index, &v);
        auto held = reflect_argument<std::meta::parameters_of(Subscript)[0]>(mrb, index);
        E &element = object->[:Subscript:](reflect_pass(held));
        if (E *const p = reflect_ptr<E>(mrb, v); p != nullptr) element = *p;
        else if constexpr (mrbcpp::value_converter::convertible_from_mrb<E>) element = mrb_value_to<E>(mrb, v);
        else mrb_raise(mrb, E_TYPE_ERROR, "wrong type");
        return v;
    }, MRB_ARGS_REQ(2));
}

template <std::meta::info Type>
RClass *reflect_define_class(mrb_state *const mrb, RClass *const under)
{
    using T = [:std::meta::dealias(Type):];
    RClass *outer = under;
    template for (constexpr std::meta::info scope : std::define_static_array(reflect_namespaces(Type)))
        outer = ::mrb_define_module_under_id(mrb, outer, reflect_intern<scope>(mrb));
    const mrb_sym name = reflect_intern<Type>(mrb);
    if (mrb_const_defined_at(mrb, mrb_obj_value(outer), name))
        mrb_raisef(mrb, E_NAME_ERROR, "%n is already defined in %C", name, outer);
    static constexpr auto direct = std::define_static_array(reflect_direct_bases(Type));
    RClass *superclass = mrb->object_class;
    if constexpr (direct.size() > 0) superclass = reflect_class<direct[0]>(mrb);
    RClass *const klass = ::mrb_define_class_under_id(mrb, outer, name, superclass);
    MRB_SET_INSTANCE_TT(klass, MRB_TT_CDATA);
    reflect_class_slot<std::meta::dealias(std::meta::remove_cvref(Type))>() = klass;
    RClass *const methods = ::mrb_define_module_under_id(mrb, klass, reflect_sym<kInstanceMethods>(mrb));
    reflect_module_slot<std::meta::dealias(std::meta::remove_cvref(Type))>() = methods;
    template for (constexpr std::meta::info base : direct) {
        reflect_class<base>(mrb);
        mrb_include_module(mrb, methods, reflect_module_slot<base>());
    }
    template for (constexpr std::meta::info base : std::define_static_array(reflect_bases(Type))) {
        using B = [:base:];
        if constexpr (requires(T *p) { static_cast<B *>(p); }) {
            constexpr auto to_base = [](void *const p) -> void * { return static_cast<B *>(static_cast<T *>(p)); };
            reflect_upcasts<B>().push_back({&reflect_data_type_owned<T>(), to_base});
            reflect_upcasts<B>().push_back({&reflect_data_type_borrowed<T>(), to_base});
        }
    }
    if constexpr (!std::is_abstract_v<T> && reflect_constructors<Type>().size() > 0) {
        MRB_DEFINE_ALLOCATOR(klass);
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            reflect_define_method<Type, reflect_constructors<Type>()[I]...>(mrb, klass, reflect_sym<kInitialize>(mrb));
        }(std::make_index_sequence<reflect_constructors<Type>().size()>{});
    } else if constexpr (std::is_default_constructible_v<T> && !std::is_abstract_v<T>) {
        MRB_DEFINE_ALLOCATOR(klass);
        ::mrb_define_method_id(mrb, klass, reflect_sym<kInitialize>(mrb),
                               [](mrb_state *const mrb, const mrb_value self) {
                                   T *const kept = static_cast<T *>(mrb_malloc(mrb, sizeof(T)));
                                   new (kept) T();
                                   mrb_data_init(self, kept, &reflect_data_type_owned<T>());
                                   return self;
                               }, MRB_ARGS_NONE());
    } else {
        MRB_UNDEF_ALLOCATOR(klass);
    }
    template for (constexpr std::meta::info member : reflect_members<Type>()) {
        constexpr bool first_of_its_name = [] consteval {
            for (const std::meta::info m : reflect_members<Type>()) {
                if (m == member) return true;
                if (reflect_identifier(m) == reflect_identifier(member)) return false;
            }
            return true;
        }();
        if constexpr (first_of_its_name) {
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                reflect_define_method<Type, reflect_overloads<Type, member>()[I]...>(mrb, methods, reflect_intern<member>(mrb));
            }(std::make_index_sequence<reflect_overloads<Type, member>().size()>{});
        }
    }
    template for (constexpr std::meta::info member : reflect_members<Type>()) {
        if constexpr (std::meta::is_operator_function(member) && std::meta::operator_of(member) == std::meta::operators::op_square_brackets &&
                      std::meta::parameters_of(member).size() == 1 && !std::meta::is_const(member) &&
                      std::meta::is_lvalue_reference_type(std::meta::return_type_of(member)) &&
                      !std::meta::is_const_type(std::meta::remove_reference(std::meta::return_type_of(member))))
            reflect_define_element_assignment<Type, member>(mrb, methods);
    }
    template for (constexpr std::meta::info field : reflect_fields<Type>())
        reflect_define_field<field>(mrb, methods);
    RClass *const singleton = mrb_class_ptr(mrb_singleton_class(mrb, mrb_obj_value(klass)));
    template for (constexpr std::meta::info function : reflect_static_functions<Type>()) {
        constexpr bool first_of_its_name = [] consteval {
            for (const std::meta::info m : reflect_static_functions<Type>()) {
                if (m == function) return true;
                if (reflect_identifier(m) == reflect_identifier(function)) return false;
            }
            return true;
        }();
        if constexpr (first_of_its_name) {
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                reflect_define_method<Type, reflect_overloads<Type, function>()[I]...>(mrb, singleton, reflect_intern<function>(mrb));
            }(std::make_index_sequence<reflect_overloads<Type, function>().size()>{});
        }
    }
    template for (constexpr std::meta::info member : reflect_static_data_members<Type>())
        reflect_define_static_data_member<member>(mrb, singleton);
    mrb_include_module(mrb, klass, methods);
    reflect_define_conversions<T>(mrb, klass);
    if constexpr (std::is_copy_assignable_v<T>) reflect_define_replace<T>(mrb, klass);
    return klass;
}

template <auto Classes>
void reflect_define(mrb_state *const mrb, RClass *const under = nullptr)
{
    template for (constexpr std::meta::info type : Classes)
        if (reflect_class_slot<std::meta::dealias(std::meta::remove_cvref(type))>() == nullptr) reflect_define_class<type>(mrb, under != nullptr ? under : mrb->object_class);
}

}

#endif
