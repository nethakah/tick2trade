#ifndef CONTRACTS_HPP
#define CONTRACTS_HPP

#include <cassert>
#include <cstdio>

#define REQUIRES(condition) \
    assert((condition) && "PRE-CONDITION")
#define ENSURES(condition) \
    assert((condition) && "POST-CONDITION")
#define ASSERT(condition) \
    assert((condition) && "ASSERTION")

#endif // CONTRACTS_HPP