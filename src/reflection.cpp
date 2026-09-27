#include <mruby.h>
#include <mruby/error.h>
#include <mruby/data.h>
#include <mruby/gc.h>
#include <mruby/variable.h>
#include <mruby/class.h>
#include <mruby/string.h>
#include <mruby/presym.h>
#include <mruby/reflection.hpp>

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
        const auto child = std::ranges::find_if(at->children, [](const reflect_lifetime_base *const c) { return c != nullptr; });
        if (child != at->children.end()) {
            at = *child;
            continue;
        }
        reflect_lifetime_base *const up = at->parent;
        const mrb_value value = mrb_obj_value(at->ruby);
        const mrb_data_type *const type = DATA_TYPE(value);
        mrb_data_init(value, nullptr, nullptr);
        type->dfree(mrb, at);
        if (at == top) return MRB_EACH_OBJ_OK;
        at = up;
    }
}
}

extern "C" void mrb_mruby_cpp_reflection_gem_init(mrb_state *const mrb)
{
    mrb_cpp_reflector::reflect_define_void_pointer(mrb, MRB_SYM(VoidPointer));
    mrb_cpp_reflector::reflect_define_void_pointer(mrb, MRB_SYM(ConstVoidPointer));
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
    mrb_cpp_reflector::reflect_define_void_pointer(mrb, MRB_SYM(VoidPointer));
    mrb_cpp_reflector::reflect_define_void_pointer(mrb, MRB_SYM(ConstVoidPointer));
}

extern "C" void mrb_mruby_cpp_reflection_gem_final(mrb_state *) {}
#endif
