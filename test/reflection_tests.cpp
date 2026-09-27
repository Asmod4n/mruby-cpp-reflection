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
#include <list>
#include <stdckdint.h>
#include <map>
#include <set>
#include <variant>
#include <generator>
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
    bool same(std::string_view a, mrb_int n) { mrb_int t; const bool overflowed = ckd_add(&t, total, n); total = checked(overflowed, t); return static_cast<mrb_int>(a.size()) == n; }
    mrb_int same(mrb_int n) { mrb_int r; const bool overflowed = ckd_mul(&r, n, mrb_int{2}); return checked(overflowed, r); }
    mrb_value rest(mrb_value first, std::span<const mrb_value> more) { return more.empty() ? first : more.back(); }
    void add(mrb_int n) { mrb_int t; const bool overflowed = ckd_add(&t, total, n); total = checked(overflowed, t); seen.push_back(n); }
    mrb_int sum() const { return total; }
    mrb_int scaled_by(mrb_int n, mrb_int factor = 2) const { mrb_int r; const bool overflowed = ckd_mul(&r, n, factor); return checked(overflowed, r); }
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
    static mrb_int checked(const bool overflowed, const mrb_int n)
    {
        if (overflowed) throw std::overflow_error("overflow");
        return n;
    }
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
    Operand operator<<(mrb_int n) const
    {
        if (n < 0 || n >= 63 || v < 0 || v > (INT64_MAX >> n)) throw std::out_of_range("shift");
        return v << n;
    }
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
    mrb_int first_after(std::string_view s, const std::function<number(number)> &f) const { f(0); return s.empty() ? 0 : s[0]; }
    mrb_int length_after(const char *s, const std::function<number(number)> &f) const { f(0); return static_cast<mrb_int>(std::string_view(s).size()); }
    mrb_value pick(std::function<number(number)> f, std::span<const mrb_value> more) const { f(0); return more.empty() ? mrb_nil_value() : more.back(); }
    bool same_text(const std::function<std::string_view()> &f) const { return f() == f(); }
    bool same_label(const std::function<const std::string &()> &f) const { return f() == f(); }
    bool same_plain(const std::function<const Plain *()> &f) const { return f() == f(); }
    bool same_void(const std::function<void *()> &f) const { return f() == f(); }
    const void *const fixed() const { return this; }
    void *place = nullptr;
    static inline void *anywhere = nullptr;
    const void *const where = this;
};
/* Node owns its children through std::unique_ptr and its tag by value,
 * so its types say who deletes what. Node has a virtual destructor, so a
 * Node that Ruby made tells its Ruby object when C++ deletes it. alive
 * counts the nodes C++ has, so a test sees a node deleted twice or never. */
static mrb_int &nodes_alive()
{
    static mrb_int n = 0;
    return n;
}
struct Node {
    Plain tag;
    std::unique_ptr<Node> first;
    std::unique_ptr<Node> second;
    Node() { ++nodes_alive(); }
    Node(const Node &) = delete;
    Node &operator=(const Node &) = delete;
    virtual ~Node() { --nodes_alive(); }
    void grow()
    {
        if (first == nullptr) first = std::make_unique<Node>();
        else if (second == nullptr) second = std::make_unique<Node>();
    }
    void cut_first() { first.reset(); }
    void swap_children() { std::swap(first, second); }
    mrb_int child_count() const { return (first != nullptr ? 1 : 0) + (second != nullptr ? 1 : 0); }
    static mrb_int alive() { return nodes_alive(); }
    std::variant<int, std::string> mark = 0;
    std::size_t which(const std::variant<int, std::string> &v) const { return v.index() + static_cast<std::size_t>(child_count()); }
    static std::size_t weigh(const Node &n, const std::variant<int, std::string> &v) { return v.index() + static_cast<std::size_t>(n.child_count()); }
};
/* Leaf is a Node without a virtual destructor. C++ cannot tell Ruby
 * when it deletes a Leaf, so only the types of its fields can. */
static mrb_int &leaves_alive()
{
    static mrb_int n = 0;
    return n;
}
struct Leaf {
    Plain tag;
    std::unique_ptr<Leaf> child;
    Leaf() { ++leaves_alive(); }
    Leaf(const Leaf &) = delete;
    Leaf &operator=(const Leaf &) = delete;
    ~Leaf() { --leaves_alive(); }
    void grow()
    {
        if (child == nullptr) child = std::make_unique<Leaf>();
    }
    void cut() { child.reset(); }
    static void cut_child_of(Leaf &leaf) { leaf.child.reset(); }
    static mrb_int alive() { return leaves_alive(); }
};
/* The elements of a container of std::unique_ptr belong to the
 * container, and no field names them. */
class Forest {
    std::vector<std::unique_ptr<Leaf>> leaves;
    std::vector<std::unique_ptr<Plain>> plains;

public:
    Forest() = default;
    Forest(const Forest &) = delete;
    Forest &operator=(const Forest &) = delete;
    void plant()
    {
        leaves.push_back(std::make_unique<Leaf>());
        plains.push_back(std::make_unique<Plain>(Plain{static_cast<mrb_int>(plains.size()) + 1}));
    }
    Leaf *leaf(mrb_int i) const { return leaves.at(static_cast<std::size_t>(i)).get(); }
    Plain *plain(mrb_int i) const { return plains.at(static_cast<std::size_t>(i)).get(); }
    void clear()
    {
        leaves.clear();
        plains.clear();
    }
};
/* Hand deletes what it holds in its destructor, and no type says so. */
class Hand {
    Leaf *held = new Leaf();

public:
    Hand() = default;
    Hand(const Hand &) = delete;
    Hand &operator=(const Hand &) = delete;
    ~Hand() { delete held; }
    Leaf &item() const { return *held; }
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
/* WatchedHolder owns a Watched through std::unique_ptr, and reset
 * deletes it. */
struct Watched {
    mrb_int v = 3;
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

struct Link {
    int v = 0;
    Link *next = this;
    void attach(Link &other) { next = &other; }
    Link &follow() const { return *next; }
    Link &itself() { return *this; }
};
struct Scored {
    int v = 0;
    int operator<=>(const Scored &o) const { return (v - o.v) * 5; }
    int operator==(const Scored &o) const { return v == o.v ? 7 : 0; }
    int operator<(const Scored &o) const { return v < o.v ? 3 : 0; }
};
struct Measure {
    double v = 0;
    auto operator<=>(const Measure &) const = default;
};
inline int shelf_numbers[2] = {3, 4};
struct Shelf {
    std::vector<int> full{1, 2};
    std::vector<int> empty;
    std::map<int, int> table{{1, 10}, {2, 20}};
    std::list<int> chain{1, 2};
    std::vector<int> &items() { return full; }
    std::span<int> window() { return full; }
    auto reversed() { return std::views::reverse(full); }
    std::span<int> part = shelf_numbers;
    int total(std::span<const int> numbers) const { return static_cast<int>(numbers.size()); }
    bool is_ready() const { return true; }
    int fits(int n) const { return n; }
    int pick(int) const { return 1; }
    int pick(double) const { return 2; }
    int pick_back(double) const { return 2; }
    int pick_back(int) const { return 1; }
    float fits_float(float f) const { return f; }
    std::size_t count_shorts(const std::vector<short> &v) const { return v.size(); }
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
struct Declared {
    int defined() const { return 1; }
    int undefined() const;
};
struct Unbuilt {
    Unbuilt() = default;
    explicit Unbuilt(int n);
    int n = 0;
};
template <class T>
struct Held {
    T value{};
    T get() const;
};
template <class T>
T Held<T>::get() const
{
    return value;
}
extern template struct Held<long>;
using HeldLong = Held<long>;
enum class Color { red, green = 5, dark_blue };
enum Flag : unsigned { none = 0, read = 1, write = 2 };
struct Palette {
    Color color = Color::green;
    Color pick(int n) const { return static_cast<Color>(n); }
    int value_of(Color c) const { return static_cast<int>(c); }
    Flag both() const { return static_cast<Flag>(read | write); }
    Flag first() const { return read; }
    enum class Mode { on, off };
    Mode mode() const { return Mode::off; }
};
struct Holder {
    struct Unused {
        int k = 4;
        struct Deeper {
            int d = 5;
        };
    };
    enum class Level { low, high };
    int n = 1;
};
struct Keeper {
    struct Unused {
        int k = 4;
    };
    int n = 1;
};
namespace fruit {
struct Apple {
    int n = 1;
};
struct Pear {
    int n = 2;
};
enum class Kind { sweet };
template <class T>
    requires requires(const T &t) { t.n; }
int weight(const T &t)
{
    return t.n;
}
inline int weight(int grams) { return grams; }
struct Scale {
    template <class T>
        requires requires(const T &t) { t.n; }
    int measure(const T &t) const
    {
        return t.n * 10;
    }
    int measure() const { return 0; }
};
struct Basket {
    template <class T>
    int count(const T &t) const
    {
        return t.n;
    }
};
}
namespace ops {
struct Vec {
    int x = 0;
};
inline bool operator==(const Vec &a, const Vec &b) { return a.x == b.x; }
inline Vec operator+(const Vec &a, const Vec &b) { return {a.x + b.x}; }
inline Vec operator-(const Vec &a) { return {-a.x}; }
inline Vec operator*(Vec &&a, int n) { return {a.x * n}; }
struct Log {
    std::string text;
    Log &operator<<(int n)
    {
        text += std::to_string(n);
        return *this;
    }
};
inline Log &operator<<(Log &log, const Vec &v)
{
    log.text += "v" + std::to_string(v.x);
    return log;
}
}
namespace globals {
struct Counter {
    int n = 1;
};
inline int counter = 3;
inline const int limit = 10;
inline Counter shared;
inline const Counter fixed{};
inline int read_counter() { return counter; }
inline int read_shared() { return shared.n; }
struct Registry {
    static inline Counter first{};
};
}
namespace only_one {
inline int picked = 5;
inline int skipped = 6;
inline int skipped_function() { return 0; }
}
namespace shapes {
struct Point {
    int x = 1;
};
struct Shape {
    explicit Shape(int scale) : scale(scale) {}
    virtual ~Shape() = default;
    virtual int area(int k) const { return scale * k; }
    virtual std::string name() const { return "shape"; }
    virtual int sides() const = 0;
    virtual int measure(const Point &p) const { return p.x; }
    virtual int quiet() const noexcept { return 1; }
    virtual const std::string &label() const
    {
        static const std::string text = "c++";
        return text;
    }
    int ask_quiet() const { return quiet(); }
    std::string ask_label() const { return label(); }
    int probe() const
    {
        const Point p{5};
        return measure(p);
    }
    virtual int look(const Node &n) const { return static_cast<int>(n.child_count()); }
    int probe_node() const
    {
        const Node n;
        return look(n);
    }
    int ask(int k) const { return area(k); }
    std::string told() const { return name(); }
    int counted() const { return sides(); }
    int scale;
};
struct Square : Shape {
    using Shape::Shape;
    int sides() const override { return 4; }
    int area(int k) const override { return scale * scale * k; }
};
}
struct Counting {
    static std::generator<int> filtered(int n, std::function<bool(int)> keep)
    {
        for (int i = 1; i <= n; i++)
            if (keep(i)) co_yield i;
    }
    int start = 1;
    static std::generator<int> up_to(int n)
    {
        for (int i = 1; i <= n; i++) co_yield i;
    }
    std::generator<int> from_start(int n) const
    {
        for (int i = start; i < start + n; i++) co_yield i;
    }
    static std::generator<int> letters_of(const std::string &text)
    {
        for (const char c : text) co_yield c;
    }
    static std::generator<std::string> words(int n)
    {
        for (int i = 0; i < n; i++) co_yield std::string(static_cast<std::size_t>(i + 1), 'a');
    }
};
struct Groups {
    std::vector<std::set<int>> sets{{1}};
    Counting counting;
};
struct Grid {
    int cells[3] = {1, 2, 3};
    const double fixed[2] = {0.5, 1.5};
    int sum() const { return cells[0] + cells[1] + cells[2]; }
};
struct Mark {
    int n = 1;
};
struct Choices {
    std::variant<int, std::string, Mark> value = 5;
    std::variant<double, std::string> measure = 0.5;
    std::variant<int, std::string, Mark> which(int kind) const
    {
        if (kind == 0) return 7;
        if (kind == 1) return std::string("text");
        return Mark{3};
    }
    std::size_t take(const std::variant<int, std::string, Mark> &v) const { return v.index(); }
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
struct PlainSharer {
    std::shared_ptr<Plain> held;
    std::shared_ptr<Plain> make() { held = std::make_shared<Plain>(); return held; }
    void keep(const std::shared_ptr<Plain> &p) { held = p; }
    mrb_int read() const { return held->n; }
};
struct Fragile {
    int v = 1;
    Fragile() = default;
    Fragile(const Fragile &) { throw std::runtime_error("no copy"); }
    Fragile &operator=(const Fragile &) { throw std::runtime_error("no assign"); }
};
struct TakesFragile {
    int take(Fragile f) const { return f.v; }
};
struct FragileHolder {
    Fragile item;
};
constexpr auto classes = mrb_cpp_reflector::reflect<^^Reflected, ^^D, ^^S, ^^Z, ^^X, ^^Y, ^^F, ^^Operand, ^^Static, ^^Thrower, ^^Callback, ^^Node, ^^Leaf, ^^Forest, ^^Hand, ^^Sharer, ^^SelfSharer, ^^WatchedHolder, ^^Lender, ^^Shelf, ^^PlainSharer, ^^Fragile, ^^TakesFragile, ^^FragileHolder, ^^Scored, ^^Measure, ^^Link, ^^Groups, ^^free_functions, ^^Odd, ^^Converts, ^^Outer, ^^Diamond, ^^TakesRvalues, ^^Flags, ^^Declared, ^^Unbuilt, ^^Held<long>, ^^Color, ^^Flag, ^^Palette, ^^Mark, ^^Choices, ^^Grid, ^^Counting, ^^Keeper, ^^ConvertsExplicitly, ^^std::pair<const std::string, int>, ^^std::pair<std::string, int>>();
constexpr auto under = mrb_cpp_reflector::reflect<^^Plain>();
constexpr auto nested = mrb_cpp_reflector::reflect<^^Holder>();
constexpr auto named = mrb_cpp_reflector::reflect<^^fruit::Basket::count<fruit::Apple>>();
constexpr auto operators = mrb_cpp_reflector::reflect<^^ops, ^^ops::Vec, ^^ops::Log>();
constexpr auto variables = mrb_cpp_reflector::reflect<^^globals, ^^globals::Registry, ^^only_one::picked>();
constexpr auto overridable = mrb_cpp_reflector::reflect<^^shapes::Shape, ^^shapes::Square>();
constexpr auto instantiated = mrb_cpp_reflector::reflect<^^fruit, ^^fruit::Apple, ^^fruit::Pear, ^^fruit::Kind, ^^fruit::Scale>();

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

/* mrb_close frees every object. A tree whose parents and children are
 * all alive then must lose each C++ object once, children before their
 * parents, and no free may read an mruby object that is already freed. */
static mrb_value tree_freed_at_close_q(mrb_state *mrb, mrb_value)
{
    const mrb_int nodes = nodes_alive();
    const mrb_int leaves = leaves_alive();
    mrb_state *const other = mrb_open();
    mrb_cpp_reflector::reflect_define<classes>(other);
    mrb_load_string(other, "$kept = []\n"
                           "30.times do\n"
                           "  r = Node.new; r.grow; r.grow; r.first.grow\n"
                           "  $kept << r.first.first << r.first.tag << r << r.second << r.first\n"
                           "  l = Leaf.new; l.grow; l.child.grow\n"
                           "  $kept << l.child.child.tag << l << l.child << l.child.child\n"
                           "end\n");
    const bool raised = other->exc != nullptr;
    const bool grown = nodes_alive() == nodes + 120 && leaves_alive() == leaves + 90;
    mrb_close(other);
    return mrb_bool_value(!raised && grown && nodes_alive() == nodes && leaves_alive() == leaves);
}

static mrb_value full_gc_m(mrb_state *mrb, mrb_value)
{
    mrb_full_gc(mrb);
    return mrb_nil_value();
}

/* The number of live objects after a full collection tells whether
 * something grows with the number of calls. */
static mrb_value live_objects_m(mrb_state *mrb, mrb_value)
{
    mrb_full_gc(mrb);
    mrb_full_gc(mrb);
    return mrb_int_value(mrb, static_cast<mrb_int>(mrb->gc.live));
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
    return mrb_load_string(mrb, "begin; Declared.new.undefined; rescue NotImplementedError; :raised_here; end");
}

extern "C" void mrb_mruby_cpp_reflection_gem_test(mrb_state *mrb)
{
    mrb_define_module_function(mrb, mrb->kernel_module, "undefined_after_other_state", undefined_after_other_state_m, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "full_gc", full_gc_m, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "tree_freed_at_close?", tree_freed_at_close_q, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "live_objects", live_objects_m, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "cancelled_through_a_call?", cancelled_m, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "second_state", second_state_m, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "constructed", constructed_m, MRB_ARGS_NONE());
    mrb_define_module_function(mrb, mrb->kernel_module, "reflect_presym_ok?", presym_ok_q, MRB_ARGS_NONE());
    mrb_cpp_reflector::reflect_define<classes>(mrb);
    mrb_cpp_reflector::reflect_define<under>(mrb, mrb_define_module(mrb, "Under"));
    mrb_cpp_reflector::reflect_define<nested, {.nested_types = true}>(mrb);
    mrb_cpp_reflector::reflect_define<named>(mrb);
    mrb_cpp_reflector::reflect_define<operators>(mrb);
    mrb_cpp_reflector::reflect_define<variables>(mrb);
    mrb_cpp_reflector::reflect_define<overridable, {.virtual_overriders = true}>(mrb);
    mrb_cpp_reflector::reflect_define<instantiated, {.templates = true}>(mrb);
}
#else
extern "C" void mrb_mruby_cpp_reflection_gem_test(mrb_state *) {}
#endif
