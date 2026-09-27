#include "lib.hpp"
int main() { return Lib{}.present() == 1 ? Lib{}.missing() : 1; }
