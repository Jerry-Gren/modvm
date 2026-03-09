/* SPDX-License-Identifier: GPL-2.0 */
/*
 * boot.s - minimal bare-metal firmware for ModVM
 *
 * This firmware executes in 16-bit real mode. It demonstrates
 * basic text output over the emulated 16550A UART console.
 */

.code16
.global _start

_start:
	/* Initialize Data Segment to 0x0000 */
	xorw %ax, %ax
	movw %ax, %ds

	/* Point DX to the COM1 base port */
	movw $0x3f8, %dx
	
	/* Load the address of our greeting string into Source Index */
	leaw greeting, %si

print_loop:
	/* Load byte at DS:SI into AL and increment SI */
	lodsb
	
	/* Check if we reached the null terminator */
	testb %al, %al
	jz halt_system
	
	/* Output the character in AL to the port specified in DX */
	outb %al, %dx
	jmp print_loop

halt_system:
	/* Write to our custom Debug Exit port (0x500) to power off the machine */
	movw $0x500, %dx
	movb $0x01, %al
	outb %al, %dx
1:
	hlt
	jmp 1b

greeting:
	.asciz "Hello from ModVM Bare-Metal Firmware!\r\n"
