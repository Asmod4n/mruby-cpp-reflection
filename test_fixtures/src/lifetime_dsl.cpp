/*
 * The lifetime declaration of a class says what its types do not say:
 * which call hands an object to another object that deletes it, which
 * call ends a lifetime, which call keeps an argument, and how a function
 * reports an error. lifetime_dsl.hpp declares it with
 * mruby::cpp_reflection::object_lifetime, and the C++ side reads it when it
 * compiles; Ruby has no way to declare it. lifetime_dsl.rb
 * drives the classes of lifetime_dsl.hpp from Ruby.
 */
#include <mruby.h>
#if defined(__cpp_impl_reflection)
#include "lifetime_dsl.hpp"
#include "lifetime_allocator_library.hpp"
#include <mruby/reflection.hpp>
#include <mruby/array.h>
#include <mruby/compile.h>
#include <mruby/hash.h>
#include <mruby/variable.h>
#include <sys/wait.h>
#include <unistd.h>

struct Worker {
    int watch(const std::function<int(int)> &f) const { return f(1); }
};
struct Blank {
    mrb_int n = 0;
};
struct Empty {
    mrb_int n = 0;
};
/* A Watcher keeps the callback it is given, so the callback lives in
 * C++ memory until the free function of the Watcher deletes it. */
struct Watcher {
    std::function<int(int)> kept;
    void watch(std::function<int(int)> f) { kept = std::move(f); }
};

/* The same tree as TreeObject, with no declaration. */
inline mrb_int &undeclared_trees_alive()
{
    static mrb_int n = 0;
    return n;
}
class UndeclaredTree {
    UndeclaredTree *parent = nullptr;
    std::vector<UndeclaredTree *> children;

public:
    UndeclaredTree() { ++undeclared_trees_alive(); }
    UndeclaredTree(const UndeclaredTree &) = delete;
    UndeclaredTree &operator=(const UndeclaredTree &) = delete;
    ~UndeclaredTree()
    {
        for (UndeclaredTree *const child : std::exchange(children, {})) {
            child->parent = nullptr;
            delete child;
        }
        --undeclared_trees_alive();
    }
    void set_parent(UndeclaredTree *const parent)
    {
        if (this->parent != nullptr) std::erase(this->parent->children, this);
        this->parent = parent;
        if (parent != nullptr) parent->children.push_back(this);
    }
};

constexpr auto lifetime_classes = mrb_cpp_reflector::reflect<^^TreeObject, ^^Resource, ^^Layout, ^^Window, ^^Device, ^^Deep, ^^AdoptedLeaf, ^^Adopter, ^^Worker, ^^Blank, ^^Empty,
                                                             ^^Watcher>();
constexpr auto undeclared_classes = mrb_cpp_reflector::reflect<^^UndeclaredTree>();

/* A test that expects a memory fault runs its part in a child process.
 * It answers whether the child ended by a signal or with a status other
 * than 0, which is how glibc and AddressSanitizer end a process that
 * frees memory twice. */
static bool fails_in_child(void (*const run)())
{
    const pid_t child = fork();
    if (child == 0) {
        run();
        _exit(0);
    }
    int status = 0;
    waitpid(child, &status, 0);
    return !WIFEXITED(status) || WEXITSTATUS(status) != 0;
}

/* The trace "today" of the lifetime model: set_parent links two objects
 * that Ruby made, the parent deletes the child, and Ruby deletes the
 * child as well. Without a declaration the gem cannot know the link. */
static mrb_value undeclared_tree_fails_q(mrb_state *, mrb_value)
{
    return mrb_bool_value(fails_in_child([] {
        mrb_state *const other = mrb_open();
        mrb_cpp_reflector::reflect_define<undeclared_classes>(other);
        mrb_load_string(other, "def link\n"
                               "  parent = UndeclaredTree.new\n"
                               "  child = UndeclaredTree.new\n"
                               "  child.set_parent(parent)\n"
                               "  nil\n"
                               "end\n"
                               "link\n"
                               "GC.start\n");
        mrb_close(other);
    }));
}

/* mrb_close frees every object in the order of the heap. With
 * takes_ownership, a child is freed before its parent, and each C++
 * object is deleted once, also along a chain of parents that is deeper
 * than one level. */
static mrb_value tree_freed_at_close_q(mrb_state *, mrb_value)
{
    const mrb_int before = tree_objects_alive();
    mrb_state *const other = mrb_open();
    mrb_cpp_reflector::reflect_define<lifetime_classes>(other);
    mrb_load_string(other, "$kept = []\n"
                           "30.times do\n"
                           "  parent = TreeObject.new\n"
                           "  3.times { c = TreeObject.new; c.set_parent(parent); $kept << c }\n"
                           "  $kept << parent if $kept.size % 2 == 0\n"
                           "end\n"
                           "last = TreeObject.new\n"
                           "$kept << last\n"
                           "1000.times { c = TreeObject.new; c.set_parent(last); last = c }\n");
    const bool raised = other->exc != nullptr;
    const bool grown = tree_objects_alive() == before + 30 * 4 + 1001;
    mrb_close(other);
    return mrb_bool_value(!raised && grown && tree_objects_alive() == before);
}

/* The number of objects that an object keeps for retains, read from the
 * hidden list that the gem keeps on it. */
static mrb_value retained_count_m(mrb_state *mrb, mrb_value)
{
    mrb_value object;
    mrb_get_args(mrb, "o", &object);
    const mrb_value kept = mrb_iv_get(mrb, object, mrb_intern_lit(mrb, "__reflected_retained__"));
    return mrb_int_value(mrb, mrb_array_p(kept) ? RARRAY_LEN(kept) : 0);
}

/* The number of callbacks that C++ memory holds as GC roots, released or
 * not. A free function only marks a root as released, and the next
 * callback that Ruby passes unregisters it, so that no free function
 * calls a function of mruby. */
static mrb_value callback_roots_m(mrb_state *mrb, mrb_value)
{
    return mrb_int_value(mrb, static_cast<mrb_int>(mrb_cpp_reflector::reflect_callbacks_of(mrb).roots.size()));
}

namespace declared_wrong {
using namespace mruby::cpp_reflection;
/* Each declaration below is one that the C++ side refuses when it
 * compiles the class. They are checked here with the function that the
 * compile runs, so that one build tests all of them, and each answer
 * reaches Ruby as the text that the compiler prints. no_function names
 * status, which Empty does not have. */
constexpr auto threadsafe_callback = std::array{threadsafe(^^Worker::watch, false)};
constexpr auto no_function = std::array{retains(^^Device::status, 0)};
constexpr auto owns_itself = std::array{takes_ownership(^^TreeObject::set_parent, {.of = 0, .by = 0})};
constexpr auto no_class_there = std::array{retains(^^Device::status, 0)};
constexpr auto no_class_named = std::array{ends_lifetime(^^Window::set_layout, "missing")};
constexpr auto errors_without_number = std::array{errors(^^Window::set_layout, {.success = 0})};
constexpr auto right = std::array{takes_ownership(^^TreeObject::set_parent, {.by = 0}), takes_ownership(^^TreeObject, {.by = "parent"}),
                                  ends_lifetime(^^TreeObject::destroy, 0)};
}

static mrb_value object_lifetime_declaration_errors_m(mrb_state *const mrb, mrb_value)
{
    using mrb_cpp_reflector::reflect_object_lifetime_error;
    const mrb_value errors = mrb_hash_new(mrb);
    const auto set = [&](const char *const key, const std::string_view error) {
        mrb_hash_set(mrb, errors, mrb_str_new_cstr(mrb, key), error.empty() ? mrb_nil_value() : mrb_str_new(mrb, error.data(), static_cast<mrb_int>(error.size())));
    };
    set("threadsafe_callback", reflect_object_lifetime_error(^^Worker, declared_wrong::threadsafe_callback));
    set("no_function", reflect_object_lifetime_error(^^Empty, declared_wrong::no_function));
    set("owns_itself", reflect_object_lifetime_error(^^TreeObject, declared_wrong::owns_itself));
    set("no_class_there", reflect_object_lifetime_error(^^Device, declared_wrong::no_class_there));
    set("no_class_named", reflect_object_lifetime_error(^^Window, declared_wrong::no_class_named));
    set("errors_without_number", reflect_object_lifetime_error(^^Window, declared_wrong::errors_without_number));
    set("right", reflect_object_lifetime_error(^^TreeObject, declared_wrong::right));
    return errors;
}

void lifetime_dsl_gem_init(mrb_state *const mrb)
{
    mrb_define_module_function(mrb, mrb->kernel_module, "retained_count", retained_count_m, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, mrb->kernel_module, "callback_roots", callback_roots_m, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "undeclared_tree_fails?", undeclared_tree_fails_q, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "tree_freed_at_close_with_takes_ownership?", tree_freed_at_close_q, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "object_lifetime_declaration_errors", object_lifetime_declaration_errors_m, MRB_ARGS_NONE());
    mrb_cpp_reflector::reflect_define<lifetime_classes>(mrb);
}
#else
void lifetime_dsl_gem_init(mrb_state *) {}
#endif
