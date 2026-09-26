/*
 * A C++ class handed to reflect_define becomes a Ruby class under CPP::
 * with one method per public member function, one attribute per public
 * data member, argument formats derived from the signature (mruby.h, the
 * mrb_get_args table), overloads chosen by argument count and type, and
 * default arguments honoured. test.rb drives it from Ruby, so what is
 * asserted is what a Ruby caller sees. Only a compiler with C++26
 * reflection builds this; elsewhere the class is absent and test.rb
 * skips the assertions.
 */
#include <mruby.h>
#if defined(__cpp_impl_reflection)
#include <mruby/reflection.hpp>
#include <mruby/compile.h>
#include <compare>
#include <functional>
#include <pthread.h>
#include <stdexcept>
#include <new>
#include <system_error>
#include <regex>
#include <filesystem>

struct Plain {
    mrb_int n = 1;
};
struct Reflected {
    mrb_int total = 0;
    std::vector<mrb_int> seen;
    bool same(std::string_view a, mrb_int n) { total += n; return static_cast<mrb_int>(a.size()) == n; }
    mrb_int same(mrb_int n) { return n * 2; }
    mrb_value rest(mrb_value first, std::span<const mrb_value> more) { return more.empty() ? first : more.back(); }
    void add(mrb_int n) { total += n; seen.push_back(n); }
    mrb_int sum() const { return total; }
    mrb_int scaled_by(mrb_int n, mrb_int factor = 2) const { return n * factor; }
    std::string_view name() const { return "reflected"; }
    const std::vector<mrb_int> &history() const { return seen; }
    mrb_int count(const std::vector<mrb_int> &v) const { return static_cast<mrb_int>(v.size()); }
    std::string label{"l"};
    std::vector<std::string> words{"a"};
    std::vector<Plain *> pointers;
    std::vector<const Plain *> const_pointers;
    std::vector<mrb_int *> raw_longs;
    mrb_int length_of(const std::string &s) const { return static_cast<mrb_int>(s.size()); }
    std::string echo(std::string s) const { return s; }
};


/* The names follow the example of multiple inheritance in [class.mi].
 * Each constructor appends its class to constructed, so the order C++
 * builds D in can be compared from Ruby with the order mruby looks
 * methods up in. f in D hides f in A, as [class.member.lookup] says,
 * and b sits at an offset inside D, so reading it converts the pointer. */
static std::vector<std::string_view> &constructed()
{
    static std::vector<std::string_view> log;
    return log;
}
struct A {
    mrb_int a = 1;
    A() { constructed().push_back("A"); }
    mrb_int f() const { return 1; }
};
struct B {
    mrb_int b = 2;
    B() { constructed().push_back("B"); }
};
struct C {
    mrb_int c = 3;
    C() { constructed().push_back("C"); }
};
struct D : A, B, C {
    D() { constructed().push_back("D"); }
    mrb_int f() const { return 4; }
    mrb_int b_of(const B &other) const { return other.b; }
};
/* Every public constructor of S is an overload of initialize, chosen
 * like the overloads of a method: by the number of arguments, then by
 * their types, with default arguments honoured. */
struct S {
    mrb_int v = 0;
    std::string_view from = "S()";
    S() = default;
    S(mrb_int n) : v(n), from("S(mrb_int)") {}
    S(mrb_int n, mrb_int m, mrb_int k = 10) : v(n * m + k), from("S(mrb_int, mrb_int, mrb_int)") {}
    S(std::string_view text) : v(static_cast<mrb_int>(text.size())), from("S(std::string_view)") {}
};
/* Z has no default constructor, so Z.new without arguments is refused
 * as C++ refuses Z z;. Its constructor is explicit, so it never
 * converts an argument ([class.conv.ctor]). */
struct Z {
    mrb_int v;
    explicit Z(mrb_int n) : v(n) {}
};
/* X is the example of [class.conv.ctor]: both constructors convert.
 * A parameter of type X takes an Integer through X(mrb_int) and a String
 * through X(const char *, mrb_int = 0), and nothing else, because an
 * implicit conversion holds at most one user-defined conversion
 * ([over.best.ics]). */
struct X {
    mrb_int v;
    X(mrb_int n) : v(n) {}
    X(const char *text, mrb_int n = 0) : v(static_cast<mrb_int>(std::string_view(text).size()) + n) {}
};
struct Y {
    X x{mrb_int{0}};
    Y(X arg) : x(arg) {}
};
/* f and g take the argument by value and by const reference, the two
 * forms an implicit conversion reaches; h takes an explicit type. */
struct F {
    mrb_int f(X arg) const { return arg.v; }
    mrb_int g(const X &arg) const { return arg.v; }
    mrb_int h(const Z &arg) const { return arg.v; }
    mrb_int y(const Y &arg) const { return arg.x.v; }
};
/* Each member operator of Operand is the Ruby method of the same sign.
 * operator[] returns a reference that can be assigned, so there is []=
 * as well; operator() is call; <=> answers -1, 0 or 1. The converting
 * constructor lets an Integer stand on the right of +. */
struct Operand {
    mrb_int v = 0;
    Operand() = default;
    Operand(mrb_int n) : v(n) {}
    Operand operator+(const Operand &o) const { return v + o.v; }
    Operand operator-(const Operand &o) const { return v - o.v; }
    Operand operator-() const { return -v; }
    Operand operator*(const Operand &o) const { return v * o.v; }
    Operand operator<<(mrb_int n) const { return v << n; }
    bool operator==(const Operand &o) const { return v == o.v; }
    bool operator<(const Operand &o) const { return v < o.v; }
    std::strong_ordering operator<=>(const Operand &o) const { return v <=> o.v; }
    bool operator!() const { return v == 0; }
    mrb_int &operator[](mrb_int) { return v; }
    mrb_int operator()(mrb_int n) const { return v * n; }
    Operand &operator+=(const Operand &o) { v += o.v; return *this; }
};
/* A static member function is a class method, overloads and all; a
 * static data member ([class.static.data]) is a class method that reads
 * it, and one that writes it unless it is const. Both reach the one
 * object C++ has, so a write from Ruby is what C++ reads. */
struct Static {
    mrb_int v = 0;
    static mrb_int count;
    static constexpr mrb_int limit = 10;
    static mrb_int twice(mrb_int n) { return n * 2; }
    static mrb_int twice(mrb_int n, mrb_int m) { return n * m * 2; }
    static Static make(mrb_int n) { Static s; s.v = n; return s; }
    static mrb_int read_count() { return count; }
};
mrb_int Static::count = 1;
/* What a reflected function throws reaches Ruby as the exception Ruby
 * has for it, with what() as the message, in the table Rice uses
 * (rice/detail/cpp_protect.hpp). A class a build does not have gives
 * way to the class for the C++ base. A constructor that throws leaves no
 * object behind. */
struct Thrower {
    std::vector<mrb_int> seen{1, 2};
    Thrower() = default;
    explicit Thrower(mrb_int n) { if (n < 0) throw std::invalid_argument("negative"); }
    void invalid() const { throw std::invalid_argument("bad argument"); }
    mrb_int at(mrb_int i) const { return seen.at(static_cast<std::size_t>(i)); }
    void overflow() const { throw std::overflow_error("too big"); }
    void runtime() const { throw std::runtime_error("broken"); }
    void number() const { throw 42; }
    void cancel() const { pthread_cancel(pthread_self()); pthread_testcancel(); }
    void memory() const { throw std::bad_alloc(); }
    void domain() const { throw std::domain_error("domain"); }
    void length() const { throw std::length_error("length"); }
    void system() const { throw std::system_error(std::make_error_code(std::errc::no_such_file_or_directory), "open"); }
    void regex() const { throw std::regex_error(std::regex_constants::error_paren); }
    void filesystem() const { throw std::filesystem::filesystem_error("fs", std::make_error_code(std::errc::no_such_file_or_directory)); }
};
/* A std::function parameter takes anything that answers call, or the
 * block when the last parameter is one and no argument stands there. A
 * std::function or a lambda that C++ returns answers call and to_proc,
 * and a callback C++ keeps is still there after a full collection. A
 * std::function that came from Ruby goes back to Ruby as the object it
 * was made from. */
using number = mrb_int;
struct Callback {
    std::function<number(number)> kept;
    mrb_int apply(const std::function<number(number)> &f, mrb_int n) const { return f(n); }
    mrb_int each_twice(mrb_int n, std::function<number(number)> f) const { return f(f(n)); }
    void keep(std::function<number(number)> f) { kept = std::move(f); }
    mrb_int call_kept(mrb_int n) const { return kept(n); }
    const std::function<number(number)> &given() const { return kept; }
    std::function<number(number)> times(mrb_int k) const { return [k](mrb_int n) { return n * k; }; }
    auto plus(mrb_int k) const { return [k](mrb_int n) { return n + k; }; }
    bool compare(const std::function<bool(const void *, void *)> &f) const { return f(this, const_cast<Callback *>(this)); }
    std::function<void *(void *)> address() const { return [](void *p) { return p; }; }
    bool same(const void *const a, const void *b) const { return a == b; }
    const void *const fixed() const { return this; }
    void *place = nullptr;
    static inline void *anywhere = nullptr;
    const void *const where = this;
};
/* Node deletes the nodes it owns in its destructor, as a tree of
 * objects in a GUI library does. reflect_ownership_traits names the
 * parent, so the collector frees a node only while it has none, and the
 * Ruby object of a node with a parent lives as long as its parent's, with its
 * instance variables. Whatever deletes a node that Ruby made, the Ruby
 * object then raises instead of reaching freed memory. alive counts the
 * nodes C++ has. */
static mrb_int &nodes_alive()
{
    static mrb_int n = 0;
    return n;
}
struct Node {
    Node *parent = nullptr;
    Plain tag;
    std::vector<Node *> children;
    Node() { ++nodes_alive(); }
    explicit Node(Node *p) : Node() { set_parent(p); }
    Node(const Node &) = delete;
    Node &operator=(const Node &) = delete;
    virtual ~Node()
    {
        for (Node *c : children) {
            c->parent = nullptr;
            delete c;
        }
        if (parent != nullptr) std::erase(parent->children, this);
        --nodes_alive();
    }
    void set_parent(Node *p)
    {
        if (parent != nullptr) std::erase(parent->children, this);
        parent = p;
        if (p != nullptr) p->children.push_back(this);
    }
    Node *child_at(mrb_int i) const { return children.at(static_cast<std::size_t>(i)); }
    void delete_child(mrb_int i) { delete children.at(static_cast<std::size_t>(i)); }
    mrb_int child_count() const { return static_cast<mrb_int>(children.size()); }
    static mrb_int alive() { return nodes_alive(); }
};
template <>
struct mrb_cpp_reflector::reflect_ownership_traits<Node> {
    static Node *parent(const Node &n) { return n.parent; }
};
/* An object C++ hands over in a std::shared_ptr lives as long as the
 * Ruby object that holds it, even after C++ lets go of its own share;
 * handed back, it is the same std::shared_ptr with the same count. */
static mrb_int &shares_alive()
{
    static mrb_int n = 0;
    return n;
}
struct Share {
    mrb_int v;
    explicit Share(mrb_int n) : v(n) { ++shares_alive(); }
    Share(const Share &) = delete;
    Share &operator=(const Share &) = delete;
    ~Share() { --shares_alive(); }
    static mrb_int alive() { return shares_alive(); }
};
struct Sharer {
    std::shared_ptr<Share> held;
    std::shared_ptr<Share> make(mrb_int n) const { return std::make_shared<Share>(n); }
    std::shared_ptr<Share> keep(mrb_int n) { held = std::make_shared<Share>(n); return held; }
    void drop() { held.reset(); }
    mrb_int count(const std::shared_ptr<Share> &s) const { return s.use_count(); }
    bool same(const std::shared_ptr<Share> &s) const { return s == held; }
};
/* A raw pointer or a reference to an object that derives from
 * std::enable_shared_from_this and belongs to a std::shared_ptr gives
 * Ruby a share of it, so the object outlives what C++ drops. */
struct SelfShare : std::enable_shared_from_this<SelfShare> {
    mrb_int v;
    explicit SelfShare(mrb_int n) : v(n) { ++shares_alive(); }
    SelfShare(const SelfShare &) = delete;
    SelfShare &operator=(const SelfShare &) = delete;
    ~SelfShare() { --shares_alive(); }
};
struct SelfSharer {
    std::shared_ptr<SelfShare> held = std::make_shared<SelfShare>(9);
    SelfShare *raw() const { return held.get(); }
    SelfShare &ref() const { return *held; }
    void drop() { held.reset(); }
};
/* Watched is made and deleted by C++ alone. Like QPointer, a WatchGuard
 * turns to null when what it watches is deleted, and
 * reflect_ownership_traits names it as the guard, so the Ruby object of
 * a Watched raises once C++ deleted it instead of reaching freed memory. */
struct WatchGuard;
struct Watched {
    mrb_int v = 3;
    std::vector<WatchGuard *> guards;
    ~Watched();
};
struct WatchGuard {
    Watched *watched;
    explicit WatchGuard(Watched *w) : watched(w) { w->guards.push_back(this); }
    WatchGuard(const WatchGuard &) = delete;
    WatchGuard &operator=(const WatchGuard &) = delete;
    ~WatchGuard() { if (watched != nullptr) std::erase(watched->guards, this); }
    Watched *get() const { return watched; }
};
Watched::~Watched()
{
    for (WatchGuard *g : guards) g->watched = nullptr;
}
template <>
struct mrb_cpp_reflector::reflect_ownership_traits<Watched> {
    using guard = WatchGuard;
};
struct WatchedHolder {
    std::unique_ptr<Watched> held = std::make_unique<Watched>();
    Watched *get() const { return held.get(); }
    void reset() { held.reset(); }
};
/* A reference a method returns is copied when the type can be
 * copied, since nothing tells how long C++ keeps what it points to; a
 * Lonely cannot be copied, has no guard and no share, and Ruby did not
 * make it, so asking for it raises. A field is lent, and it goes with
 * the object it belongs to. */
struct Lonely {
    mrb_int v = 4;
    Lonely() = default;
    Lonely(const Lonely &) = delete;
    Lonely &operator=(const Lonely &) = delete;
};
struct Lender {
    std::vector<mrb_int> items{1, 2};
    std::unique_ptr<Lonely> lonely = std::make_unique<Lonely>();
    const std::vector<mrb_int> &view() const { return items; }
    Lonely &alone() const { return *lonely; }
    void add(mrb_int n) { items.push_back(n); }
};
/* The cases a GUI library brings, rebuilt without it. A const value
 * that a method returns is copied. A member that is deleted is left
 * out, as C++ forbids its call. A member whose type names a template
 * with an incomplete argument is left out, since its instantiation
 * would need the missing definition. A range that yields pairs of
 * references by value, as a key-value view does, still converts to a
 * Hash. A member of a template that compares elements is left out when
 * the element has no ==, since C++ only instantiates it when used. */
struct Opaque;
struct NoEquality {
    mrb_int n = 2;
};
template <class T>
struct Box {
    T item{};
    bool contains(const T &x) const { return item == x; }
    const T &get() const { return item; }
};
struct KeyValues {
    std::vector<std::string> keys{"a", "b"};
    std::vector<mrb_int> values{1, 2};
    struct iterator {
        using value_type = std::pair<const std::string &, const mrb_int &>;
        using difference_type = std::ptrdiff_t;
        const KeyValues *owner = nullptr;
        std::size_t at = 0;
        value_type operator*() const { return {owner->keys[at], owner->values[at]}; }
        iterator &operator++() { ++at; return *this; }
        iterator operator++(int) { iterator was = *this; ++at; return was; }
        bool operator==(const iterator &) const = default;
    };
    using key_type = std::string;
    using mapped_type = mrb_int;
    iterator begin() const { return {this, 0}; }
    iterator end() const { return {this, keys.size()}; }
};
struct Top {
    int n = 1;
    virtual ~Top() = default;
    int top() const { return n; }
};
struct LeftOfDiamond : virtual Top {
    int left() const { return 2; }
};
struct RightOfDiamond : virtual Top {
    int right() const { return 3; }
};
struct Diamond : LeftOfDiamond, RightOfDiamond {
    int reach(const Top &t) const { return t.top(); }
    int reach_right(const RightOfDiamond &r) const { return r.right(); }
};
struct Outer {
    struct Inner {
        int n = 3;
    };
    Inner inner() const { return {}; }
};
struct TakesRvalues {
    std::string taken;
    std::size_t take(std::string &&s)
    {
        taken = std::move(s);
        return taken.size();
    }
    int take_inner(Outer::Inner &&i)
    {
        const int n = i.n;
        i.n = 0;
        return n;
    }
    int take_number(int &&n) { return n + 1; }
};
struct Flags {
    unsigned ready : 1 = 0;
    unsigned count : 3 = 5;
    int wide = 9;
};
struct Converts {
    operator int() const { return 7; }
    operator double() const { return 2.5; }
    operator std::string() const { return "seven"; }
};
struct ConvertsExplicitly {
    explicit operator std::string() const { return "eight"; }
    explicit operator long() const { return 8; }
};
struct Odd {
    mrb_int n = 1;
    const Plain constant() const { return Plain{7}; }
    void gone() = delete;
    bool operator==(const Odd &) const = delete;
    std::vector<Opaque> *opaque() const { return nullptr; }
    KeyValues pairs() const { return {}; }
    Box<NoEquality> box() const { return {}; }
};
/* A namespace listed like a class is a module, and its free functions
 * are its module functions, overloads and default arguments as for
 * methods. */
namespace free_functions {
mrb_int twice(mrb_int n) { return n * 2; }
mrb_int twice(mrb_int n, mrb_int m) { return n * m * 2; }
mrb_int scaled(mrb_int n, mrb_int by = 3) { return n * by; }
Plain made(mrb_int n) { return Plain{n}; }
}
constexpr auto classes = mrb_cpp_reflector::reflect<^^Reflected, ^^D, ^^S, ^^Z, ^^X, ^^Y, ^^F, ^^Operand, ^^Static, ^^Thrower, ^^Callback, ^^Node, ^^Sharer, ^^SelfSharer, ^^WatchedHolder, ^^Lender, ^^free_functions, ^^Odd, ^^Converts, ^^Outer, ^^Diamond, ^^TakesRvalues, ^^Flags, ^^ConvertsExplicitly, ^^std::pair<const std::string, int>, ^^std::pair<std::string, int>>();
constexpr auto under = mrb_cpp_reflector::reflect<^^Plain>();

static_assert(std::string_view(mrb_cpp_reflector::reflect_get_args_format<std::meta::members_of(^^Reflected, std::meta::access_context::current())[2]>().data()) == "si");
static_assert(mrb_cpp_reflector::reflect_presym("same") != 0);
static_assert(mrb_cpp_reflector::reflect_presym("nowhere") == 0);

static mrb_value presym_ok_q(mrb_state *mrb, mrb_value)
{
    return mrb_bool_value(mrb_cpp_reflector::reflect_presym("same") == mrb_intern_lit(mrb, "same") &&
                          mrb_cpp_reflector::reflect_presym("Reflected") == mrb_intern_lit(mrb, "Reflected"));
}

static mrb_value constructed_m(mrb_state *mrb, mrb_value)
{
    const mrb_value names = mrb_ary_new(mrb);
    for (const std::string_view name : constructed()) mrb_ary_push(mrb, names, mrb_str_new(mrb, name.data(), static_cast<mrb_int>(name.size())));
    constructed().clear();
    return names;
}

/* Each mrb_state has its own classes: a second state in the same
 * process defines them again and answers the same, and closing it
 * leaves the first untouched. */
static mrb_value second_state_m(mrb_state *mrb, mrb_value)
{
    mrb_state *const other = mrb_open();
    mrb_cpp_reflector::reflect_define<classes>(other);
    const mrb_value answer = mrb_load_string(other, "[D.new.f, D.new.b, S.new(7).v, D.ancestors.size, Static.twice(3)]");
    const mrb_value inspected = other->exc ? mrb_obj_value(other->exc) : answer;
    const mrb_value text = mrb_inspect(other, inspected);
    const mrb_value copied = mrb_str_new(mrb, RSTRING_PTR(text), RSTRING_LEN(text));
    mrb_close(other);
    return copied;
}

/* glibc cancels a thread by unwinding it with abi::__forced_unwind, and
 * whoever catches that must throw it on. A thread with its own state
 * cancels itself inside a reflected call; it ends as cancelled only if
 * the call let the unwind through. The state is left open, since its
 * frames were unwound under it. */
static void *cancel_in_state(void *)
{
    mrb_state *const other = mrb_open();
    mrb_cpp_reflector::reflect_define<classes>(other);
    mrb_load_string(other, "Thrower.new.cancel");
    return nullptr;
}

static mrb_value cancelled_m(mrb_state *mrb, mrb_value)
{
    pthread_t thread;
    pthread_create(&thread, nullptr, cancel_in_state, nullptr);
    void *result = nullptr;
    pthread_join(thread, &result);
    return mrb_bool_value(result == PTHREAD_CANCELED);
}

static mrb_value full_gc_m(mrb_state *mrb, mrb_value)
{
    mrb_full_gc(mrb);
    return mrb_nil_value();
}

/* reflect_undefined raises in the state whose call reached it, even
 * when a callback ran code in another state on the same thread in the
 * meantime. */
static mrb_value undefined_after_other_state_m(mrb_state *mrb, mrb_value)
{
    mrb_state *const other = mrb_open();
    mrb_cpp_reflector::reflect_define<classes>(other);
    mrb_load_string(other, "Callback.new.apply(->(n) { n }, 1)");
    mrb_close(other);
    return mrb_load_string(mrb, "begin; Reflected.new.label; Std::Allocator[:char].new.allocate_at_least(1); rescue NotImplementedError; :raised_here; end");
}

extern "C" void mrb_mruby_cpp_reflection_gem_test(mrb_state *mrb)
{
    mrb_define_module_function(mrb, mrb->kernel_module, "undefined_after_other_state", undefined_after_other_state_m, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "full_gc", full_gc_m, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "cancelled_through_a_call?", cancelled_m, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "second_state", second_state_m, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "constructed", constructed_m, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "reflect_presym_ok?", presym_ok_q, MRB_ARGS_NONE());
    mrb_cpp_reflector::reflect_define<classes>(mrb);
    mrb_cpp_reflector::reflect_define<under>(mrb, mrb_define_module(mrb, "Under"));
}
#else
extern "C" void mrb_mruby_cpp_reflection_gem_test(mrb_state *) {}
#endif
