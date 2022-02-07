#ifndef ARM_ENCODE_H
#define ARM_ENCODE_H

#include <stdint.h>

static inline uint32_t arm_encode_movw(uint16_t reg, uint16_t immed)
{
	// 1110 0011 0000 XXXX YYYY XXXXXXXXXXXX
	// where X is the immediate and Y is the register
	// Upper bits == 0xE30
	return ((uint32_t)0xE30 << 20) | ((uint32_t)(immed & 0xF000) << 4) | (immed & 0xFFF) | (reg << 12);
}

static inline uint32_t arm_encode_movt(uint16_t reg, uint16_t immed)
{
	// 1110 0011 0100 XXXX YYYY XXXXXXXXXXXX
	// where X is the immediate and Y is the register
	// Upper bits == 0xE34
	return ((uint32_t)0xE34 << 20) | ((uint32_t)(immed & 0xF000) << 4) | (immed & 0xFFF) | (reg << 12);
}

static inline uint32_t arm_encode_bx(uint16_t reg)
{
	// 1110 0001 0010 111111111111 0001 YYYY
	// BX Rn has 0xE12FFF1 as top bytes
	return ((uint32_t)0xE12FFF1 << 4) | reg;
}

#define arm_encode_ret() arm_encode_bx(14)

#endif
