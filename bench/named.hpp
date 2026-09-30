#pragma once
#include <cstring>
#include <string_view>

namespace named {
class Named {
    const char *name = "";
    std::string_view label;

public:
    void set_name(const char *const given) { name = given; }
    void set_label(const std::string_view given) { label = given; }
    long length_of_name(const char *const given) const { return static_cast<long>(std::strlen(given)); }
    long length_of_label(const std::string_view given) const { return static_cast<long>(given.size()); }
};
}
