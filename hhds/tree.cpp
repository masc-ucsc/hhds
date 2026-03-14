// test.cpp
#include "tree.hpp"

static_assert(sizeof(hhds::Tree_pointers) == 192);  // 64B alignment keeps the struct at three cache lines
