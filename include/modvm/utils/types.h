/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_UTILS_TYPES_H
#define MODVM_UTILS_TYPES_H

#include <stdint.h>

/*
 * By wrapping the raw integers inside structs, we force GCC and Clang to
 * perform strict type checking without relying on external static analyzers
 * like Sparse. Assigning a raw integer to these types, or mixing LE/BE types,
 * will result in an immediate compiler error:
 * "incompatible types when assigning to type 'le32_t' from type 'int'"
 */

typedef struct {
	uint16_t __val;
} le16_t;
typedef struct {
	uint16_t __val;
} be16_t;

typedef struct {
	uint32_t __val;
} le32_t;
typedef struct {
	uint32_t __val;
} be32_t;

typedef struct {
	uint64_t __val;
} le64_t;
typedef struct {
	uint64_t __val;
} be64_t;

/* Virtio-specific type aliases */
typedef le16_t virtio16_t;
typedef le32_t virtio32_t;
typedef le64_t virtio64_t;

#endif /* MODVM_UTILS_TYPES_H */