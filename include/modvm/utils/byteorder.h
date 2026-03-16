/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_UTILS_BYTEORDER_H
#define MODVM_UTILS_BYTEORDER_H

#include <stdint.h>
#include <stddef.h>

/* Use compiler built-ins for optimal byte swapping instructions */
#define swab16(x) __builtin_bswap16(x)
#define swab32(x) __builtin_bswap32(x)
#define swab64(x) __builtin_bswap64(x)

#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
	__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__

#define cpu_to_le16(x) ((uint16_t)(x))
#define le16_to_cpu(x) ((uint16_t)(x))
#define cpu_to_le32(x) ((uint32_t)(x))
#define le32_to_cpu(x) ((uint32_t)(x))
#define cpu_to_le64(x) ((uint64_t)(x))
#define le64_to_cpu(x) ((uint64_t)(x))

#define cpu_to_be16(x) swab16((uint16_t)(x))
#define be16_to_cpu(x) swab16((uint16_t)(x))
#define cpu_to_be32(x) swab32((uint32_t)(x))
#define be32_to_cpu(x) swab32((uint32_t)(x))
#define cpu_to_be64(x) swab64((uint64_t)(x))
#define be64_to_cpu(x) swab64((uint64_t)(x))

#elif defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && \
	__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__

#define cpu_to_le16(x) swab16((uint16_t)(x))
#define le16_to_cpu(x) swab16((uint16_t)(x))
#define cpu_to_le32(x) swab32((uint32_t)(x))
#define le32_to_cpu(x) swab32((uint32_t)(x))
#define cpu_to_le64(x) swab64((uint64_t)(x))
#define le64_to_cpu(x) swab64((uint64_t)(x))

#define cpu_to_be16(x) ((uint16_t)(x))
#define be16_to_cpu(x) ((uint16_t)(x))
#define cpu_to_be32(x) ((uint32_t)(x))
#define be32_to_cpu(x) ((uint32_t)(x))
#define cpu_to_be64(x) ((uint64_t)(x))
#define be64_to_cpu(x) ((uint64_t)(x))

#else
#error "Unknown host byte order. Compiler does not define __BYTE_ORDER__."
#endif

static inline void le16_add_cpu(uint16_t *var, uint16_t val)
{
	*var = cpu_to_le16(le16_to_cpu(*var) + val);
}

static inline void le32_add_cpu(uint32_t *var, uint32_t val)
{
	*var = cpu_to_le32(le32_to_cpu(*var) + val);
}

static inline void le64_add_cpu(uint64_t *var, uint64_t val)
{
	*var = cpu_to_le64(le64_to_cpu(*var) + val);
}

static inline void be16_add_cpu(uint16_t *var, uint16_t val)
{
	*var = cpu_to_be16(be16_to_cpu(*var) + val);
}

static inline void be32_add_cpu(uint32_t *var, uint32_t val)
{
	*var = cpu_to_be32(be32_to_cpu(*var) + val);
}

static inline void be64_add_cpu(uint64_t *var, uint64_t val)
{
	*var = cpu_to_be64(be64_to_cpu(*var) + val);
}

static inline void le32_to_cpu_array(uint32_t *buf, size_t words)
{
	size_t i;
	for (i = 0; i < words; i++)
		buf[i] = le32_to_cpu(buf[i]);
}

static inline void cpu_to_le32_array(uint32_t *buf, size_t words)
{
	size_t i;
	for (i = 0; i < words; i++)
		buf[i] = cpu_to_le32(buf[i]);
}

static inline void be32_to_cpu_array(uint32_t *buf, size_t words)
{
	size_t i;
	for (i = 0; i < words; i++)
		buf[i] = be32_to_cpu(buf[i]);
}

static inline void cpu_to_be32_array(uint32_t *buf, size_t words)
{
	size_t i;
	for (i = 0; i < words; i++)
		buf[i] = cpu_to_be32(buf[i]);
}

#endif /* MODVM_UTILS_BYTEORDER_H */