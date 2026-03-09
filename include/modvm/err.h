/* SPDX-License-Identifier: GPL-2.0 */
#ifndef MODVM_ERR_H
#define MODVM_ERR_H

#include <stdint.h>
#include <stdbool.h>
#include <modvm/compiler.h>

/*
 * Maximum error number.
 * Standard POSIX error codes (like ENOMEM, EINVAL) are well within the
 * 1 to 4095 range.
 */
#define MAX_ERRNO 4095

/**
 * IS_ERR_VALUE - determine if a pointer value falls in the error range
 * @x: The unsigned long representation of the pointer
 *
 * In a 64-bit address space, valid user-space pointers will never
 * naturally fall into the extremely high range representing negative
 * integers from -1 to -4095.
 */
#define IS_ERR_VALUE(x) \
	((unsigned long)(void *)(x) >= (unsigned long)-MAX_ERRNO)

/**
 * ERR_PTR - Convert a negative error code to a pointer
 * @error: The negative error code (e.g., -ENOMEM)
 */
static inline void *ERR_PTR(long error)
{
	return (void *)error;
}

/**
 * PTR_ERR - extract the error code from an error pointer
 * @ptr: the error pointer
 *
 * Return: the negative error code.
 */
static inline long PTR_ERR(const void *ptr)
{
	return (long)ptr;
}

/**
 * IS_ERR - check if a pointer is actually an error code
 * @ptr: the pointer to check
 */
static inline bool IS_ERR(const void *ptr)
{
	return IS_ERR_VALUE((unsigned long)ptr);
}

/**
 * IS_ERR_OR_NULL - check if a pointer is NULL or an error code
 * @ptr: the pointer to check
 */
static inline bool IS_ERR_OR_NULL(const void *ptr)
{
	return !ptr || IS_ERR_VALUE((unsigned long)ptr);
}

#endif /* MODVM_ERR_H */