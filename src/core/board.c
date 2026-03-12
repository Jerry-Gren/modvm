/* SPDX-License-Identifier: GPL-2.0 */
#include <string.h>

#include <modvm/core/board.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/compiler.h>

#undef pr_fmt
#define pr_fmt(fmt) "board: " fmt

#define MAX_BOARD_CLASSES 16

static const struct modvm_board *board_classes[MAX_BOARD_CLASSES];
static int nr_board_classes = 0;

/**
 * modvm_board_register - statically register a motherboard blueprint
 * @board: the board definition to expose to the system
 *
 * Typically invoked automatically via compiler constructor attributes
 * before the main routine executes.
 */
void modvm_board_register(const struct modvm_board *board)
{
	if (WARN_ON(!board || !board->name))
		return;

	if (WARN_ON(nr_board_classes >= MAX_BOARD_CLASSES)) {
		pr_err("maximum board registry capacity exceeded\n");
		return;
	}

	board_classes[nr_board_classes++] = board;
}

/**
 * modvm_board_find - retrieve a motherboard blueprint by its identifier
 * @name: the string identifier of the board type
 *
 * Return: pointer to the board definition, or NULL if unsupported.
 */
const struct modvm_board *modvm_board_find(const char *name)
{
	int i;

	if (WARN_ON(!name))
		return NULL;

	for (i = 0; i < nr_board_classes; i++) {
		if (strcmp(board_classes[i]->name, name) == 0)
			return board_classes[i];
	}

	return NULL;
}