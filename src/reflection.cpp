#include <mruby.h>
#include <mruby/error.h>
#include <mruby/data.h>
#include <mruby/gc.h>
#include <mruby/variable.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/presym.h>
#include <mruby/cpp_reflection.hpp>
#if defined(__cpp_impl_reflection) && defined(__GLIBC__)
#include <pthread.h>
#endif

const struct mrb_data_type mrb_void_pointer_type = {"VoidPointer", nullptr};
const struct mrb_data_type mrb_const_void_pointer_type = {"ConstVoidPointer", nullptr};

namespace mruby::cpp_reflection {
void reflect_define_void_pointer(mrb_state *const mrb, const mrb_sym name)
{
    RClass *const klass = mrb_define_class_id(mrb, name, mrb->object_class);
    MRB_SET_INSTANCE_TT(klass, MRB_TT_CDATA);
    mrb_undef_class_method_id(mrb, klass, MRB_SYM(new));
}
}

#if defined(__cpp_impl_reflection)
namespace mruby::cpp_reflection {
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
        reflect_lifetime_base *const up = at->parent != nullptr ? at->parent : at->taken_by;
        const mrb_value value = mrb_obj_value(at->ruby);
        const mrb_data_type *const type = DATA_TYPE(value);
        mrb_data_init(value, nullptr, nullptr);
        type->dfree(mrb, at);
        if (at == top) return MRB_EACH_OBJ_OK;
        at = up;
    }
}

mrb_value reflect_argument_at(mrb_state *const mrb, const mrb_value self, const int position)
{
    if (position == reflect_receiver) return self;
    if (position < 0 || position >= mrb_get_argc(mrb)) return mrb_undef_value();
    return std::span<const mrb_value>(mrb_get_argv(mrb), static_cast<std::size_t>(mrb_get_argc(mrb)))[static_cast<std::size_t>(position)];
}

bool reflect_reaches(const reflect_lifetime_base &from, const reflect_lifetime_base &target)
{
    for (const reflect_lifetime_base *at = &from; at != nullptr; at = at->parent != nullptr ? at->parent : at->taken_by)
        if (at == &target) return true;
    return false;
}

reflect_lifetime_base &reflect_topmost(reflect_lifetime_base &record)
{
    reflect_lifetime_base *at = &record;
    while (at->parent != nullptr || at->taken_by != nullptr) at = at->parent != nullptr ? at->parent : at->taken_by;
    return *at;
}

void reflect_raise_unless_handed_over(mrb_state *const mrb, const reflect_lifetime_base &record, const char *const function)
{
    if (record.parent != nullptr) [[unlikely]] mrb_raisef(mrb, E_TYPE_ERROR, "%s cannot take a member object from the object that holds it", function);
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
    if (declared.of != reflect_nowhere) {
        const reflect_lifetime_base *const of = reflect_record(mrb, reflect_argument_at(mrb, self, declared.of));
        const bool constructed = declared.of == reflect_receiver && mrb_type(self) == MRB_TT_CDATA && DATA_PTR(self) == nullptr;
        if (of == nullptr && !constructed) [[unlikely]] mrb_raisef(mrb, E_TYPE_ERROR, "%s takes ownership only of an object that Ruby holds", declared.name);
        const mrb_value given = reflect_argument_at(mrb, self, declared.by);
        const reflect_lifetime_base *const by = reflect_record(mrb, given);
        if (by == nullptr && !mrb_nil_p(given) && !mrb_undef_p(given)) [[unlikely]]
            mrb_raisef(mrb, E_TYPE_ERROR, "%s gives ownership only to an object that Ruby holds", declared.name);
        if (of != nullptr) {
            reflect_raise_unless_handed_over(mrb, *of, declared.name);
            if (by != nullptr && reflect_reaches(*by, *of)) [[unlikely]]
                mrb_raisef(mrb, E_ARGUMENT_ERROR, "%s would make an object take ownership of itself", declared.name);
        }
    }
    if (declared.ended != reflect_nowhere) {
        const reflect_lifetime_base *const ended = reflect_record(mrb, reflect_argument_at(mrb, self, declared.ended));
        if (ended == nullptr) [[unlikely]] mrb_raisef(mrb, E_TYPE_ERROR, "%s ends the lifetime only of an object that Ruby holds", declared.name);
        reflect_raise_unless_handed_over(mrb, *ended, declared.name);
    }
    for (const int position : declared.retained) {
        const mrb_value given = reflect_argument_at(mrb, self, position);
        if (!mrb_nil_p(given) && !mrb_undef_p(given) && reflect_record(mrb, given) == nullptr) [[unlikely]]
            mrb_raisef(mrb, E_TYPE_ERROR, "%s keeps parameter %d, so it takes only an object that Ruby holds", declared.name, position);
    }
}

bool reflect_answer_equals(const mrb_value answer, const reflect_function_lifetime &declared)
{
    if (declared.expects_nil) return mrb_nil_p(answer);
    return mrb_integer_p(answer) && mrb_integer(answer) == declared.expected;
}

bool reflect_answer_failed(const reflect_function_lifetime &declared, const mrb_value answer)
{
    switch (declared.test) {
    case reflect_answer_test::success: return !reflect_answer_equals(answer, declared);
    case reflect_answer_test::error: return reflect_answer_equals(answer, declared);
    case reflect_answer_test::negative: return mrb_integer_p(answer) && mrb_integer(answer) < 0;
    case reflect_answer_test::none: return false;
    }
    return false;
}

void reflect_take_ownership(mrb_state *const mrb, reflect_lifetime_base &of, reflect_lifetime_base &by)
{
    if (of.taken_by == &by) return;
    by.taken.push_back(&of);
    reflect_unlink_taken_by(of);
    of.taken_by = &by;
    of.owned = false;
    reflect_hidden_iv_set(mrb, of.ruby, MRB_SYM(__reflected_taken_by__), mrb_obj_value(by.ruby));
}

void reflect_release(mrb_state *const mrb, reflect_lifetime_base &of)
{
    if (of.taken_by == nullptr) return;
    reflect_unlink_taken_by(of);
    of.owned = of.adopted;
    reflect_hidden_iv_set(mrb, of.ruby, MRB_SYM(__reflected_taken_by__), mrb_undef_value());
}

mrb_value reflect_after_declared_call(mrb_state *const mrb, const mrb_value self, const reflect_function_lifetime &declared, const mrb_value answer, const mrb_value output,
                                      const int error_number)
{
    if (reflect_answer_failed(declared, answer)) [[unlikely]] {
        if (declared.sets_errno) {
            errno = error_number;
            mrb_sys_fail(mrb, declared.name);
        }
        mrb_raisef(mrb, E_RUNTIME_ERROR, "%s answered %v", declared.name, answer);
    }
    return reflect_translate_exceptions(mrb, [&] {
        mrb_value result = answer;
        const mrb_value handle = mrb_undef_p(output) ? answer : output;
        if (reflect_lifetime_base *const record = reflect_record(mrb, handle); record != nullptr && !record->alive && record->object != nullptr) {
            if (declared.allocates || declared.shared) {
                reflect_identity_set(*record);
                if (!declared.allocates) declared.increment(record->object);
                record->alive = true;
                record->owned = true;
                record->deallocator = declared.allocates ? declared.deallocator : declared.decrement;
            }
        }
        if (!mrb_undef_p(output)) {
            const reflect_lifetime_base *const record = reflect_record(mrb, output);
            result = record != nullptr && record->alive ? output : mrb_nil_value();
        }
        if (declared.of != reflect_nowhere) {
            if (reflect_lifetime_base *const of = reflect_record(mrb, reflect_argument_at(mrb, self, declared.of)); of != nullptr && of->alive) {
                reflect_lifetime_base *const by = reflect_record(mrb, reflect_argument_at(mrb, self, declared.by));
                if (by == nullptr) reflect_release(mrb, *of);
                else if (!by->alive) {
                    of->owned = false;
                    reflect_end_lifetime(*of);
                } else reflect_take_ownership(mrb, *of, *by);
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

}

extern "C" void mrb_mruby_cpp_reflection_gem_init(mrb_state *const mrb)
{
    mruby::cpp_reflection::reflect_define_void_pointer(mrb, MRB_SYM(VoidPointer));
    mruby::cpp_reflection::reflect_define_void_pointer(mrb, MRB_SYM(ConstVoidPointer));
    mrb_define_class_id(mrb, MRB_SYM(CppCoroutineError), E_STANDARD_ERROR);
    mruby::cpp_reflection::reflect_define_void_pointer(mrb, MRB_SYM(FunctionPointer));
    mrb_iv_set(mrb, mrb_obj_value(mrb->object_class), mruby::cpp_reflection::reflect_identities_key(mrb),
               mrb_cptr_value(mrb, new mruby::cpp_reflection::reflect_identities()));
    mrb_iv_set(mrb, mrb_obj_value(mrb->object_class), mruby::cpp_reflection::reflect_callbacks_key(mrb),
               mrb_cptr_value(mrb, new mruby::cpp_reflection::reflect_callbacks{std::this_thread::get_id()}));
    mrb_iv_set(mrb, mrb_obj_value(mrb->object_class), mruby::cpp_reflection::reflect_lifetimes_key(mrb),
               mrb_cptr_value(mrb, new mruby::cpp_reflection::reflect_lifetimes()));
    mrb_iv_set(mrb, mrb_obj_value(mrb->object_class), mruby::cpp_reflection::reflect_symbols_key(mrb),
               mrb_cptr_value(mrb, new mruby::cpp_reflection::reflect_symbols{mruby::cpp_reflection::reflect_intern_names(mrb, mruby::cpp_reflection::reflect_gem_names), {}, {}}));
}

extern "C" void mrb_mruby_cpp_reflection_gem_final(mrb_state *const mrb)
{
    mruby::cpp_reflection::reflect_callbacks &callbacks = mruby::cpp_reflection::reflect_callbacks_of(mrb);
    callbacks.closed = true;
    for (mruby::cpp_reflection::reflect_gc_root *const root : callbacks.roots) {
        if (root->released) delete root;
        else root->callbacks = nullptr;
    }
    callbacks.roots.clear();
    mrb_objspace_each_objects(mrb, mruby::cpp_reflection::reflect_free_object, nullptr);
    delete &mruby::cpp_reflection::reflect_identity_map(mrb);
    mrb_iv_remove(mrb, mrb_obj_value(mrb->object_class), mruby::cpp_reflection::reflect_identities_key(mrb));
    delete &callbacks;
    mrb_iv_remove(mrb, mrb_obj_value(mrb->object_class), mruby::cpp_reflection::reflect_callbacks_key(mrb));
    delete &mruby::cpp_reflection::reflect_lifetimes_of(mrb);
    mrb_iv_remove(mrb, mrb_obj_value(mrb->object_class), mruby::cpp_reflection::reflect_lifetimes_key(mrb));
    delete &mruby::cpp_reflection::reflect_symbols_of(mrb);
    mrb_iv_remove(mrb, mrb_obj_value(mrb->object_class), mruby::cpp_reflection::reflect_symbols_key(mrb));
}
#else
extern "C" void mrb_mruby_cpp_reflection_gem_init(mrb_state *const mrb)
{
    mruby::cpp_reflection::reflect_define_void_pointer(mrb, MRB_SYM(VoidPointer));
    mruby::cpp_reflection::reflect_define_void_pointer(mrb, MRB_SYM(ConstVoidPointer));
}

extern "C" void mrb_mruby_cpp_reflection_gem_final(mrb_state *) {}
#endif
