#pragma once
#include <functional>
#include <string>
#include <vector>

#define SHOWCASE_VERSION "1.0"
#define SHOWCASE_MAX_ITEMS 64

namespace showcase {

enum class Unit { gram, kilogram };

struct Item {
    std::string name;
    double weight = 0;
    Unit unit = Unit::gram;
};

class Basket {
    std::vector<Item> items;
    std::function<void(const Item &)> on_add;

public:
    Basket() = default;
    explicit Basket(const std::string &first) { add(first, 1); }

    void add(const std::string &name, double weight, Unit unit = Unit::gram)
    {
        items.push_back({name, weight, unit});
        if (on_add) on_add(items.back());
    }
    void add(const Item &item) { add(item.name, item.weight, item.unit); }

    std::size_t size() const { return items.size(); }
    double total_grams() const
    {
        double total = 0;
        for (const Item &item : items) total += item.unit == Unit::kilogram ? item.weight * 1000 : item.weight;
        return total;
    }
    const std::vector<Item> &contents() const { return items; }
    void when_added(std::function<void(const Item &)> callback) { on_add = std::move(callback); }
};

inline void scale(double factors[3], const double by)
{
    for (int i = 0; i < 3; i++) factors[i] *= by;
}

inline Basket make_basket() { return {}; }

}
