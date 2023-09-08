#ifndef BITSET_H
#define BITSET_H

#include <assert.h>
#include <stdint.h>

#define BITSET_DEFINE(name, size)        \
	uint32_t name[(size + 31) / 32]; \
	static_assert((size) % 32 == 0, "Bitset size must be a multiple of 32!")

#define BITSET_SET(bitset, index) bitset[(index) / 32] |= 1u << ((index) % 32)

#define BITSET_CLEAR(bitset, index) bitset[(index) / 32] &= ~(1u << ((index) % 32))

#define BITSET_IS_SET(bitset, index) (bitset[(index) / 32] & (1u << ((index) % 32)))

#define bitset_for_each_bit_set(bitset, index)                                      \
	for (uint32_t __i = 0, index; __i < (sizeof(bitset) + 31) / 32; __i++)      \
		for (uint32_t __val = bitset[__i], __j;                             \
		     __j = __builtin_ffs(__val), index = __i * 32 + (__j - 1), __j; \
		     __val &= ~(1u << (__j - 1)))

#define bitset_find_first_clear_and_set(bitset)                             \
	({                                                                  \
		uint32_t val, ret = UINT32_MAX;                             \
		for (uint32_t i = 0; i < (sizeof(bitset) + 31) / 32; i++) { \
			if ((val = bitset[i], val == 0) ||                  \
			    (val = __builtin_ctz(~val), ~val != 0)) {       \
				bitset[i] |= 1u << val;                     \
				ret = i * 32 + val;                         \
				break;                                      \
			}                                                   \
		}                                                           \
		ret;                                                        \
	})

#endif