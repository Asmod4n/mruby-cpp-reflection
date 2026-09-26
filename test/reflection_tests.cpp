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
#include <pthread.h>
#include <stdexcept>
#include <new>
#include <system_error>
#include <regex>
#include <filesystem>

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
    mrb_int length_of(const std::string &s) const { return static_cast<mrb_int>(s.size()); }
    std::string echo(std::string s) const { return s; }
};

struct Plain {
    mrb_int n = 1;
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
constexpr auto classes = mrb_cpp_reflector::reflect<^^Reflected, ^^D, ^^S, ^^Z, ^^X, ^^Y, ^^F, ^^Operand, ^^Static, ^^Thrower>();
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

extern "C" void mrb_mruby_cpp_reflection_gem_test(mrb_state *mrb)
{
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
