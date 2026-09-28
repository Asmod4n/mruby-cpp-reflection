#pragma once
/*
 * The classes whose lifetime this header declares with
 * mruby::cpp_reflection::object_lifetime, after the classes. Each source
 * that reflects one of them includes this header, so every source sees
 * the same declaration. lifetime_dsl.rb drives them.
 */
#include <mruby.h>
#include <mruby/cpp_reflection_lifetime.hpp>
#include <cerrno>
#include <functional>
#include <utility>
#include <vector>

/* An object tree in the form Qt gives it (QObject::setParent):
 * set_parent makes the argument take ownership of the receiver, and
 * the destructor of an object deletes the objects it took. No type says so, since
 * the link is a raw pointer. The destructor is not virtual, so C++
 * cannot tell Ruby when it deletes an object, and it does not take the
 * object out of the list of its parent, so each order in which two frees
 * can come deletes one object twice when Ruby also deletes it. */
inline mrb_int &tree_objects_alive()
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
    explicit TreeObject(TreeObject *const parent) : TreeObject() { set_parent(parent); }
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
    void set_parent(TreeObject *const parent)
    {
        if (this->parent != nullptr) std::erase(this->parent->children, this);
        this->parent = parent;
        if (parent != nullptr) parent->children.push_back(this);
    }
    mrb_int child_count() const { return static_cast<mrb_int>(children.size()); }
    mrb_int value() const { return n; }
    static void destroy(TreeObject *const object) { delete object; }
    static mrb_int alive() { return tree_objects_alive(); }
};
/* A function deletes a Resource, as fclose ends the lifetime of a FILE
 * (ISO C 7.21.5.1). After the call nothing may reach it. */
inline mrb_int &resources_alive()
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
/* An Adopter takes ownership of the AdoptedLeaf it adopts and deletes it. An AdoptedLeaf
 * that Ruby made comes from new, so the delete in ~Adopter is valid. The
 * name differs from Leaf in reflection_tests.cpp, so each name has one
 * definition. */
struct AdoptedLeaf {
    mrb_int n = 5;
};
class Adopter {
    std::vector<AdoptedLeaf *> leaves;

public:
    Adopter() = default;
    Adopter(const Adopter &) = delete;
    Adopter &operator=(const Adopter &) = delete;
    ~Adopter()
    {
        for (AdoptedLeaf *const leaf : leaves) delete leaf;
    }
    void adopt(AdoptedLeaf *const leaf) { leaves.push_back(leaf); }
};

#if defined(__cpp_impl_reflection)
template <>
inline constexpr auto mruby::cpp_reflection::object_lifetime<^^TreeObject> = std::array{
    takes_ownership(^^TreeObject::set_parent, {.by = 0}),
    takes_ownership(^^TreeObject, {.by = "parent"}),
    ends_lifetime(^^TreeObject::destroy, 0),
};
template <>
inline constexpr auto mruby::cpp_reflection::object_lifetime<^^Resource> = std::array{ends_lifetime(^^Resource::destroy, 0)};
template <>
inline constexpr auto mruby::cpp_reflection::object_lifetime<^^Window> = std::array{retains(^^Window::set_layout, 0)};
template <>
inline constexpr auto mruby::cpp_reflection::object_lifetime<^^Device> = std::array{
    errors(^^Device::open, {.error = negative, .sets_errno = true}),
    errors(^^Device::status, {.success = 0}),
    errors(^^Device::find, {.error = nullptr}),
    threadsafe(^^Device::start, false),
};
/* The reserve of depth is more than any thread stack has, so every call
 * of depth raises. */
template <>
inline constexpr auto mruby::cpp_reflection::object_lifetime<^^Deep> = std::array{
    stack_reserve(^^Deep::depth, std::size_t{1} << 60),
    stack_reserve(^^Deep::shallow, 1024),
};
template <>
inline constexpr auto mruby::cpp_reflection::object_lifetime<^^Adopter> = std::array{takes_ownership(^^Adopter::adopt, {.of = 0})};
#endif
