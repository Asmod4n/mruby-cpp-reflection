#include <mruby.h>
#include <mruby/error.h>
#include <mruby/data.h>
#include <mruby/gc.h>
#include <mruby/variable.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <cstdint>
#include <format>
#include <string>
#include <mruby/reflection.hpp>

const struct mrb_data_type mrb_void_pointer_type = {"VoidPointer", nullptr};
const struct mrb_data_type mrb_const_void_pointer_type = {"ConstVoidPointer", nullptr};

namespace mrb_cpp_reflector {
const void *reflect_void_pointer_of(mrb_state *const mrb, const mrb_value v)
{
    if (mrb_data_check_get_ptr(mrb, v, &mrb_void_pointer_type) != nullptr) return DATA_PTR(v);
    if (mrb_data_check_get_ptr(mrb, v, &mrb_const_void_pointer_type) != nullptr) return DATA_PTR(v);
    return nullptr;
}

void reflect_define_void_pointer(mrb_state *const mrb, const char *const name)
{
    RClass *const klass = mrb_define_class(mrb, name, mrb->object_class);
    MRB_SET_INSTANCE_TT(klass, MRB_TT_CDATA);
    mrb_undef_class_method(mrb, klass, "new");
    constexpr auto address = [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        return mrb_int_value(mrb, static_cast<mrb_int>(reinterpret_cast<std::uintptr_t>(DATA_PTR(self))));
    };
    mrb_define_method(mrb, klass, "address", address, MRB_ARGS_NONE());
    mrb_define_method(mrb, klass, "to_i", address, MRB_ARGS_NONE());
    mrb_define_method(mrb, klass, "==", [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        const void *const p = reflect_void_pointer_of(mrb, mrb_get_arg1(mrb));
        return mrb_bool_value(p != nullptr && p == DATA_PTR(self));
    }, MRB_ARGS_REQ(1));
    constexpr auto inspect = [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        const std::string text = std::format("#<{} address={}>", mrb_obj_classname(mrb, self), static_cast<const void *>(DATA_PTR(self)));
        return mrb_str_new(mrb, text.data(), static_cast<mrb_int>(text.size()));
    };
    mrb_define_method(mrb, klass, "inspect", inspect, MRB_ARGS_NONE());
    mrb_define_method(mrb, klass, "to_s", inspect, MRB_ARGS_NONE());
    mrb_define_method(mrb, klass, "initialize_copy", [](mrb_state *const mrb, const mrb_value self) -> mrb_value {
        const mrb_value original = mrb_get_arg1(mrb);
        if (mrb_obj_class(mrb, original) != mrb_obj_class(mrb, self)) [[unlikely]] mrb_raise(mrb, E_TYPE_ERROR, "initialize_copy should take same class object");
        mrb_data_init(self, DATA_PTR(original), DATA_TYPE(original));
        return self;
    }, MRB_ARGS_REQ(1));
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
    const mrb_value value = mrb_obj_value(object);
    bool reflected = false;
    for (RClass *c = mrb_obj_class(mrb, value); c != nullptr && !reflected; c = c->super)
        reflected = mrb_obj_iv_defined(mrb, reinterpret_cast<RObject *>(c), mrb_cpp_reflector::reflect_reflected_key(mrb));
    if (!reflected) return MRB_EACH_OBJ_OK;
    const mrb_data_type *const type = DATA_TYPE(value);
    void *const data = DATA_PTR(value);
    mrb_data_init(value, nullptr, nullptr);
    if (type != nullptr && type->dfree != nullptr && data != nullptr) type->dfree(mrb, data);
    return MRB_EACH_OBJ_OK;
}
}

extern "C" void mrb_mruby_cpp_reflection_gem_init(mrb_state *const mrb)
{
    mrb_cpp_reflector::reflect_define_void_pointer(mrb, "VoidPointer");
    mrb_cpp_reflector::reflect_define_void_pointer(mrb, "ConstVoidPointer");
    mrb_iv_set(mrb, mrb_obj_value(mrb->object_class), mrb_cpp_reflector::reflect_identities_key(mrb),
               mrb_cptr_value(mrb, new mrb_cpp_reflector::reflect_identities()));
}

extern "C" void mrb_mruby_cpp_reflection_gem_final(mrb_state *const mrb)
{
    mrb_objspace_each_objects(mrb, mrb_cpp_reflector::reflect_free_object, nullptr);
    delete &mrb_cpp_reflector::reflect_identity_map(mrb);
    mrb_iv_remove(mrb, mrb_obj_value(mrb->object_class), mrb_cpp_reflector::reflect_identities_key(mrb));
}
#else
extern "C" void mrb_mruby_cpp_reflection_gem_init(mrb_state *const mrb)
{
    mrb_cpp_reflector::reflect_define_void_pointer(mrb, "VoidPointer");
    mrb_cpp_reflector::reflect_define_void_pointer(mrb, "ConstVoidPointer");
}

extern "C" void mrb_mruby_cpp_reflection_gem_final(mrb_state *) {}
#endif
