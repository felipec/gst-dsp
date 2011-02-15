/*
 * Copyright (C) 2004 Michael Niedermayer
 *
 * This file may be used under the terms of the GNU Lesser General Public
 * License version 2.1, a copy of which is found in LICENSE included in the
 * packaging of this file.
 */

/* This was borrowed from FFmpeg */

#ifndef GET_BITS_H
#define GET_BITS_H

#include <stdint.h>

struct get_bit_context {
	const uint8_t *buffer, *buffer_end;
	unsigned index;
	unsigned size_in_bits;
};

static inline void skip_bits(struct get_bit_context *s, int n)
{
	unsigned index = s->index;
	index += n;
	s->index = index;
}

static inline void init_get_bits(struct get_bit_context *s, const uint8_t *buffer, unsigned bit_size)
{
	s->buffer = buffer;
	s->buffer_end = buffer + ((bit_size + 7) >> 3);
	s->size_in_bits = bit_size;
	s->index = 0;
}

static inline unsigned get_bits1(struct get_bit_context *s)
{
	unsigned index = s->index;
	uint8_t result = s->buffer[index >> 3];
	result <<= (index & 0x07);
	result >>= 8 - 1;
	index++;
	s->index = index;
	return result;
}

union unaligned_16 { uint16_t l; } __attribute__((packed));
union unaligned_32 { uint32_t l; } __attribute__((packed));

#define AV_RN(s, p) (((const union unaligned_##s *) (p))->l)

#if defined(__i386__)
static inline uint16_t av_bswap16(uint16_t x)
{
	__asm__("rorw $8, %0" : "+r"(x));
	return x;
}
static inline uint32_t av_bswap32(uint32_t x)
{
	__asm__("bswap   %0" : "+r" (x));
	return x;
}
#define AV_RB16(x) av_bswap16(AV_RN(16, x))
#define AV_RB32(x) av_bswap32(AV_RN(32, x))
#elif defined(__arm__)
static inline uint16_t av_read16_be(const uint8_t *p)
{
	uint16_t v;
	__asm__("ldrh %0, %1\n\trev16 %0, %0" : "=r"(v) : "m"(*(const uint16_t *)p));
	return v;
}
static inline uint32_t av_read32_be(const uint8_t *p)
{
	uint32_t v;
	__asm__("ldr %0, %1" : "=r"(v) : "m"(*(const uint32_t *)p));
	__asm__("rev %0, %0" : "+r"(v));
	return v;
}
#define AV_RB16(x) av_read16_be(x)
#define AV_RB32(x) av_read32_be(x)
#endif

#ifndef AV_RB16
#define AV_RB16(x) \
	((((const uint8_t *)(x))[0] << 8) | \
	 ((const uint8_t *)(x))[1])
#endif

#ifndef AV_RB32
#define AV_RB32(x) \
	((((const uint8_t *)(x))[0] << 24) | \
	 (((const uint8_t *)(x))[1] << 16) | \
	 (((const uint8_t *)(x))[2] <<  8) | \
	 ((const uint8_t *)(x))[3])
#endif

static inline unsigned get_bits(struct get_bit_context *s, int n)
{
	unsigned index = s->index;
	int re_cache = 0;
	register int tmp;
	re_cache = AV_RB32(((const uint8_t *)s->buffer) + (index >> 3)) << (index & 0x07);
	tmp = ((uint32_t)re_cache) >> (32 - n);
	index += n;
	s->index = index;
	return tmp;
}

static inline unsigned get_bits_count(const struct get_bit_context *s)
{
	return s->index;
}

static inline int get_bits_left(const struct get_bit_context *s)
{
	return s->size_in_bits - get_bits_count(s);
}

#endif
