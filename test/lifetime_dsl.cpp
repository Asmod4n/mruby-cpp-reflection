/*
 * The lifetime declaration of a class says what its types do not say:
 * which call hands an object to another object that deletes it, which
 * call ends a lifetime, which call keeps an argument, and how a function
 * reports an error. The C++ side reads the declaration once and keeps
 * it; lifetime_dsl.rb drives the classes below from Ruby.
 */
#include <mruby.h>
#if defined(__cpp_impl_reflection)
#include <mruby/reflection.hpp>
#include <mruby/array.h>
#include <mruby/compile.h>
#include <mruby/variable.h>
#include <cerrno>
#include <functional>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

/* An object tree in the form Qt gives it (QObject::setParent):
 * set_parent makes the argument the owner of the receiver, and the
 * destructor of an owner deletes what it owns. No type says so, since
 * the link is a raw pointer. The destructor is not virtual, so C++
 * cannot tell Ruby when it deletes an object, and it does not take the
 * object out of the list of its owner, so each order in which two frees
 * can come deletes one object twice when Ruby also deletes it. */
static mrb_int &tree_objects_alive()
{
    static mrb_int n = 0;
    return n;
}
class TreeObject {
    TreeObject *parent = nullptr;
    std::vector<TreeObject *> children;
    mrb_int n = 1;

public:
    TreeObject() { ++tree_objects_alive(); }
    explicit TreeObject(TreeObject *const owner) : TreeObject() { set_parent(owner); }
    TreeObject(const TreeObject &) = delete;
    TreeObject &operator=(const TreeObject &) = delete;
    ~TreeObject()
    {
        for (TreeObject *const child : std::exchange(children, {})) {
            child->parent = nullptr;
            delete child;
        }
        --tree_objects_alive();
    }
    void set_parent(TreeObject *const owner)
    {
        if (parent != nullptr) std::erase(parent->children, this);
        parent = owner;
        if (owner != nullptr) owner->children.push_back(this);
    }
    mrb_int child_count() const { return static_cast<mrb_int>(children.size()); }
    mrb_int value() const { return n; }
    static void destroy(TreeObject *const object) { delete object; }
    static mrb_int alive() { return tree_objects_alive(); }
};
/* A function deletes a Resource, as fclose ends the lifetime of a FILE
 * (ISO C 7.21.5.1). After the call nothing may reach it. */
static mrb_int &resources_alive()
{
    static mrb_int n = 0;
    return n;
}
struct Resource {
    mrb_int n = 2;
    Resource() { ++resources_alive(); }
    Resource(const Resource &) = delete;
    Resource &operator=(const Resource &) = delete;
    ~Resource() { --resources_alive(); }
    mrb_int value() const { return n; }
    static void destroy(Resource *const resource) { delete resource; }
    static mrb_int alive() { return resources_alive(); }
};
/* A Window keeps the Layout it is given, as QWidget::setLayout does, and
 * reads it later. */
struct Layout {
    mrb_int n = 3;
};
class Window {
    const Layout *layout = nullptr;

public:
    void set_layout(const Layout *const given) { layout = given; }
    mrb_int layout_value() const { return layout == nullptr ? 0 : layout->n; }
};
/* The error conventions of C: a negative answer and errno (open in
 * POSIX), one value for success, and a null pointer. start is called
 * from another thread in a C library that takes a callback, so it takes
 * only copies. */
struct Device {
    int open(const int flags) const
    {
        if (flags < 0) {
            errno = EINVAL;
            return -1;
        }
        return 3;
    }
    int status(const int code) const { return code; }
    const Device *find(const bool found) const { return found ? this : nullptr; }
    int start(const int n) const { return n; }
};
struct Deep {
    int depth() const { return 1; }
    int shallow() const { return 2; }
};
struct Worker {
    int watch(const std::function<int(int)> &f) const { return f(1); }
};
struct Blank {
    mrb_int n = 0;
};
struct Empty {
    mrb_int n = 0;
};

constexpr auto lifetime_classes = mrb_cpp_reflector::reflect<^^TreeObject, ^^Resource, ^^Layout, ^^Window, ^^Device, ^^Deep, ^^Worker, ^^Blank, ^^Empty>();

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
 * that Ruby made, the owner deletes the child, and Ruby deletes the
 * child as well. Without a declaration the gem cannot know the link. */
static mrb_value undeclared_tree_fails_q(mrb_state *, mrb_value)
{
    return mrb_bool_value(fails_in_child([] {
        mrb_state *const other = mrb_open();
        mrb_cpp_reflector::reflect_define<lifetime_classes>(other);
        mrb_load_string(other, "def link\n"
                               "  owner = TreeObject.new\n"
                               "  child = TreeObject.new\n"
                               "  child.set_parent(owner)\n"
                               "  nil\n"
                               "end\n"
                               "link\n"
                               "GC.start\n");
        mrb_close(other);
    }));
}

/* mrb_close frees every object in the order of the heap. With owns, a
 * child is freed before its owner, and each C++ object is deleted
 * once, also along a chain of owners that is deeper than one level. */
static mrb_value tree_freed_at_close_q(mrb_state *, mrb_value)
{
    const mrb_int before = tree_objects_alive();
    mrb_state *const other = mrb_open();
    mrb_cpp_reflector::reflect_define<lifetime_classes>(other);
    mrb_load_string(other, "TreeObject.lifetime { owns :set_parent, owner: 0 }\n"
                           "$kept = []\n"
                           "30.times do\n"
                           "  owner = TreeObject.new\n"
                           "  3.times { c = TreeObject.new; c.set_parent(owner); $kept << c }\n"
                           "  $kept << owner if $kept.size % 2 == 0\n"
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

void lifetime_dsl_gem_test(mrb_state *const mrb)
{
    mrb_define_module_function(mrb, mrb->kernel_module, "retained_count", retained_count_m, MRB_ARGS_REQ(1));
    mrb_define_module_function(mrb, mrb->kernel_module, "undeclared_tree_fails?", undeclared_tree_fails_q, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "tree_freed_at_close_with_owns?", tree_freed_at_close_q, MRB_ARGS_NONE());
    mrb_cpp_reflector::reflect_define<lifetime_classes>(mrb);
}
#else
void lifetime_dsl_gem_test(mrb_state *) {}
#endif
