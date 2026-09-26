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
#include <compare>
#include <functional>
#include <concepts>
#include <cstddef>
#include <meta>
#include <new>
#include <memory>
#include <unordered_map>
#include <optional>
#include <ranges>
#include <span>
#include <filesystem>
#include <regex>
#include <system_error>
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

struct reflect_upcast {
    const mrb_data_type *base;
    void *(*to_base)(void *);
};

struct reflect_data_type : mrb_data_type {
    std::span<const reflect_upcast> upcasts;
    const mrb_data_type *owned_by_cpp;
    void *(*object_of)(void *data) = nullptr;
};

template <class T>
struct reflect_ownership_traits {
};

template <class T>
constexpr bool reflect_has_parent = requires(const T &t) { reflect_ownership_traits<T>::parent(t); };

template <class T>
constexpr bool reflect_has_guard = requires { typename reflect_ownership_traits<T>::guard; };

template <class T>
consteval std::meta::info reflect_guard_class()
{
    if (!std::meta::is_class_type(^^T)) return ^^void;
    if (reflect_has_guard<T>) return ^^T;
    for (const std::meta::info b : reflect_bases(^^T))
        if (std::meta::extract<bool>(std::meta::substitute(^^reflect_has_guard, {b}))) return b;
    return ^^void;
}

template <class T>
consteval std::meta::info reflect_ownership_class()
{
    if (!std::meta::is_class_type(^^T)) return ^^void;
    if (reflect_has_parent<T>) return ^^T;
    for (const std::meta::info b : reflect_bases(^^T))
        if (std::meta::extract<bool>(std::meta::substitute(^^reflect_has_parent, {b}))) return b;
    return ^^void;
}

using reflect_identities = std::unordered_map<const void *, RObject *>;

inline mrb_sym reflect_identities_key(mrb_state *const mrb)
{
    return mrb_intern_lit(mrb, "reflected identities");
}

inline reflect_identities &reflect_identity_map(mrb_state *const mrb)
{
    return *static_cast<reflect_identities *>(mrb_cptr(mrb_iv_get(mrb, mrb_obj_value(mrb->object_class), reflect_identities_key(mrb))));
}

inline RObject *reflect_identity(mrb_state *const mrb, const void *const object)
{
    const reflect_identities &map = reflect_identity_map(mrb);
    const auto found = map.find(object);
    return found == map.end() ? nullptr : found->second;
}

inline void reflect_identity_set(mrb_state *const mrb, const void *const object, RObject *const ruby)
{
    reflect_identity_map(mrb)[object] = ruby;
}

inline void reflect_identity_erase(mrb_state *const mrb, const void *const object)
{
    reflect_identity_map(mrb).erase(object);
}

inline mrb_sym reflect_reflected_key(mrb_state *const mrb)
{
    return mrb_intern_lit(mrb, "reflected");
}

template <class T>
const void *reflect_identity_of(const T *const object)
{
    using O = [:reflect_ownership_class<T>():];
    return static_cast<const O *>(object);
}

template <class T>
constexpr bool reflect_trackable = std::is_class_v<T> && std::has_virtual_destructor_v<T> && !std::is_final_v<T>;

inline mrb_sym reflect_lent_key(mrb_state *const mrb)
{
    return mrb_intern_lit(mrb, "reflected lent");
}

inline void reflect_forget(mrb_state *const mrb, const mrb_value object)
{
    DATA_PTR(object) = nullptr;
    const mrb_value lent = mrb_iv_get(mrb, object, reflect_lent_key(mrb));
    if (!mrb_array_p(lent)) return;
    for (mrb_int i = 0; i < RARRAY_LEN(lent); i++) reflect_forget(mrb, RARRAY_PTR(lent)[i]);
}

struct reflect_tracked_base {
    mrb_state *mrb = nullptr;
    RObject *ruby = nullptr;
};

template <class T>
struct reflect_tracked final : T, reflect_tracked_base {
    template <class... A>
    explicit reflect_tracked(A &&...args) : T(std::forward<A>(args)...)
    {
    }
    ~reflect_tracked()
    {
        if (ruby == nullptr) return;
        reflect_forget(mrb, mrb_obj_value(ruby));
        if constexpr (reflect_ownership_class<T>() != ^^void) reflect_identity_erase(mrb, reflect_identity_of(static_cast<T *>(this)));
    }
};

template <class T>
const reflect_data_type &reflect_data_type_tracked();

template <class T>
const reflect_data_type &reflect_data_type_guarded();

template <class T>
const reflect_data_type &reflect_data_type_owned();

template <class T>
const reflect_data_type &reflect_data_type_borrowed();

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
                table[at] = {&reflect_data_type_owned<B>(), [](void *const p) -> void * { return static_cast<B *>(static_cast<T *>(p)); }};
            at++;
        }
        return table;
    }();
    return upcasts;
}

template <class T>
const reflect_data_type &reflect_data_type_owned()
{
    static constexpr auto name = std::define_static_string(reflect_class_name(^^T));
    if constexpr (!std::is_destructible_v<T>) {
        static const reflect_data_type type{{name, nullptr}, reflect_upcasts<T>(), nullptr};
        return type;
    } else if constexpr (reflect_trackable<T>) {
        static const reflect_data_type type{{name, [](mrb_state *const mrb, void *const p) {
                                                 if (p == nullptr) return;
                                                 reflect_tracked<T> *const tracked = static_cast<reflect_tracked<T> *>(static_cast<T *>(p));
                                                 tracked->ruby = nullptr;
                                                 if constexpr (reflect_ownership_class<T>() != ^^void) reflect_identity_erase(mrb, reflect_identity_of(static_cast<T *>(p)));
                                                 delete tracked;
                                             }},
                                            reflect_upcasts<T>(), &reflect_data_type_tracked<T>()};
        return type;
    } else if constexpr (reflect_ownership_class<T>() != ^^void) {
        static const reflect_data_type type{{name, [](mrb_state *const mrb, void *const p) {
                                                 if (p == nullptr) return;
                                                 reflect_identity_erase(mrb, reflect_identity_of(static_cast<T *>(p)));
                                                 delete static_cast<T *>(p);
                                             }},
                                            reflect_upcasts<T>(), &reflect_data_type_borrowed<T>()};
        return type;
    } else {
        static const reflect_data_type type{{name, [](mrb_state *const mrb, void *const p) {
                                                 if (p == nullptr) return;
                                                 static_cast<T *>(p)->~T();
                                                 mrb_free(mrb, p);
                                             }},
                                            reflect_upcasts<T>(), nullptr};
        return type;
    }
}

template <class T>
const reflect_data_type &reflect_data_type_tracked()
{
    static constexpr auto name = std::define_static_string(reflect_class_name(^^T));
    static const reflect_data_type type{{name, [](mrb_state *const mrb, void *const p) {
                                             if (p == nullptr) return;
                                             static_cast<reflect_tracked<T> *>(static_cast<T *>(p))->ruby = nullptr;
                                             if constexpr (reflect_ownership_class<T>() != ^^void) reflect_identity_erase(mrb, reflect_identity_of(static_cast<T *>(p)));
                                         }},
                                        reflect_upcasts<T>(), nullptr};
    return type;
}

template <class T>
const reflect_data_type &reflect_data_type_borrowed()
{
    static constexpr auto name = std::define_static_string(reflect_class_name(^^T));
    if constexpr (reflect_ownership_class<T>() != ^^void) {
        static const reflect_data_type type{{name, [](mrb_state *const mrb, void *const p) {
                                                 if (p != nullptr) reflect_identity_erase(mrb, reflect_identity_of(static_cast<T *>(p)));
                                             }},
                                            reflect_upcasts<T>(), nullptr};
        return type;
    } else {
        static const reflect_data_type type{{name, nullptr}, reflect_upcasts<T>(), nullptr};
        return type;
    }
}

template <class T>
const reflect_data_type &reflect_data_type_guarded()
{
    using O = [:reflect_guard_class<T>():];
    using G = typename reflect_ownership_traits<O>::guard;
    static constexpr auto name = std::define_static_string(reflect_class_name(^^T));
    static const reflect_data_type type{{name, [](mrb_state *, void *const g) { delete static_cast<G *>(g); }},
                                        reflect_upcasts<T>(), nullptr, [](void *const g) -> void * {
                                            O *const watched = static_cast<G *>(g)->get();
                                            return watched == nullptr ? nullptr : static_cast<T *>(watched);
                                        }};
    return type;
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

template <std::meta::info Type>
RClass *reflect_define_class(reflect_definition &definition, RClass *super);

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
    if (!mrb_iv_defined(mrb, mrb_obj_value(mrb->object_class), key)) reflect_define_class<bare>(definition, mrb->object_class);
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
    void *const owned = mrb_data_check_get_ptr(mrb, v, &reflect_data_type_owned<T>());
    if (owned != nullptr) return static_cast<T *>(owned);
    if constexpr (reflect_trackable<T>) {
        void *const tracked = mrb_data_check_get_ptr(mrb, v, &reflect_data_type_tracked<T>());
        if (tracked != nullptr) return static_cast<T *>(tracked);
    }
    if constexpr (reflect_guard_class<T>() != ^^void) {
        void *const guard = mrb_data_check_get_ptr(mrb, v, &reflect_data_type_guarded<T>());
        if (guard != nullptr) return static_cast<T *>(reflect_data_type_guarded<T>().object_of(guard));
    }
    void *const borrowed = mrb_data_check_get_ptr(mrb, v, &reflect_data_type_borrowed<T>());
    if (borrowed != nullptr || mrb_type(v) != MRB_TT_CDATA || DATA_PTR(v) == nullptr || DATA_TYPE(v) == nullptr) return static_cast<T *>(borrowed);
    RClass *const methods = reflect_module<^^T>(mrb);
    if (methods == nullptr || !mrb_obj_is_kind_of(mrb, v, methods)) return nullptr;
    const reflect_data_type *const type = static_cast<const reflect_data_type *>(DATA_TYPE(v));
    void *const object = type->object_of != nullptr ? type->object_of(DATA_PTR(v)) : DATA_PTR(v);
    if (object == nullptr) return nullptr;
    for (const reflect_upcast &upcast : type->upcasts)
        if (upcast.base == &reflect_data_type_owned<T>()) return static_cast<T *>(upcast.to_base(object));
    return nullptr;
}

inline const mrb_data_type &reflect_data_type_share()
{
    static const mrb_data_type type{"shared", [](mrb_state *, void *const p) { delete static_cast<std::shared_ptr<void> *>(p); }};
    return type;
}

inline mrb_sym reflect_share_key(mrb_state *const mrb)
{
    return mrb_intern_lit(mrb, "reflected share");
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

template <class T>
void reflect_attach(mrb_state *const mrb, const mrb_value object)
{
    using O = [:reflect_ownership_class<T>():];
    T *const cpp = reflect_ptr<T>(mrb, object);
    if (cpp == nullptr) return;
    const O *const parent = reflect_ownership_traits<O>::parent(*cpp);
    if (parent == nullptr) return;
    RData *const data = RDATA(object);
    const mrb_data_type *const cpp_owned = static_cast<const reflect_data_type *>(data->type)->owned_by_cpp;
    if (cpp_owned != nullptr) data->type = cpp_owned;
    RObject *const holder = reflect_identity(mrb, parent);
    if (holder == nullptr) return;
    const mrb_sym key = mrb_intern_lit(mrb, "reflected children");
    mrb_value held = mrb_iv_get(mrb, mrb_obj_value(holder), key);
    if (mrb_nil_p(held)) {
        held = mrb_ary_new(mrb);
        mrb_iv_set(mrb, mrb_obj_value(holder), key, held);
    }
    for (mrb_int i = 0; i < RARRAY_LEN(held); i++)
        if (mrb_obj_eq(mrb, RARRAY_PTR(held)[i], object)) return;
    mrb_ary_push(mrb, held, object);
}

template <class T, class... A>
T *reflect_new(mrb_state *const mrb, A &&...args)
{
    if constexpr (reflect_trackable<T>) return new reflect_tracked<T>(std::forward<A>(args)...);
    else if constexpr (reflect_ownership_class<T>() != ^^void) return new T(std::forward<A>(args)...);
    else {
        T *const kept = static_cast<T *>(mrb_malloc(mrb, sizeof(T)));
        try {
            return new (kept) T(std::forward<A>(args)...);
        } catch (...) {
            mrb_free(mrb, kept);
            throw;
        }
    }
}

template <class T>
void reflect_adopt(mrb_state *const mrb, const mrb_value self, T *const made)
{
    mrb_data_init(self, made, &reflect_data_type_owned<T>());
    if constexpr (reflect_trackable<T>) {
        reflect_tracked<T> *const tracked = static_cast<reflect_tracked<T> *>(made);
        tracked->mrb = mrb;
        tracked->ruby = mrb_obj_ptr(self);
    }
    if constexpr (reflect_ownership_class<T>() != ^^void) {
        reflect_identity_set(mrb, reflect_identity_of(made), mrb_obj_ptr(self));
        reflect_attach<T>(mrb, self);
    }
}

template <class T>
mrb_value reflect_object(mrb_state *const mrb, T &&value, const bool frozen = false)
{
    using U = std::remove_cvref_t<T>;
    RData *const data = mrb_data_object_alloc(mrb, reflect_class<std::meta::dealias(std::meta::remove_cvref(^^U))>(mrb), nullptr, &reflect_data_type_owned<U>());
    reflect_adopt<U>(mrb, mrb_obj_value(data), reflect_new<U>(mrb, std::forward<T>(value)));
    if (frozen) mrb_obj_freeze(mrb, mrb_obj_value(data));
    return mrb_obj_value(data);
}

template <class T>
mrb_value reflect_borrowed(mrb_state *const mrb, T *const ref, const mrb_value owner, const bool frozen)
{
    if constexpr (reflect_ownership_class<T>() != ^^void) {
        if (RObject *const known = reflect_identity(mrb, reflect_identity_of(ref)); known != nullptr) return mrb_obj_value(known);
    }
    RData *data;
    if constexpr (reflect_guard_class<T>() != ^^void) {
        using O = [:reflect_guard_class<T>():];
        data = mrb_data_object_alloc(mrb, reflect_class<std::meta::dealias(std::meta::remove_cvref(^^T))>(mrb), new typename reflect_ownership_traits<O>::guard(static_cast<O *>(ref)),
                                     &reflect_data_type_guarded<T>());
    } else {
        data = mrb_data_object_alloc(mrb, reflect_class<std::meta::dealias(std::meta::remove_cvref(^^T))>(mrb), ref, &reflect_data_type_borrowed<T>());
    }
    const mrb_value object = mrb_obj_value(data);
    mrb_iv_set(mrb, object, reflect_sym<kOwner>(mrb), owner);
    if constexpr (reflect_ownership_class<T>() != ^^void) {
        reflect_identity_set(mrb, reflect_identity_of(ref), mrb_obj_ptr(object));
        reflect_attach<T>(mrb, object);
    }
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

template <class T>
constexpr bool reflect_from_mrb = [] {
    namespace vc = mrbcpp::value_converter;
    if constexpr (!vc::convertible_from_mrb<T>) return false;
    else if constexpr (vc::is_std_optional<T>::value || vc::is_std_vector<T>::value || vc::is_std_array<T>::value || vc::is_set_like_v<T>)
        return reflect_from_mrb<std::remove_cv_t<typename T::value_type>>;
    else if constexpr (vc::is_std_pair<T>::value)
        return reflect_from_mrb<std::remove_cv_t<typename T::first_type>> && reflect_from_mrb<std::remove_cv_t<typename T::second_type>>;
    else if constexpr (vc::is_map_like_v<T>)
        return reflect_from_mrb<std::remove_cv_t<typename T::key_type>> && reflect_from_mrb<std::remove_cv_t<typename T::mapped_type>>;
    else return true;
}();

template <class T, bool Move = false>
struct reflect_holder {
    static constexpr bool moves = Move;
    std::unique_ptr<T> temporary;
    T *ptr = nullptr;
};

struct reflect_gc_root {
    mrb_state *mrb;
    mrb_value object;
    reflect_gc_root(mrb_state *const state, const mrb_value v) : mrb(state), object(v) { mrb_gc_register(mrb, object); }
    reflect_gc_root(const reflect_gc_root &) = delete;
    reflect_gc_root &operator=(const reflect_gc_root &) = delete;
    ~reflect_gc_root() { mrb_gc_unregister(mrb, object); }
};

template <class R>
mrb_value reflect_result(mrb_state *mrb, mrb_value self, R &&value);

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
        mrb_state *const mrb = root->mrb;
        const std::array<mrb_value, sizeof...(A)> argv{reflect_result(mrb, mrb_nil_value(), std::forward<A>(args))...};
        const mrb_value answer = mrb_funcall_argv(mrb, root->object, mrb_intern_lit(mrb, "call"), static_cast<mrb_int>(argv.size()), argv.data());
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
    if (!mrb_respond_to(mrb, v, mrb_intern_lit(mrb, "call"))) return nullptr;
    using Signature = [:std::meta::template_arguments_of(std::meta::dealias(^^T))[0]:];
    return std::make_unique<T>(reflect_callable<Signature>{std::make_shared<reflect_gc_root>(mrb, v)});
}

template <class T>
std::unique_ptr<T> reflect_implicit_conversion(mrb_state *mrb, mrb_value v);

template <class T>
bool reflect_implicitly_converts(mrb_state *mrb, mrb_value v);

template <std::meta::info Function, bool Converting = true>
bool reflect_get_args_match(mrb_state *mrb, std::span<const mrb_value> argv);

template <std::meta::info ParameterType, bool Converting = true>
auto reflect_argument(mrb_state *const mrb, const mrb_value v)
{
    constexpr std::meta::info type = ParameterType;
    using T = [:reflect_bare(type):];
    if constexpr (std::meta::is_pointer_type(std::meta::dealias(type))) {
        using P = [:std::meta::dealias(std::meta::remove_cv(std::meta::remove_pointer(std::meta::dealias(type)))):];
        if constexpr (std::is_void_v<P>) return reflect_void_ptr<typename [:std::meta::remove_pointer(std::meta::dealias(type)):]>(mrb, v);
        else {
        if (mrb_nil_p(v)) return static_cast<P *>(nullptr);
        P *const p = reflect_ptr<P>(mrb, v);
        if (p == nullptr) mrb_raisef(mrb, E_TYPE_ERROR, "%s wanted", std::define_static_string(reflect_class_name(std::meta::dealias(^^P))));
        if constexpr (reflect_mutates(type)) mrb_check_frozen(mrb, mrb_obj_ptr(v));
        return p;
        }
    } else if constexpr (std::meta::is_lvalue_reference_type(type)) {
        reflect_holder<T> held;
        held.ptr = reflect_ptr<T>(mrb, v);
        if (held.ptr != nullptr) {
            if constexpr (reflect_mutates(type)) mrb_check_frozen(mrb, mrb_obj_ptr(v));
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
            if constexpr (std::same_as<std::remove_cvref_t<decltype(s)>, mrb_value> && !std::same_as<T, mrb_value>) return reflect_argument<std::meta::type_of(P)>(mrb, s);
            else if constexpr (std::same_as<T, std::string_view>) return std::string_view(s.first, static_cast<std::size_t>(s.second));
            else if constexpr (std::same_as<T, std::string>) return std::string(s.first, static_cast<std::size_t>(s.second));
            else if constexpr (std::same_as<T, std::span<const mrb_value>>) return std::span(s.first, static_cast<std::size_t>(s.second));
            else return static_cast<T>(s);
        };
        return std::tuple<decltype(converted.template operator()<I>())...>(converted.template operator()<I>()...);
    }(std::make_index_sequence<Count>{});
}

template <class P>
mrb_value reflect_lend(mrb_state *const mrb, const mrb_value holder, P *const object, const bool frozen)
{
    const mrb_value lent = reflect_borrowed<std::remove_const_t<P>>(mrb, const_cast<std::remove_const_t<P> *>(object), holder, frozen);
    mrb_value list = mrb_iv_get(mrb, holder, reflect_lent_key(mrb));
    if (!mrb_array_p(list)) {
        list = mrb_ary_new(mrb);
        mrb_iv_set(mrb, holder, reflect_lent_key(mrb), list);
    }
    mrb_ary_push(mrb, list, lent);
    return lent;
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
    if constexpr (reflect_guard_class<Q>() != ^^void) return reflect_borrowed<Q>(mrb, const_cast<Q *>(object), mrb_nil_value(), std::is_const_v<P>);
    else {
        if constexpr (std::is_polymorphic_v<Q>) {
            if (const reflect_tracked_base *const made = dynamic_cast<const reflect_tracked_base *>(object);
                made != nullptr && made->mrb == mrb && made->ruby != nullptr)
                return mrb_obj_value(made->ruby);
        }
        if constexpr (reflect_ownership_class<Q>() != ^^void) {
            if (RObject *const known = reflect_identity(mrb, reflect_identity_of(object)); known != nullptr) return mrb_obj_value(known);
        }
        if constexpr (std::is_copy_constructible_v<Q> && !std::is_abstract_v<Q>) return reflect_object(mrb, static_cast<const Q &>(*object));
        else {
            mrb_raisef(mrb, E_TYPE_ERROR, "%s is kept by C++ and cannot be kept alive from Ruby", std::define_static_string(reflect_class_name(^^Q)));
            std::unreachable();
        }
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
    else if constexpr (std::same_as<T, bool> || std::is_arithmetic_v<T>) return cpp_to_mrb_value(mrb, value);
    else if constexpr (std::same_as<T, std::strong_ordering> || std::same_as<T, std::weak_ordering> || std::same_as<T, std::partial_ordering>)
        return value < 0 ? mrb_fixnum_value(-1) : value > 0 ? mrb_fixnum_value(1) : value == 0 ? mrb_fixnum_value(0) : mrb_nil_value();
    else if constexpr (std::is_pointer_v<T>) {
        using P = std::remove_cv_t<std::remove_pointer_t<T>>;
        if constexpr (std::is_void_v<P>) {
            if (value == nullptr) return mrb_nil_value();
            constexpr bool constant = std::is_const_v<std::remove_pointer_t<T>>;
            RClass *const klass = mrb_class_get(mrb, constant ? "ConstVoidPointer" : "VoidPointer");
            return mrb_obj_value(mrb_data_object_alloc(mrb, klass, const_cast<void *>(static_cast<const void *>(value)),
                                                       constant ? &mrb_const_void_pointer_type : &mrb_void_pointer_type));
        } else if constexpr (std::same_as<P, char>) return value == nullptr ? mrb_nil_value() : mrb_str_new_cstr(mrb, value);
        else if constexpr (mrbcpp::value_converter::is_std_pair<P>::value) return value == nullptr ? mrb_nil_value() : reflect_result(mrb, self, *value);
        else return value == nullptr ? mrb_nil_value() : reflect_reference(mrb, value);
    } else if constexpr (mrbcpp::value_converter::is_std_pair<T>::value) {
        const mrb_value pair = mrb_ary_new_capa(mrb, 2);
        mrb_ary_push(mrb, pair, reflect_result(mrb, self, value.first));
        mrb_ary_push(mrb, pair, reflect_result(mrb, self, value.second));
        return pair;
    } else if constexpr (std::is_lvalue_reference_v<R>) return reflect_reference(mrb, &value);
    else if constexpr (reflect_is_view(^^T)) return reflect_view<T>(mrb, std::move(value), self, frozen);
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
        if (mrb_class_defined(mrb, "IOError")) kind = mrb_class_get(mrb, "IOError");
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

template <std::meta::info Type, std::meta::info Function, std::size_t Count = std::meta::parameters_of(Function).size()>
mrb_value reflect_call(mrb_state *const mrb, const mrb_value self)
{
    if constexpr (!std::meta::is_class_member(Function)) {
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
        using T = [:std::meta::dealias(Type):];
        if constexpr (std::meta::is_constructor(Function)) {
            auto args = reflect_get_args<Function, 0, Count>(mrb);
            return reflect_translate_exceptions(mrb, [&] {
                reflect_adopt<T>(mrb, self, std::apply([&](auto &...held) { return reflect_new<T>(mrb, reflect_pass(held)...); }, args));
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
            const mrb_value answer = reflect_translate_exceptions(mrb, [&] {
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
            if constexpr (reflect_ownership_class<T>() != ^^void) reflect_attach<T>(mrb, self);
            const mrb_value *const argv = mrb_get_argv(mrb);
            template for (constexpr std::size_t I : std::views::iota(std::size_t{0}, Count)) {
                constexpr std::meta::info bare = reflect_bare(std::meta::type_of(std::meta::parameters_of(Function)[I]));
                constexpr std::meta::info held = std::meta::is_pointer_type(bare) ? std::meta::dealias(std::meta::remove_cv(std::meta::remove_pointer(bare))) : bare;
                if constexpr (std::meta::is_class_type(held) && reflect_ownership_class<typename [:held:]>() != ^^void) {
                    if (mrb_type(argv[I]) == MRB_TT_CDATA) reflect_attach<typename [:held:]>(mrb, argv[I]);
                }
            }
            return answer;
        }
    }
}

template <std::meta::info Function, bool Converting>
bool reflect_get_args_match(mrb_state *const mrb, const std::span<const mrb_value> argv)
{
    [[maybe_unused]] constexpr auto format = reflect_get_args_format<Function>();
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
            else if constexpr (letter == 'o' && reflect_is_void_pointer(std::meta::type_of(P))) {
                fits = mrb_nil_p(v) || mrb_data_check_get_ptr(mrb, v, &mrb_void_pointer_type) != nullptr;
                if constexpr (std::meta::is_const_type(std::meta::remove_pointer(reflect_bare(std::meta::type_of(P)))))
                    fits = fits || mrb_data_check_get_ptr(mrb, v, &mrb_const_void_pointer_type) != nullptr;
            } else if constexpr (letter == 'o' && reflect_is_object(std::meta::type_of(P))) {
                using T = [:reflect_bare(std::meta::type_of(P)):];
                fits = reflect_ptr<T>(mrb, v) != nullptr || ((!reflect_mutates(std::meta::type_of(P)) && reflect_from_mrb<T>) ||
                                                        (Converting && !reflect_mutates(std::meta::type_of(P)) && reflect_implicitly_converts<T>(mrb, v)) ||
                                                        (reflect_is_function(std::meta::type_of(P)) && mrb_respond_to(mrb, v, mrb_intern_lit(mrb, "call"))) ||
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
mrb_value reflect_dispatch(mrb_state *const mrb, const mrb_value self)
{
    if constexpr (sizeof...(Overloads) == 1) return reflect_call_given<Type, Overloads...>(mrb, self);
    else {
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
    }
}

template <std::meta::info Type, std::meta::info... Overloads>
void reflect_define_method(mrb_state *const mrb, RClass *const klass, const mrb_sym name)
{
    constexpr std::meta::info first = std::array{Overloads...}[0];
    static constexpr std::array arities{(reflect_takes_block(Overloads) ? std::meta::parameters_of(Overloads).size() : std::size_t{0})...};
    if constexpr (std::ranges::any_of(arities, [](const std::size_t n) { return n > 0; })) {
        ::mrb_define_method_id(mrb, klass, name, [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
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
        constexpr std::size_t total = std::meta::parameters_of(first).size();
        constexpr mrb_aspec aspec = reflect_rest(first) ? (MRB_ARGS_REQ(total - 1) | MRB_ARGS_REST())
                                                        : (MRB_ARGS_REQ(reflect_required(first)) | MRB_ARGS_OPT(total - reflect_required(first)));
        ::mrb_define_method_id(mrb, klass, name,
                               [](mrb_state *const mrb, const mrb_value self) { return reflect_call_given<Type, first>(mrb, self); }, aspec);
    } else {
        ::mrb_define_method_id(mrb, klass, name, [](mrb_state *const mrb, const mrb_value self) { return reflect_dispatch<Type, Overloads...>(mrb, self); },
                               MRB_ARGS_ANY());
    }
}

template <std::meta::info Field>
void reflect_define_field(mrb_state *const mrb, RClass *const klass)
{
    using T = [:std::meta::parent_of(Field):];
    ::mrb_define_method_id(mrb, klass, reflect_intern<Field>(mrb), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        T *const object = reflect_ptr<T>(mrb, self);
        if (object == nullptr) mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
        using F = [:reflect_bare(std::meta::type_of(Field)):];
        if constexpr (std::is_class_v<F> && (reflect_guard_class<T>() == ^^void) && !std::same_as<F, std::string_view>)
            return reflect_lend(mrb, self, &object->[:Field:], std::meta::is_const_type(std::meta::type_of(Field)) || mrb_frozen_p(mrb_obj_ptr(self)));
        else if constexpr (std::meta::is_bit_field(Field)) return reflect_result(mrb, self, static_cast<F>(object->[:Field:]));
        else return reflect_result(mrb, self, object->[:Field:]);
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
            if constexpr (std::is_pointer_v<F> && std::is_void_v<std::remove_pointer_t<F>>) object->[:Field:] = reflect_void_ptr<std::remove_pointer_t<F>>(mrb, v);
            else if (F *const p = reflect_ptr<F>(mrb, v); p != nullptr) object->[:Field:] = *p;
            else if constexpr (reflect_from_mrb<F>) object->[:Field:] = mrb_value_to<F>(mrb, v);
            else mrb_raise(mrb, E_TYPE_ERROR, "wrong type");
            return v;
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
        ::mrb_define_method(mrb, klass, "to_i", convert, MRB_ARGS_NONE());
        if constexpr (implicit) ::mrb_define_method(mrb, klass, "to_int", convert, MRB_ARGS_NONE());
    } else if constexpr (std::is_floating_point_v<R>) {
        ::mrb_define_method(mrb, klass, "to_f", convert, MRB_ARGS_NONE());
    } else {
        ::mrb_define_method(mrb, klass, "to_s", convert, MRB_ARGS_NONE());
        if constexpr (implicit) ::mrb_define_method(mrb, klass, "to_str", convert, MRB_ARGS_NONE());
    }
}

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
            mrb_value hash;
            if constexpr (std::ranges::sized_range<T>) hash = mrb_hash_new_capa(mrb, static_cast<mrb_int>(std::ranges::size(*object)));
            else hash = mrb_hash_new(mrb);
            for (auto &&[key, mapped] : *object) {
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
            for (auto &&element : *object) mrb_yield(mrb, block, reflect_result(mrb, self, element));
            return self;
        };
        constexpr auto to_a = [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            T *const object = reflect_ptr<T>(mrb, self);
            const mrb_value array = mrb_ary_new(mrb);
            for (auto &&element : *object) mrb_ary_push(mrb, array, reflect_result(mrb, self, element));
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
        else if constexpr (reflect_from_mrb<T>) *object = mrb_value_to<T>(mrb, v);
        else mrb_raise(mrb, E_TYPE_ERROR, "wrong type");
        return self;
    }, MRB_ARGS_REQ(1));
}

template <std::meta::info Member>
void reflect_define_static_data_member(mrb_state *const mrb, RClass *const singleton)
{
    ::mrb_define_method_id(mrb, singleton, reflect_intern<Member>(mrb), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        using F = [:reflect_bare(std::meta::type_of(Member)):];
        if constexpr (std::is_class_v<F>) return reflect_borrowed<F>(mrb, const_cast<F *>(&[:Member:]), self, std::meta::is_const_type(std::meta::type_of(Member)));
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
    ::mrb_define_method_id(mrb, methods, mrb_intern_lit(mrb, "[]="), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        using T = [:std::meta::dealias(Type):];
        using E = [:reflect_bare(std::meta::return_type_of(Subscript)):];
        mrb_check_frozen(mrb, mrb_obj_ptr(self));
        T *const object = reflect_ptr<T>(mrb, self);
        if (object == nullptr) mrb_raise(mrb, E_TYPE_ERROR, "wrong receiver");
        mrb_value index, v;
        mrb_get_args(mrb, "oo", &index, &v);
        auto held = reflect_argument<std::meta::type_of(std::meta::parameters_of(Subscript)[0])>(mrb, index);
        E &element = object->[:Subscript:](reflect_pass(held));
        if constexpr (std::is_pointer_v<E> && std::is_void_v<std::remove_pointer_t<E>>) element = reflect_void_ptr<std::remove_pointer_t<E>>(mrb, v);
            else if (E *const p = reflect_ptr<E>(mrb, v); p != nullptr) element = *p;
        else if constexpr (reflect_from_mrb<E>) element = mrb_value_to<E>(mrb, v);
        else mrb_raise(mrb, E_TYPE_ERROR, "wrong type");
        return v;
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
    static constexpr std::string_view spelling = std::define_static_string(std::meta::display_string_of(t));
    mrb_obj_iv_set(mrb, reinterpret_cast<RObject *>(klass), MRB_SYM(__classname__), mrb_str_new_static(mrb, spelling.data(), spelling.size()));
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
RClass *reflect_define_class(reflect_definition &definition, RClass *const under)
{
    mrb_state *const mrb = definition.mrb;
    using T = [:std::meta::dealias(Type):];
    RClass *outer = under;
    template for (constexpr std::meta::info scope : std::define_static_array(reflect_namespaces(Type)))
        outer = ::mrb_define_module_under_id(mrb, outer, reflect_intern<scope>(mrb));
    constexpr std::meta::info enclosing = std::meta::parent_of(std::meta::dealias(Type));
    if constexpr (std::meta::is_type(enclosing) && std::meta::is_class_type(enclosing) && !reflect_reserved(enclosing)) outer = reflect_class<enclosing>(definition);
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
    if constexpr (!std::is_abstract_v<T> && reflect_constructors<Type>().size() > 0) {
        MRB_DEFINE_ALLOCATOR(klass);
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            reflect_define_method<Type, reflect_constructors<Type>()[I]...>(mrb, klass, reflect_sym<kInitialize>(mrb));
        }(std::make_index_sequence<reflect_constructors<Type>().size()>{});
    } else if constexpr (std::is_default_constructible_v<T> && !std::is_abstract_v<T>) {
        MRB_DEFINE_ALLOCATOR(klass);
        ::mrb_define_method_id(mrb, klass, reflect_sym<kInitialize>(mrb),
                               [](mrb_state *const mrb, const mrb_value self) {
                                   reflect_adopt<T>(mrb, self, reflect_new<T>(mrb));
                                   return self;
                               }, MRB_ARGS_NONE());
    } else {
        MRB_UNDEF_ALLOCATOR(klass);
    }
    if constexpr (std::is_copy_constructible_v<T> && std::is_destructible_v<T> && !std::is_abstract_v<T>) {
        ::mrb_define_method_id(mrb, klass, mrb_intern_lit(mrb, "initialize_copy"), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            mrb_value original;
            mrb_get_args(mrb, "o", &original);
            T *const source = reflect_ptr<T>(mrb, original);
            if (source == nullptr) mrb_raise(mrb, E_TYPE_ERROR, "wrong original");
            return reflect_translate_exceptions(mrb, [&] {
                reflect_adopt<T>(mrb, self, reflect_new<T>(mrb, static_cast<const T &>(*source)));
                return self;
            });
        }, MRB_ARGS_REQ(1));
    } else {
        ::mrb_define_method_id(mrb, klass, mrb_intern_lit(mrb, "initialize_copy"), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            mrb_raisef(mrb, E_TYPE_ERROR, "can't copy %s", std::define_static_string(reflect_class_name(^^T)));
            std::unreachable();
        }, MRB_ARGS_REQ(1));
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
    template for (constexpr std::meta::info function : reflect_conversion_functions<Type>())
        reflect_define_conversion_function<Type, function>(mrb, methods);
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
    if constexpr (std::ranges::any_of(reflect_members<Type>(), [](const std::meta::info m) {
                      return std::meta::is_operator_function(m) && std::meta::operator_of(m) == std::meta::operators::op_parentheses;
                  })) {
        ::mrb_define_method_id(mrb, methods, mrb_intern_lit(mrb, "to_proc"), [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
            RProc *const proc = mrb_proc_new_cfunc_with_env(mrb, [](mrb_state *const mrb, const mrb_value) -> mrb_value {
                mrb_value *argv;
                mrb_int argc;
                mrb_get_args(mrb, "*", &argv, &argc);
                return mrb_funcall_argv(mrb, mrb_cfunc_env_get(mrb, 0), mrb_intern_lit(mrb, "call"), argc, argv);
            }, 1, &self);
            proc->flags |= MRB_PROC_STRICT;
            mrb_proc_set_cfunc_aspec(proc, reflect_call_aspec<Type>());
            return mrb_obj_value(proc);
        }, MRB_ARGS_NONE());
    }
    reflect_define_conversions<T>(mrb, klass);
    if constexpr (std::is_copy_assignable_v<T>) reflect_define_replace<T>(mrb, klass);
    return klass;
}

template <std::meta::info Namespace>
RClass *reflect_define_namespace(reflect_definition &definition, RClass *const under)
{
    mrb_state *const mrb = definition.mrb;
    RClass *outer = under;
    template for (constexpr std::meta::info scope : std::define_static_array(reflect_namespaces(Namespace)))
        outer = ::mrb_define_module_under_id(mrb, outer, reflect_intern<scope>(mrb));
    RClass *const module = ::mrb_define_module_under_id(mrb, outer, reflect_intern<Namespace>(mrb));
    RClass *const singleton = mrb_class_ptr(mrb_singleton_class(mrb, mrb_obj_value(module)));
    template for (constexpr std::meta::info function : reflect_members<Namespace>()) {
        constexpr bool first_of_its_name = [] consteval {
            for (const std::meta::info m : reflect_members<Namespace>()) {
                if (m == function) return true;
                if (reflect_identifier(m) == reflect_identifier(function)) return false;
            }
            return true;
        }();
        if constexpr (first_of_its_name) {
            [&]<std::size_t... I>(std::index_sequence<I...>) {
                reflect_define_method<Namespace, reflect_overloads<Namespace, function>()[I]...>(mrb, singleton, reflect_intern<function>(mrb));
            }(std::make_index_sequence<reflect_overloads<Namespace, function>().size()>{});
        }
    }
    return module;
}

template <auto Classes>
void reflect_define(mrb_state *const mrb, RClass *const under = nullptr)
{
    reflect_definition definition(mrb);
    template for (constexpr std::meta::info type : Classes) {
        if constexpr (std::meta::is_namespace(type)) reflect_define_namespace<type>(definition, under != nullptr ? under : mrb->object_class);
        else if (!mrb_iv_defined(mrb, mrb_obj_value(mrb->object_class), reflect_class_key<std::meta::dealias(std::meta::remove_cvref(type))>(mrb)))
            reflect_define_class<type>(definition, under != nullptr ? under : mrb->object_class);
    }
    definition.finish();
}

}

#endif
