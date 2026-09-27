#include <mruby.h>
#include <mruby/error.h>
#include <mruby/data.h>
#include <mruby/gc.h>
#include <mruby/variable.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/presym.h>
#include <mruby/reflection.hpp>
#if defined(__cpp_impl_reflection) && defined(__GLIBC__)
#include <pthread.h>
#endif

const struct mrb_data_type mrb_void_pointer_type = {"VoidPointer", nullptr};
const struct mrb_data_type mrb_const_void_pointer_type = {"ConstVoidPointer", nullptr};

namespace mrb_cpp_reflector {
void reflect_define_void_pointer(mrb_state *const mrb, const mrb_sym name)
{
    RClass *const klass = mrb_define_class_id(mrb, name, mrb->object_class);
    MRB_SET_INSTANCE_TT(klass, MRB_TT_CDATA);
    mrb_undef_class_method_id(mrb, klass, MRB_SYM(new));
}
}

#if defined(__cpp_impl_reflection)
namespace mrb_cpp_reflector {
[[noreturn]] void reflect_undefined()
{
    throw reflect_undefined_call();
}

int reflect_free_object(mrb_state *const mrb, RBasic *const object, void *)
{
    if (mrb_object_dead_p(mrb, object) || object->tt != MRB_TT_CDATA || object->c == nullptr) return MRB_EACH_OBJ_OK;
    reflect_lifetime_base *const top = reflect_record(mrb, mrb_obj_value(object));
    if (top == nullptr) return MRB_EACH_OBJ_OK;
    reflect_lifetime_base *at = top;
    for (;;) {
        if (reflect_lifetime_base *const dependent = reflect_first_dependent(*at); dependent != nullptr) {
            at = dependent;
            continue;
        }
        reflect_lifetime_base *const up = at->parent != nullptr ? at->parent : at->owner;
        const mrb_value value = mrb_obj_value(at->ruby);
        const mrb_data_type *const type = DATA_TYPE(value);
        mrb_data_init(value, nullptr, nullptr);
        type->dfree(mrb, at);
        if (at == top) return MRB_EACH_OBJ_OK;
        at = up;
    }
}

struct reflect_deallocator {
    void (*release)(void *object);
    mrb_sym name;
    std::vector<mrb_sym> results_of;
};

struct reflect_declaration {
    const mrb_data_type *type;
    RClass *klass;
    RClass *methods;
    RClass *singleton;
    std::unordered_map<const void *, reflect_function_lifetime> functions;
    std::unordered_set<const mrb_data_type *> made_by_new;
    std::vector<reflect_deallocator> deallocators;
    std::optional<reflect_shared_ownership> shares;
};

const mrb_data_type reflect_declaration_type = {"lifetime declaration", nullptr};

reflect_declaration &reflect_declaration_of(mrb_state *const mrb, const mrb_value self)
{
    void *const declaration = mrb_data_check_get_ptr(mrb, self, &reflect_declaration_type);
    if (declaration == nullptr) [[unlikely]] mrb_raise(mrb, E_FROZEN_ERROR, "the lifetime declaration is closed");
    return *static_cast<reflect_declaration *>(declaration);
}

bool reflect_involves(const reflect_overload &overload, const mrb_data_type *const type)
{
    const auto is = [type](const reflect_data_type_getter getter) { return getter != nullptr && &getter() == type; };
    return is(overload.receiver) || is(overload.made) || std::ranges::any_of(overload.lent, is);
}

const std::vector<reflect_overload> &reflect_overloads_named(mrb_state *const mrb, const reflect_declaration &declaration, const mrb_sym method)
{
    const reflect_lifetimes &lifetimes = reflect_lifetimes_of(mrb);
    for (const RClass *const scope : {declaration.methods, declaration.klass, declaration.singleton})
        if (const auto found = lifetimes.overloads.find({scope, method}); found != lifetimes.overloads.end()) return found->second;
    const std::vector<reflect_overload> *involved = nullptr;
    for (const auto &[key, overloads] : lifetimes.overloads) {
        if (key.second != method || std::ranges::none_of(overloads, [&](const reflect_overload &o) { return reflect_involves(o, declaration.type); })) continue;
        if (involved != nullptr) [[unlikely]] mrb_raisef(mrb, E_NAME_ERROR, "more than one reflected function named '%n' takes or makes %C", method, declaration.klass);
        involved = &overloads;
    }
    if (involved == nullptr) [[unlikely]] mrb_raisef(mrb, E_NAME_ERROR, "%C has no reflected method '%n'", declaration.klass, method);
    return *involved;
}

void reflect_raise_unless_parameter(mrb_state *const mrb, const mrb_value given)
{
    if (!mrb_undef_p(given) && !mrb_integer_p(given) && !mrb_symbol_p(given)) [[unlikely]]
        mrb_raise(mrb, E_TYPE_ERROR, "a parameter is named by its number or its identifier");
}

std::optional<int> reflect_lent_position(mrb_state *const mrb, const reflect_overload &overload, const mrb_value given)
{
    if (mrb_undef_p(given)) return overload.receiver == nullptr ? std::nullopt : std::optional<int>(reflect_receiver);
    std::size_t at = overload.parameters.size();
    if (mrb_integer_p(given) && mrb_integer(given) >= 0) at = static_cast<std::size_t>(mrb_integer(given));
    else if (mrb_symbol_p(given))
        at = static_cast<std::size_t>(std::ranges::find(overload.parameters, std::string_view(mrb_sym_name(mrb, mrb_symbol(given))),
                                                        [](const char *const p) { return std::string_view(p); }) -
                                      overload.parameters.begin());
    if (at >= overload.parameters.size() || overload.lent[at] == nullptr) return std::nullopt;
    return static_cast<int>(at);
}

void reflect_raise_unless_declared(mrb_state *const mrb, const std::size_t declared, const mrb_sym method)
{
    if (declared == 0) [[unlikely]] mrb_raisef(mrb, E_ARGUMENT_ERROR, "no overload of %n has a pointer or reference to a class there", method);
}

const mrb_data_type *reflect_class_at(const reflect_overload &overload, const int position)
{
    return &(position == reflect_receiver ? overload.receiver : overload.lent[static_cast<std::size_t>(position)])();
}

reflect_function_lifetime &reflect_entry(mrb_state *const mrb, reflect_declaration &declaration, const reflect_overload &overload, const mrb_sym method)
{
    if (reflect_lifetimes_of(mrb).functions.contains(overload.function)) [[unlikely]]
        mrb_raisef(mrb, E_FROZEN_ERROR, "the lifetime of %n is already declared", method);
    reflect_function_lifetime &entry = declaration.functions[overload.function];
    entry.name = method;
    return entry;
}

template <class Body>
mrb_value reflect_declare(mrb_state *const mrb, const mrb_value self, const Body &body)
{
    return reflect_translate_exceptions(mrb, [&] {
        body(reflect_declaration_of(mrb, self));
        return self;
    });
}

mrb_value reflect_declare_owns(mrb_state *const mrb, const mrb_value self)
{
    mrb_sym method;
    const std::array<mrb_sym, 2> names{MRB_SYM(owner), MRB_SYM(owned)};
    std::array<mrb_value, 2> values{};
    mrb_kwargs kwargs{2, 0, names.data(), values.data(), nullptr};
    mrb_get_args(mrb, "n:", &method, &kwargs);
    if (mrb_undef_p(values[0]) && mrb_undef_p(values[1])) [[unlikely]] mrb_raise(mrb, E_ARGUMENT_ERROR, "owns names the owner: or the owned: parameter");
    reflect_raise_unless_parameter(mrb, values[0]);
    reflect_raise_unless_parameter(mrb, values[1]);
    return reflect_declare(mrb, self, [&](reflect_declaration &declaration) {
        std::size_t declared = 0;
        for (const reflect_overload &overload : reflect_overloads_named(mrb, declaration, method)) {
            const std::optional<int> owner = reflect_lent_position(mrb, overload, values[0]);
            const std::optional<int> owned = reflect_lent_position(mrb, overload, values[1]);
            if (!owner || !owned) continue;
            if (*owner == *owned) [[unlikely]] mrb_raisef(mrb, E_ARGUMENT_ERROR, "%n cannot make an object own itself", method);
            reflect_function_lifetime &entry = reflect_entry(mrb, declaration, overload, method);
            if (entry.owned != reflect_nowhere) [[unlikely]] mrb_raisef(mrb, E_ARGUMENT_ERROR, "owns is already declared for %n", method);
            entry.owner = *owner;
            entry.owned = *owned;
            declaration.made_by_new.insert(reflect_class_at(overload, *owned));
            declared++;
        }
        reflect_raise_unless_declared(mrb, declared, method);
    });
}

mrb_value reflect_declare_ends_lifetime(mrb_state *const mrb, const mrb_value self)
{
    mrb_sym method;
    mrb_value position = mrb_undef_value();
    mrb_get_args(mrb, "n|o", &method, &position);
    reflect_raise_unless_parameter(mrb, position);
    return reflect_declare(mrb, self, [&](reflect_declaration &declaration) {
        std::size_t declared = 0;
        for (const reflect_overload &overload : reflect_overloads_named(mrb, declaration, method)) {
            const std::optional<int> ended = reflect_lent_position(mrb, overload, position);
            if (!ended) continue;
            reflect_function_lifetime &entry = reflect_entry(mrb, declaration, overload, method);
            if (entry.ended != reflect_nowhere) [[unlikely]] mrb_raisef(mrb, E_ARGUMENT_ERROR, "ends_lifetime is already declared for %n", method);
            entry.ended = *ended;
            declaration.made_by_new.insert(reflect_class_at(overload, *ended));
            declared++;
        }
        reflect_raise_unless_declared(mrb, declared, method);
    });
}

mrb_value reflect_declare_retains(mrb_state *const mrb, const mrb_value self)
{
    mrb_sym method;
    mrb_value position;
    mrb_get_args(mrb, "no", &method, &position);
    reflect_raise_unless_parameter(mrb, position);
    return reflect_declare(mrb, self, [&](reflect_declaration &declaration) {
        std::size_t declared = 0;
        for (const reflect_overload &overload : reflect_overloads_named(mrb, declaration, method)) {
            const std::optional<int> retained = reflect_lent_position(mrb, overload, position);
            if (!retained) continue;
            reflect_function_lifetime &entry = reflect_entry(mrb, declaration, overload, method);
            if (std::ranges::contains(entry.retained, *retained)) [[unlikely]]
                mrb_raisef(mrb, E_ARGUMENT_ERROR, "retains is already declared for parameter %d of %n", *retained, method);
            entry.retained.push_back(*retained);
            declared++;
        }
        reflect_raise_unless_declared(mrb, declared, method);
    });
}

std::optional<mrb_int> reflect_expected_answer(mrb_state *const mrb, const mrb_value given)
{
    if (mrb_nil_p(given)) return std::nullopt;
    if (!mrb_integer_p(given)) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "an answer is an Integer or nil");
    return mrb_integer(given);
}

mrb_value reflect_declare_errors(mrb_state *const mrb, const mrb_value self)
{
    mrb_sym method;
    const std::array<mrb_sym, 3> names{MRB_SYM(success), MRB_SYM(error), MRB_SYM(sets_errno)};
    std::array<mrb_value, 3> values{};
    mrb_kwargs kwargs{3, 0, names.data(), values.data(), nullptr};
    mrb_get_args(mrb, "n:", &method, &kwargs);
    const auto [success, error, sets_errno] = values;
    if (mrb_undef_p(success) == mrb_undef_p(error)) [[unlikely]] mrb_raise(mrb, E_ARGUMENT_ERROR, "errors names either success: or error:");
    if (!mrb_undef_p(sets_errno) && !mrb_true_p(sets_errno) && !mrb_false_p(sets_errno)) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "sets_errno is true or false");
    reflect_answer_test test = reflect_answer_test::success;
    std::optional<mrb_int> expected;
    if (!mrb_undef_p(success)) expected = reflect_expected_answer(mrb, success);
    else if (mrb_symbol_p(error) && mrb_symbol(error) == MRB_SYM(negative)) test = reflect_answer_test::negative;
    else {
        test = reflect_answer_test::error;
        expected = reflect_expected_answer(mrb, error);
    }
    return reflect_declare(mrb, self, [&](reflect_declaration &declaration) {
        for (const reflect_overload &overload : reflect_overloads_named(mrb, declaration, method)) {
            if (!overload.answers_number) [[unlikely]] mrb_raisef(mrb, E_ARGUMENT_ERROR, "%n answers no integer and no pointer", method);
            reflect_function_lifetime &entry = reflect_entry(mrb, declaration, overload, method);
            if (entry.test != reflect_answer_test::none) [[unlikely]] mrb_raisef(mrb, E_ARGUMENT_ERROR, "errors is already declared for %n", method);
            entry.test = test;
            entry.expected = expected;
            entry.sets_errno = mrb_true_p(sets_errno);
        }
    });
}

mrb_value reflect_declare_stack_reserve(mrb_state *const mrb, const mrb_value self)
{
    mrb_sym method;
    mrb_int bytes;
    mrb_get_args(mrb, "ni", &method, &bytes);
#if !defined(__GLIBC__)
    mrb_raise(mrb, E_NOTIMP_ERROR, "stack_reserve reads the thread stack with pthread_getattr_np");
#endif
    if (bytes <= 0) [[unlikely]] mrb_raise(mrb, E_ARGUMENT_ERROR, "a stack reserve is a positive number of bytes");
    return reflect_declare(mrb, self, [&](reflect_declaration &declaration) {
        for (const reflect_overload &overload : reflect_overloads_named(mrb, declaration, method)) {
            reflect_function_lifetime &entry = reflect_entry(mrb, declaration, overload, method);
            if (entry.stack_reserve != 0) [[unlikely]] mrb_raisef(mrb, E_ARGUMENT_ERROR, "stack_reserve is already declared for %n", method);
            entry.stack_reserve = static_cast<std::size_t>(bytes);
        }
    });
}

mrb_value reflect_declare_threadsafe(mrb_state *const mrb, const mrb_value self)
{
    mrb_sym method;
    mrb_sym value;
    mrb_get_args(mrb, "nn", &method, &value);
    if (value != MRB_SYM(no)) [[unlikely]] mrb_raise(mrb, E_ARGUMENT_ERROR, "threadsafe takes :no");
    return reflect_declare(mrb, self, [&](reflect_declaration &declaration) {
        for (const reflect_overload &overload : reflect_overloads_named(mrb, declaration, method)) {
            if (!overload.copies) [[unlikely]] mrb_raisef(mrb, E_ARGUMENT_ERROR, "%n takes an argument that points into the VM", method);
            reflect_function_lifetime &entry = reflect_entry(mrb, declaration, overload, method);
            entry.threadsafe = false;
        }
    });
}

mrb_value reflect_declare_allocator(mrb_state *const mrb, const mrb_value self)
{
    mrb_sym method;
    const std::array<mrb_sym, 1> names{MRB_SYM(output_parameter)};
    std::array<mrb_value, 1> values{};
    mrb_kwargs kwargs{1, 0, names.data(), values.data(), nullptr};
    mrb_get_args(mrb, "n:", &method, &kwargs);
    const mrb_value output = values[0];
    reflect_raise_unless_parameter(mrb, output);
    return reflect_declare(mrb, self, [&](reflect_declaration &declaration) {
        std::size_t declared = 0;
        for (const reflect_overload &overload : reflect_overloads_named(mrb, declaration, method)) {
            if (overload.made == nullptr || &overload.made() != declaration.type) continue;
            if (mrb_undef_p(output) && overload.output != reflect_nowhere) [[unlikely]]
                mrb_raisef(mrb, E_ARGUMENT_ERROR, "%n gives the object through parameter %d, which output_parameter: names", method, overload.output);
            if (!mrb_undef_p(output)) {
                const std::size_t at = mrb_integer_p(output) && mrb_integer(output) >= 0 ? static_cast<std::size_t>(mrb_integer(output))
                                       : mrb_symbol_p(output) ? static_cast<std::size_t>(std::ranges::find(overload.parameters, std::string_view(mrb_sym_name(mrb, mrb_symbol(output))),
                                                                                                       [](const char *const p) { return std::string_view(p); }) -
                                                                                    overload.parameters.begin())
                                                              : overload.parameters.size();
                if (std::cmp_not_equal(at, overload.output)) [[unlikely]] mrb_raisef(mrb, E_ARGUMENT_ERROR, "parameter %v of %n is no pointer to a pointer to %C", output, method, declaration.klass);
            }
            reflect_function_lifetime &entry = reflect_entry(mrb, declaration, overload, method);
            if (entry.allocates) [[unlikely]] mrb_raisef(mrb, E_ARGUMENT_ERROR, "allocator is already declared for %n", method);
            entry.allocates = true;
            declared++;
        }
        if (declared == 0) [[unlikely]] mrb_raisef(mrb, E_ARGUMENT_ERROR, "no overload of %n makes %C", method, declaration.klass);
    });
}

mrb_value reflect_declare_deallocator(mrb_state *const mrb, const mrb_value self)
{
    mrb_sym method;
    const std::array<mrb_sym, 1> names{MRB_SYM(results_of)};
    std::array<mrb_value, 1> values{};
    mrb_kwargs kwargs{1, 0, names.data(), values.data(), nullptr};
    mrb_get_args(mrb, "n:", &method, &kwargs);
    std::vector<mrb_sym> results_of;
    reflect_translate_exceptions(mrb, [&] {
        if (mrb_symbol_p(values[0])) results_of.push_back(mrb_symbol(values[0]));
        else if (mrb_array_p(values[0])) {
            for (const mrb_value name : std::span<const mrb_value>(RARRAY_PTR(values[0]), static_cast<std::size_t>(RARRAY_LEN(values[0])))) {
                if (!mrb_symbol_p(name)) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "results_of names allocators by Symbol");
                results_of.push_back(mrb_symbol(name));
            }
        } else if (!mrb_undef_p(values[0])) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "results_of names allocators by Symbol");
        return mrb_nil_value();
    });
    return reflect_declare(mrb, self, [&](reflect_declaration &declaration) {
        std::size_t declared = 0;
        for (const reflect_overload &overload : reflect_overloads_named(mrb, declaration, method)) {
            if (overload.deallocator == nullptr || &overload.lent[0]() != declaration.type) continue;
            reflect_function_lifetime &entry = reflect_entry(mrb, declaration, overload, method);
            if (entry.ended != reflect_nowhere) [[unlikely]] mrb_raisef(mrb, E_ARGUMENT_ERROR, "deallocator is already declared for %n", method);
            entry.ended = 0;
            declaration.deallocators.push_back({overload.deallocator, method, results_of});
            declared++;
        }
        if (declared == 0) [[unlikely]] mrb_raisef(mrb, E_ARGUMENT_ERROR, "no overload of %n takes %C as its only parameter", method, declaration.klass);
    });
}

void (*reflect_share_function(mrb_state *const mrb, reflect_declaration &declaration, const mrb_sym method, const bool ends))(void *)
{
    void (*found)(void *) = nullptr;
    for (const reflect_overload &overload : reflect_overloads_named(mrb, declaration, method)) {
        if (overload.deallocator == nullptr || &overload.lent[0]() != declaration.type) continue;
        if (found != nullptr) [[unlikely]] mrb_raisef(mrb, E_ARGUMENT_ERROR, "more than one overload of %n takes %C as its only parameter", method, declaration.klass);
        found = overload.deallocator;
        if (ends) {
            reflect_function_lifetime &entry = reflect_entry(mrb, declaration, overload, method);
            entry.ended = 0;
        }
    }
    if (found == nullptr) [[unlikely]] mrb_raisef(mrb, E_ARGUMENT_ERROR, "no overload of %n takes %C as its only parameter", method, declaration.klass);
    return found;
}

mrb_value reflect_declare_shared_ownership(mrb_state *const mrb, const mrb_value self)
{
    const std::array<mrb_sym, 2> names{MRB_SYM(increment), MRB_SYM(decrement)};
    std::array<mrb_value, 2> values{};
    mrb_kwargs kwargs{2, 2, names.data(), values.data(), nullptr};
    mrb_get_args(mrb, ":", &kwargs);
    if (!mrb_symbol_p(values[0]) || !mrb_symbol_p(values[1])) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "increment: and decrement: name functions by Symbol");
    return reflect_declare(mrb, self, [&](reflect_declaration &declaration) {
        if (declaration.shares) [[unlikely]] mrb_raise(mrb, E_ARGUMENT_ERROR, "shared_ownership is already declared");
        void (*const increment)(void *) = reflect_share_function(mrb, declaration, mrb_symbol(values[0]), false);
        void (*const decrement)(void *) = reflect_share_function(mrb, declaration, mrb_symbol(values[1]), true);
        declaration.shares = reflect_shared_ownership{increment, decrement};
    });
}

void reflect_pair_allocators(mrb_state *const mrb, reflect_declaration &declaration)
{
    for (auto &[function, entry] : declaration.functions) {
        if (!entry.allocates) continue;
        const auto named = std::ranges::find_if(declaration.deallocators, [&](const reflect_deallocator &d) { return std::ranges::contains(d.results_of, entry.name); });
        const auto general = std::ranges::find_if(declaration.deallocators, [](const reflect_deallocator &d) { return d.results_of.empty(); });
        const auto chosen = named != declaration.deallocators.end() ? named : general;
        if (chosen == declaration.deallocators.end()) [[unlikely]] mrb_raisef(mrb, E_ARGUMENT_ERROR, "the allocator %n has no deallocator", entry.name);
        entry.deallocator = chosen->release;
    }
    for (const reflect_deallocator &d : declaration.deallocators)
        for (const mrb_sym allocator : d.results_of)
            if (std::ranges::none_of(declaration.functions, [&](const auto &f) { return f.second.allocates && f.second.name == allocator; })) [[unlikely]]
                mrb_raisef(mrb, E_ARGUMENT_ERROR, "the deallocator %n names %n, which is no allocator of this class", d.name, allocator);
}

mrb_value reflect_argument_at(mrb_state *const mrb, const mrb_value self, const int position)
{
    if (position == reflect_receiver) return self;
    if (position < 0 || position >= mrb_get_argc(mrb)) return mrb_undef_value();
    return std::span<const mrb_value>(mrb_get_argv(mrb), static_cast<std::size_t>(mrb_get_argc(mrb)))[static_cast<std::size_t>(position)];
}

bool reflect_reaches(const reflect_lifetime_base &from, const reflect_lifetime_base &target)
{
    for (const reflect_lifetime_base *at = &from; at != nullptr; at = at->parent != nullptr ? at->parent : at->owner)
        if (at == &target) return true;
    return false;
}

reflect_lifetime_base &reflect_topmost(reflect_lifetime_base &record)
{
    reflect_lifetime_base *at = &record;
    while (at->parent != nullptr || at->owner != nullptr) at = at->parent != nullptr ? at->parent : at->owner;
    return *at;
}

void reflect_raise_unless_handed_over(mrb_state *const mrb, const reflect_lifetime_base &record, const mrb_sym method)
{
    if (record.parent != nullptr) [[unlikely]] mrb_raisef(mrb, E_TYPE_ERROR, "%n cannot take a member object from the object that holds it", method);
    if (record.adopted && record.owned && !record.made_by_new) [[unlikely]]
        mrb_raisef(mrb, E_TYPE_ERROR, "%n takes an object that was made before the lifetime of its class was declared", method);
}

void reflect_hidden_iv_set(mrb_state *const mrb, RObject *const object, const mrb_sym key, const mrb_value value)
{
    const bool frozen = mrb_frozen_p(object);
    if (frozen) object->frozen = 0;
    const std::unique_ptr<RObject, decltype([](RObject *const o) { o->frozen = 1; })> refrozen(frozen ? object : nullptr);
    if (mrb_undef_p(value)) mrb_iv_remove(mrb, mrb_obj_value(object), key);
    else mrb_obj_iv_set(mrb, object, key, value);
}

void reflect_hold(mrb_state *const mrb, RObject *const keeper, const mrb_value held)
{
    mrb_value kept = mrb_obj_iv_get(mrb, keeper, MRB_SYM(__reflected_retained__));
    if (!mrb_array_p(kept)) {
        kept = mrb_ary_new(mrb);
        reflect_hidden_iv_set(mrb, keeper, MRB_SYM(__reflected_retained__), kept);
    }
    const std::span<const mrb_value> known(RARRAY_PTR(kept), static_cast<std::size_t>(RARRAY_LEN(kept)));
    if (std::ranges::any_of(known, [&](const mrb_value k) { return mrb_obj_eq(mrb, k, held); })) return;
    mrb_ary_push(mrb, kept, held);
}

void reflect_raise_on_short_stack(mrb_state *const mrb, const std::size_t reserve)
{
#if defined(__GLIBC__)
    reflect_lifetimes &lifetimes = reflect_lifetimes_of(mrb);
    if (lifetimes.stack_end == 0) {
        pthread_attr_t attributes;
        if (pthread_getattr_np(pthread_self(), &attributes) != 0) [[unlikely]] mrb_sys_fail(mrb, "pthread_getattr_np");
        void *address = nullptr;
        std::size_t size = 0;
        const int read = pthread_attr_getstack(&attributes, &address, &size);
        pthread_attr_destroy(&attributes);
        if (read != 0) [[unlikely]] mrb_sys_fail(mrb, "pthread_attr_getstack");
        lifetimes.stack_end = reinterpret_cast<std::uintptr_t>(address);
    }
    const std::uintptr_t here = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
    if (here < lifetimes.stack_end || here - lifetimes.stack_end < reserve) [[unlikely]]
        mrb_raise(mrb, mrb_exc_get_id(mrb, MRB_SYM(SystemStackError)), "the thread stack has less left than the reserve of the function");
#else
    mrb_raise(mrb, E_NOTIMP_ERROR, "stack_reserve reads the thread stack with pthread_getattr_np");
#endif
}

void reflect_before_declared_call(mrb_state *const mrb, const mrb_value self, const reflect_function_lifetime &declared)
{
    if (declared.stack_reserve > 0) reflect_raise_on_short_stack(mrb, declared.stack_reserve);
    if (declared.owned != reflect_nowhere) {
        const reflect_lifetime_base *const owned = reflect_record(mrb, reflect_argument_at(mrb, self, declared.owned));
        const bool constructed = declared.owned == reflect_receiver && mrb_type(self) == MRB_TT_CDATA && DATA_PTR(self) == nullptr;
        if (owned == nullptr && !constructed) [[unlikely]] mrb_raisef(mrb, E_TYPE_ERROR, "%n takes as owned object only an object that Ruby holds", declared.name);
        const mrb_value owner = reflect_argument_at(mrb, self, declared.owner);
        const reflect_lifetime_base *const holder = reflect_record(mrb, owner);
        if (holder == nullptr && !mrb_nil_p(owner) && !mrb_undef_p(owner)) [[unlikely]]
            mrb_raisef(mrb, E_TYPE_ERROR, "%n takes as owner only an object that Ruby holds", declared.name);
        if (owned != nullptr) {
            reflect_raise_unless_handed_over(mrb, *owned, declared.name);
            if (holder != nullptr && reflect_reaches(*holder, *owned)) [[unlikely]]
                mrb_raisef(mrb, E_ARGUMENT_ERROR, "%n would make an object own itself", declared.name);
        }
    }
    if (declared.ended != reflect_nowhere) {
        const reflect_lifetime_base *const ended = reflect_record(mrb, reflect_argument_at(mrb, self, declared.ended));
        if (ended == nullptr) [[unlikely]] mrb_raisef(mrb, E_TYPE_ERROR, "%n ends the lifetime only of an object that Ruby holds", declared.name);
        reflect_raise_unless_handed_over(mrb, *ended, declared.name);
    }
    for (const int position : declared.retained) {
        const mrb_value given = reflect_argument_at(mrb, self, position);
        if (!mrb_nil_p(given) && !mrb_undef_p(given) && reflect_record(mrb, given) == nullptr) [[unlikely]]
            mrb_raisef(mrb, E_TYPE_ERROR, "%n keeps parameter %d, so it takes only an object that Ruby holds", declared.name, position);
    }
}

bool reflect_answer_equals(const mrb_value answer, const std::optional<mrb_int> expected)
{
    if (!expected) return mrb_nil_p(answer);
    return mrb_integer_p(answer) && mrb_integer(answer) == *expected;
}

bool reflect_answer_failed(const reflect_function_lifetime &declared, const mrb_value answer)
{
    switch (declared.test) {
    case reflect_answer_test::success: return !reflect_answer_equals(answer, declared.expected);
    case reflect_answer_test::error: return reflect_answer_equals(answer, declared.expected);
    case reflect_answer_test::negative: return mrb_integer_p(answer) && mrb_integer(answer) < 0;
    case reflect_answer_test::none: return false;
    }
    return false;
}

void reflect_own(mrb_state *const mrb, reflect_lifetime_base &owner, reflect_lifetime_base &owned)
{
    if (owned.owner == &owner) return;
    owner.owned_objects.push_back(&owned);
    reflect_unlink_owner(owned);
    owned.owner = &owner;
    owned.owned = false;
    reflect_hidden_iv_set(mrb, owned.ruby, MRB_SYM(__reflected_owner__), mrb_obj_value(owner.ruby));
}

void reflect_release(mrb_state *const mrb, reflect_lifetime_base &owned)
{
    if (owned.owner == nullptr) return;
    reflect_unlink_owner(owned);
    owned.owned = owned.adopted;
    reflect_hidden_iv_set(mrb, owned.ruby, MRB_SYM(__reflected_owner__), mrb_undef_value());
}

mrb_value reflect_after_declared_call(mrb_state *const mrb, const mrb_value self, const reflect_function_lifetime &declared, const mrb_value answer, const mrb_value output,
                                      const int error_number)
{
    if (reflect_answer_failed(declared, answer)) [[unlikely]] {
        if (declared.sets_errno) {
            errno = error_number;
            mrb_sys_fail(mrb, mrb_sym_name(mrb, declared.name));
        }
        mrb_raisef(mrb, E_RUNTIME_ERROR, "%n answered %v", declared.name, answer);
    }
    return reflect_translate_exceptions(mrb, [&] {
        mrb_value result = answer;
        const mrb_value handle = mrb_undef_p(output) ? answer : output;
        if (reflect_lifetime_base *const record = reflect_record(mrb, handle); record != nullptr && !record->alive && record->object != nullptr) {
            const reflect_lifetimes &lifetimes = reflect_lifetimes_of(mrb);
            const auto shares = lifetimes.shared.find(record->type);
            if (declared.allocates || shares != lifetimes.shared.end()) {
                reflect_identity_set(*record);
                if (!declared.allocates) shares->second.increment(record->object);
                record->alive = true;
                record->owned = true;
                record->deallocator = declared.allocates ? declared.deallocator : shares->second.decrement;
            }
        }
        if (!mrb_undef_p(output)) {
            const reflect_lifetime_base *const record = reflect_record(mrb, output);
            result = record != nullptr && record->alive ? output : mrb_nil_value();
        }
        if (declared.owned != reflect_nowhere) {
            if (reflect_lifetime_base *const owned = reflect_record(mrb, reflect_argument_at(mrb, self, declared.owned)); owned != nullptr && owned->alive) {
                reflect_lifetime_base *const owner = reflect_record(mrb, reflect_argument_at(mrb, self, declared.owner));
                if (owner == nullptr) reflect_release(mrb, *owned);
                else if (!owner->alive) {
                    owned->owned = false;
                    reflect_end_lifetime(*owned);
                } else reflect_own(mrb, *owner, *owned);
            }
        }
        if (declared.ended != reflect_nowhere) {
            if (reflect_lifetime_base *const ended = reflect_record(mrb, reflect_argument_at(mrb, self, declared.ended)); ended != nullptr) {
                ended->owned = false;
                reflect_end_lifetime(*ended);
            }
        }
        if (!declared.retained.empty()) {
            reflect_lifetime_base *const receiver = reflect_record(mrb, self);
            reflect_lifetime_base *const top = receiver == nullptr ? nullptr : &reflect_topmost(*receiver);
            RObject *const keeper = top != nullptr && top->adopted && top->owned ? top->ruby : reinterpret_cast<RObject *>(mrb->object_class);
            for (const int position : declared.retained) {
                const mrb_value given = reflect_argument_at(mrb, self, position);
                if (reflect_record(mrb, given) != nullptr) reflect_hold(mrb, keeper, given);
            }
        }
        return result;
    });
}

mrb_value reflect_read_lifetime(mrb_state *const mrb, const void *const key, RClass *const klass, RClass *const methods, const mrb_value block)
{
    reflect_lifetimes &lifetimes = reflect_lifetimes_of(mrb);
    if (lifetimes.declared.contains(key)) [[unlikely]] mrb_raisef(mrb, E_FROZEN_ERROR, "the lifetime of %C is already declared", klass);
    RClass *const reader = mrb_class_ptr(mrb_iv_get(mrb, mrb_obj_value(mrb->object_class), MRB_SYM(__reflected_lifetime_reader__)));
    reflect_declaration declaration{static_cast<const mrb_data_type *>(key), klass, methods, mrb_class_ptr(mrb_singleton_class(mrb, mrb_obj_value(klass))), {}, {}, {}};
    reflect_translate_exceptions(mrb, [&] {
        lifetimes.declared.insert(key);
        return mrb_nil_value();
    });
    RData *const builder = mrb_data_object_alloc(mrb, reader, &declaration, &reflect_declaration_type);
    const std::unique_ptr<RData, decltype([](RData *const b) { b->data = nullptr; })> closed(builder);
    mrb_yield_with_class(mrb, block, 0, nullptr, mrb_obj_value(builder), reader);
    reflect_pair_allocators(mrb, declaration);
    return reflect_translate_exceptions(mrb, [&] {
        for (const auto &[function, lifetime] : declaration.functions) lifetimes.functions.insert_or_assign(function, lifetime);
        for (const mrb_data_type *const type : declaration.made_by_new) lifetimes.made_by_new.insert(type);
        if (declaration.shares) lifetimes.shared.insert_or_assign(declaration.type, *declaration.shares);
        return mrb_nil_value();
    });
}

void reflect_define_lifetime_reader(mrb_state *const mrb)
{
    RClass *const reader = mrb_class_new(mrb, mrb->object_class);
    MRB_SET_INSTANCE_TT(reader, MRB_TT_CDATA);
    MRB_UNDEF_ALLOCATOR(reader);
    mrb_undef_class_method_id(mrb, reader, MRB_SYM(new));
    mrb_define_method_id(mrb, reader, MRB_SYM(owns), reflect_declare_owns, MRB_ARGS_REQ(1) | MRB_ARGS_KEY(2, 0));
    mrb_define_method_id(mrb, reader, MRB_SYM(ends_lifetime), reflect_declare_ends_lifetime, MRB_ARGS_ARG(1, 1));
    mrb_define_method_id(mrb, reader, MRB_SYM(retains), reflect_declare_retains, MRB_ARGS_REQ(2));
    mrb_define_method_id(mrb, reader, MRB_SYM(errors), reflect_declare_errors, MRB_ARGS_REQ(1) | MRB_ARGS_KEY(3, 0));
    mrb_define_method_id(mrb, reader, MRB_SYM(stack_reserve), reflect_declare_stack_reserve, MRB_ARGS_REQ(2));
    mrb_define_method_id(mrb, reader, MRB_SYM(threadsafe), reflect_declare_threadsafe, MRB_ARGS_REQ(2));
    mrb_define_method_id(mrb, reader, MRB_SYM(allocator), reflect_declare_allocator, MRB_ARGS_REQ(1) | MRB_ARGS_KEY(1, 0));
    mrb_define_method_id(mrb, reader, MRB_SYM(deallocator), reflect_declare_deallocator, MRB_ARGS_REQ(1) | MRB_ARGS_KEY(1, 0));
    mrb_define_method_id(mrb, reader, MRB_SYM(shared_ownership), reflect_declare_shared_ownership, MRB_ARGS_KEY(2, 0));
    mrb_iv_set(mrb, mrb_obj_value(mrb->object_class), MRB_SYM(__reflected_lifetime_reader__), mrb_obj_value(reader));
}
}

extern "C" void mrb_mruby_cpp_reflection_gem_init(mrb_state *const mrb)
{
    mrb_cpp_reflector::reflect_define_void_pointer(mrb, MRB_SYM(VoidPointer));
    mrb_cpp_reflector::reflect_define_void_pointer(mrb, MRB_SYM(ConstVoidPointer));
    mrb_define_class_id(mrb, MRB_SYM(CppCoroutineError), E_STANDARD_ERROR);
    mrb_cpp_reflector::reflect_define_void_pointer(mrb, MRB_SYM(FunctionPointer));
    mrb_iv_set(mrb, mrb_obj_value(mrb->object_class), mrb_cpp_reflector::reflect_identities_key(mrb),
               mrb_cptr_value(mrb, new mrb_cpp_reflector::reflect_identities()));
    mrb_iv_set(mrb, mrb_obj_value(mrb->object_class), mrb_cpp_reflector::reflect_callbacks_key(mrb),
               mrb_cptr_value(mrb, new mrb_cpp_reflector::reflect_callbacks{std::this_thread::get_id()}));
    mrb_iv_set(mrb, mrb_obj_value(mrb->object_class), mrb_cpp_reflector::reflect_lifetimes_key(mrb),
               mrb_cptr_value(mrb, new mrb_cpp_reflector::reflect_lifetimes()));
    mrb_cpp_reflector::reflect_define_lifetime_reader(mrb);
}

extern "C" void mrb_mruby_cpp_reflection_gem_final(mrb_state *const mrb)
{
    mrb_cpp_reflector::reflect_callbacks &callbacks = mrb_cpp_reflector::reflect_callbacks_of(mrb);
    callbacks.closed = true;
    for (mrb_cpp_reflector::reflect_gc_root *const root : callbacks.roots) {
        mrb_gc_unregister(mrb, root->object);
        root->callbacks = nullptr;
    }
    callbacks.roots.clear();
    mrb_objspace_each_objects(mrb, mrb_cpp_reflector::reflect_free_object, nullptr);
    delete &mrb_cpp_reflector::reflect_identity_map(mrb);
    mrb_iv_remove(mrb, mrb_obj_value(mrb->object_class), mrb_cpp_reflector::reflect_identities_key(mrb));
    delete &callbacks;
    mrb_iv_remove(mrb, mrb_obj_value(mrb->object_class), mrb_cpp_reflector::reflect_callbacks_key(mrb));
    delete &mrb_cpp_reflector::reflect_lifetimes_of(mrb);
    mrb_iv_remove(mrb, mrb_obj_value(mrb->object_class), mrb_cpp_reflector::reflect_lifetimes_key(mrb));
}
#else
extern "C" void mrb_mruby_cpp_reflection_gem_init(mrb_state *const mrb)
{
    mrb_cpp_reflector::reflect_define_void_pointer(mrb, MRB_SYM(VoidPointer));
    mrb_cpp_reflector::reflect_define_void_pointer(mrb, MRB_SYM(ConstVoidPointer));
}

extern "C" void mrb_mruby_cpp_reflection_gem_final(mrb_state *) {}
#endif
