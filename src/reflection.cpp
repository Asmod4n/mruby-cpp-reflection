#include <mruby.h>
#include <mruby/error.h>
#include <mruby/data.h>
#include <mruby/gc.h>
#include <mruby/variable.h>
#if defined(__cpp_impl_reflection)
#include <mruby/reflection.hpp>
#endif

#if defined(__cpp_impl_reflection)
extern "C" [[noreturn]] void reflect_undefined()
{
    throw mrb_cpp_reflector::reflect_undefined_call();
}
#endif

#if defined(__cpp_impl_reflection)
/* mrb_close runs the gem finalizers before it frees the objects, in no
 * order. Every reflected object is freed here while the table of
 * identities still exists, and detached, so the collector later calls
 * no dfree that would reach the table. */
static int reflect_free_object(mrb_state *const mrb, RBasic *const object, void *)
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

extern "C" void mrb_mruby_cpp_reflection_gem_init(mrb_state *const mrb)
{
    mrb_iv_set(mrb, mrb_obj_value(mrb->object_class), mrb_cpp_reflector::reflect_identities_key(mrb),
               mrb_cptr_value(mrb, new mrb_cpp_reflector::reflect_identities()));
}

extern "C" void mrb_mruby_cpp_reflection_gem_final(mrb_state *const mrb)
{
    mrb_objspace_each_objects(mrb, reflect_free_object, nullptr);
    delete &mrb_cpp_reflector::reflect_identity_map(mrb);
    mrb_iv_remove(mrb, mrb_obj_value(mrb->object_class), mrb_cpp_reflector::reflect_identities_key(mrb));
}
#else
extern "C" void mrb_mruby_cpp_reflection_gem_init(mrb_state *) {}

extern "C" void mrb_mruby_cpp_reflection_gem_final(mrb_state *) {}
#endif
