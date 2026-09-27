#include <meta>
int main() { return std::meta::is_integral_type(^^int) ? 0 : 1; }
