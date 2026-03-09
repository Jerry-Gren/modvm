/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

#include <modvm/bus.h>
#include <modvm/hw/serial_reg.h>
#include <modvm/hw/serial.h>
#include <modvm/compiler.h>
#include <modvm/log.h>
#include <modvm/bug.h>
#include <modvm/os_thread.h>

#undef pr_fmt
#define pr_fmt(fmt) "uart_16550a: " fmt

/**
 * struct uart_state - Internal state machine for the 16550A UART
 * @dev: Base virtual device structure for bus registration
 * @lock: Mutex to serialize access from multiple vCPU threads
 * @irq_pin: 
 * @irq: Hardware interrupt line number
 * @dll: Divisor Latch Low
 * @dlm: Divisor Latch High
 * @ier: Interrupt Enable Register
 * @fcr: FIFO Control Register
 * @lcr: Line Control Register
 * @mcr: Modem Control Register
 * @lsr: Line Status Register
 * @msr: Modem Status Register
 * @scr: Scratchpad Register
 * @thr: Transmit Holding Register (emulated outgoing buffer)
 * @rbr: Receive Buffer Register (emulated incoming buffer)
 * @thre_int_pending: Internal flip-flop for edge-triggered THRE interrupts
 *
 * Encapsulates all internal registers to emulate a standard PC16550D UART.
 * The mutex ensures that read-modify-write cycles on registers remain
 * atomic when targeted by parallel execution units.
 */
struct uart_state {
	struct vm_device dev;
	struct os_mutex *lock;
	struct vm_irq *irq_pin;
	struct vm_chardev *chr;

	uint8_t irq;

	uint8_t dll;
	uint8_t dlm;
	uint8_t ier;
	uint8_t fcr;
	uint8_t lcr;
	uint8_t mcr;
	uint8_t lsr;
	uint8_t msr;
	uint8_t scr;

	uint8_t thr;
	uint8_t rbr;

	bool thre_int_pending;
};

/**
 * uart_is_dlab_set - check if the Divisor Latch Access Bit is enabled
 * @uart: pointer to the uart state machine
 */
static inline bool uart_is_dlab_set(struct uart_state *uart)
{
	return (uart->lcr & UART_LCR_DLAB) != 0;
}

static inline bool uart_is_loopback(struct uart_state *uart)
{
	/* According to 8.6.7, MCR bit 4 enables local loopback feature */
	return (uart->mcr & UART_MCR_LOOP) != 0;
}

/**
 * uart_get_iir - dynamically synthesize the Interrupt Identification Register
 * @uart: the UART state machine
 *
 * Even without hardware interrupt injection, drivers read the IIR to determine
 * the logical state of the chip. We synthesize this based on enabled interrupts
 * (IER) and current line/modem status.
 */
static uint8_t uart_get_iir(struct uart_state *uart)
{
	/*
         * Bits 6 and 7 are set to 1 if FIFOs are enabled.
         * We initialize the base value here to prevent subsequent
         * bitwise OR operations from accidentally clobbering the state
         * if the logic tree grows more complex in the future.
         */
	uint8_t iir = (uart->fcr & UART_FCR_ENABLE_FIFO) ? 0xc0 : 0x00;

	/* Priority 1: Receiver Line Status*/
	if ((uart->ier & UART_IER_RLSI) &&
	    (uart->lsr & UART_LSR_BRK_ERROR_BITS)) {
		iir |= 0x06;
		return iir;
	}

	/*
	 * Priority 2: Received Data Available.
	 * Triggered if Data Ready (DR) is set AND the interrupt is enabled.
	 */
	if ((uart->ier & UART_IER_RDI) && (uart->lsr & UART_LSR_DR)) {
		iir |= 0x04;
		return iir;
	}

	/* Priority 3: Transmitter Holding Register Empty */
	if ((uart->ier & UART_IER_THRI) && uart->thre_int_pending) {
		iir |= 0x02;
		return iir;
	}

	/* Priority 4: Modem Status */
	if ((uart->ier & UART_IER_MSI) && (uart->msr & UART_MSR_ANY_DELTA)) {
		iir |= 0x00;
		return iir;
	}

	/* No pending logical interrupts */
	iir |= UART_IIR_NO_INT;

	return iir;
}

/**
 * uart_update_irq - evaluate internal state and drive the INTR pin
 * @uart: the UART state machine
 *
 * Analyzes the synthesized Interrupt Identification Register (IIR).
 * If any logical interrupt condition is met and unmasked, it asserts
 * the physical IRQ line into the virtual machine.
 */
static void uart_update_irq(struct uart_state *uart)
{
	uint8_t iir;
	int level;

	iir = uart_get_iir(uart);

	/*
         * Bit 0 of IIR is logic 0 when an interrupt is pending.
         * Otherwise, it is logic 1 (No interrupt pending).
         */
	level = (iir & UART_IIR_NO_INT) ? 0 : 1;

	vm_irq_set(uart->irq_pin, level);
}

/**
 * uart_update_msr - update Modem Status Register based on MCR
 * @uart: the UART state machine
 *
 * In loopback mode, the modem control outputs (DTR, RTS, OUT1, OUT2)
 * are internally routed to the modem control inputs (DSR, CTS, RI, DCD).
 */
static void uart_update_msr(struct uart_state *uart)
{
	uint8_t old_msr = uart->msr;
	uint8_t new_msr = 0;

	if (!uart_is_loopback(uart)) {
		/*
		 * In normal mode, MSR is driven by external hardware lines.
		 * We simulate unconnected lines where Carrier Detect, Data Set Ready,
		 * and Clear To Send are asserted (typical null-modem behavior).
		 */
		new_msr = UART_MSR_DCD | UART_MSR_DSR | UART_MSR_CTS;
	} else {
		/*
		 * Loopback wiring mapping per Datasheet 8.6.7:
		 * MCR Bit 0 (DTR)  -> MSR Bit 5 (DSR)
		 * MCR Bit 1 (RTS)  -> MSR Bit 4 (CTS)
		 * MCR Bit 2 (OUT1) -> MSR Bit 6 (RI)
		 * MCR Bit 3 (OUT2) -> MSR Bit 7 (DCD)
		 */
		if (uart->mcr & UART_MCR_DTR)
			new_msr |= UART_MSR_DSR;
		if (uart->mcr & UART_MCR_RTS)
			new_msr |= UART_MSR_CTS;
		if (uart->mcr & UART_MCR_OUT1)
			new_msr |= UART_MSR_RI;
		if (uart->mcr & UART_MCR_OUT2)
			new_msr |= UART_MSR_DCD;
	}

	uint8_t changed = ((old_msr ^ new_msr) &
			   (UART_MSR_DCD | UART_MSR_DSR | UART_MSR_CTS)) >>
			  4;

	/*
	 * Trailing Edge of Ring Indicator (TERI).
	 * Triggers when physical RI goes 0->1, which means MSR bit 6 goes 1->0
	 * due to the pin complement logic described in the datasheet.
	 */
	if ((old_msr & UART_MSR_RI) && !(new_msr & UART_MSR_RI)) {
		changed |= UART_MSR_TERI;
	}

	uart->msr = new_msr | (old_msr & UART_MSR_ANY_DELTA) | changed;
}

/**
 * uart_read_register
 *
 * Handles a single byte read from a specific UART offset.
 */
static uint8_t uart_read_register(struct uart_state *uart, uint16_t offset)
{
	uint8_t ret = 0;

	switch (offset) {
	case UART_RX:
		if (uart_is_dlab_set(uart))
			return uart->dll;

		ret = uart->rbr;
		/*
		 * Reading the Receiver Buffer clears the Data Ready (DR) bit
		 * in the Line Status Register, per 8.6.3.
		 */
		uart->lsr &= ~UART_LSR_DR;
		return ret;

	case UART_IER:
		if (uart_is_dlab_set(uart))
			return uart->dlm;
		return uart->ier;

	case UART_IIR:
		ret = uart_get_iir(uart);
		/*
		 * Reading IIR acknowledges the THRE interrupt per datasheet.
		 * Priority 3 mask is 0x02.
		 */
		if ((ret & 0x0f) == 0x02)
			uart->thre_int_pending = false;
		return ret;

	case UART_LCR:
		return uart->lcr;

	case UART_MCR:
		return uart->mcr;

	case UART_LSR:
		/*
		 * Physical state: transmission is infinitely fast,
		 * so holding register and shift register are always empty.
		 */
		uart->lsr |= (UART_LSR_THRE | UART_LSR_TEMT);
		ret = uart->lsr;
		uart->lsr &= ~UART_LSR_BRK_ERROR_BITS;
		return ret;

	case UART_MSR:
		ret = uart->msr;
		/* Datasheet 8.6.8: Delta is cleared after CPU reads MSR */
		uart->msr &= ~UART_MSR_ANY_DELTA;
		return ret;

	case UART_SCR:
		return uart->scr;

	default:
		pr_warn_once("Read from unknown UART offset: 0x%x\n", offset);
		return 0xff;
	}
}

/**
 * uart_write_register - handle a single byte write to a specific UART offset
 *
 * Expected to be called with the hardware state lock held.
 */
static void uart_write_register(struct uart_state *uart, uint16_t offset,
				uint8_t val)
{
	switch (offset) {
	case UART_TX:
		if (uart_is_dlab_set(uart)) {
			uart->dll = val;
			return;
		}

		if (uart_is_loopback(uart)) {
			/*
			 * In loopback mode, transmitted data is immediately
			 * received in the Receive Buffer Register, and Data Ready
			 * bit is set in LSR.
			 */
			if (uart->lsr & UART_LSR_DR) {
				uart->lsr |= UART_LSR_OE;
			} else {
				uart->rbr = val;
				uart->lsr |= UART_LSR_DR;
			}
		} else {
			/*
			 * Dispatch the payload to the attached host backend.
			 * The frontend hardware does not care how the backend processes it.
			 */
			if (uart->chr && uart->chr->ops->write)
				uart->chr->ops->write(uart->chr, &val, 1);
		}

		/**
		 * Emulated transmission is instantaneous. The transmit
                 * holding register (THR) and transmit shift register (TSR)
                 * remain empty at all times in this model.
                 * We trigger the THRE interrupt pending flag but do not
                 * clear the THRE status bit in the LSR, reflecting the
                 * immediate consumption of the data.
                 */
		uart->thre_int_pending = true;
		/* uart->lsr &= ~UART_LSR_THRE; */
		break;

	case UART_IER: {
		if (uart_is_dlab_set(uart)) {
			uart->dlm = val;
			return;
		}
		/* 
		 * Datasheet 8.6.3: IER bits 4-7 are always logic 0
		 */
		uint8_t old_ier = uart->ier;
		uart->ier = val & 0x0f;

		/*
		 * If the THRE interrupt was just enabled, and the buffer is
		 * currently empty (which it always is in our simulation),
		 * the interrupt must fire immediately to satisfy OS IRQ probing.
		 */
		if (!(old_ier & UART_IER_THRI) && (uart->ier & UART_IER_THRI)) {
			uart->thre_int_pending = true;
		}
		break;
	}

	case UART_FCR:
		/* Mask out reserved bits 4 and 5 (0xcf = 1100 1111) */
		val &= 0xcf;

		/*
		 * Datasheet 8.6.4:
		 * 1. Writing 0 to bit 0 (ENABLE_FIFO) will disable other bits
		 * 2. Writing 1 to bit 1 (CLEAR_RCVR) or bit 2 (CLEAR_XMIT) clears themselves
		 */
		if (!(val & UART_FCR_ENABLE_FIFO)) {
			uart->fcr = val & UART_FCR_ENABLE_FIFO;
			/* Disabling FIFOs automatically drops all queued data */
			uart->lsr &= ~(UART_LSR_DR | UART_LSR_OE);
		} else {
			uart->fcr = val & ~(UART_FCR_CLEAR_RCVR |
					    UART_FCR_CLEAR_XMIT);
			if (val & UART_FCR_CLEAR_RCVR)
				uart->lsr &= ~(UART_LSR_DR | UART_LSR_OE);
		}
		break;

	case UART_LCR:
		uart->lcr = val;
		/* Emulate Break signal reflection in loopback mode */
		if (uart_is_loopback(uart)) {
			if (uart->lcr & UART_LCR_SBC)
				uart->lsr |= UART_LSR_BI;
			else
				uart->lsr &= ~UART_LSR_BI;
		}
		break;

	case UART_MCR:
		/*
		 * Datasheet 8.6.7: MCR bits 5-7 are always logic 0
		 */
		uart->mcr = val & 0x1f;
		/* Changing MCR requires re-evaluating MSR wiring */
		uart_update_msr(uart);
		break;

	case UART_LSR:
		/*
		 * Note in 8.6.3: "Writing to this register is not recommended."
		 * Real hardware ignores most writes here.
		 */
		break;

	case UART_SCR:
		uart->scr = val;
		break;

	default:
		pr_warn_once("Write to unknown UART offset: 0x%x\n", offset);
		break;
	}
}

static uint64_t uart_read(struct vm_device *dev, uint64_t offset, uint8_t size)
{
	struct uart_state *uart = dev->private_data;
	uint64_t result = 0;
	uint8_t i;

	os_mutex_lock(uart->lock);

	for (i = 0; i < size; i++) {
		uint8_t val = uart_read_register(uart, offset + i);
		result |= ((uint64_t)val << (i * 8));
	}

	/* Reading registers may clear pending interrupts */
	uart_update_irq(uart);

	os_mutex_unlock(uart->lock);

	return result;
}

static void uart_write(struct vm_device *dev, uint64_t offset, uint64_t value,
		       uint8_t size)
{
	struct uart_state *uart = dev->private_data;
	uint8_t i;

	os_mutex_lock(uart->lock);

	for (i = 0; i < size; i++) {
		uint8_t val = (uint8_t)((value >> (i * 8)) & 0xff);
		uart_write_register(uart, offset + i, val);
	}

	/* Writing registers may clear or trigger new interrupts */
	uart_update_irq(uart);

	os_mutex_unlock(uart->lock);
}

/**
 * uart_receive_byte - hardware model callback for incoming data
 * @opaque: pointer to the internal uart_state
 * @buf: incoming byte stream from the host backend
 * @len: number of bytes available
 *
 * Simulates electrical signals arriving on the UART RX pin.
 */
static void uart_receive_byte(void *opaque, const uint8_t *buf, size_t len)
{
	struct uart_state *uart = opaque;
	size_t i;

	if (len == 0)
		return;

	os_mutex_lock(uart->lock);

	/*
	 * We currently emulate a 16450-style single-byte buffer (FIFO disabled).
	 * In a robust implementation, we would queue this into the FCR-managed
	 * FIFO ring buffer.
	 */
	for (i = 0; i < len; i++) {
		/*
		 * Overrun condition: previous character was not read by the CPU
		 * before the next one arrived. The old character is overwritten.
		 */
		if (uart->lsr & UART_LSR_DR) {
			uart->lsr |= UART_LSR_OE;
		}

		uart->rbr = buf[i];
		uart->lsr |= UART_LSR_DR;
	}

	/* Evaluate the interrupt lines after state mutation */
	uart_update_irq(uart);

	os_mutex_unlock(uart->lock);
}

static const struct vm_device_ops uart_16550a_ops = {
	.read = uart_read,
	.write = uart_write,
};

/**
 * uart_16550a_create - allocate, initialize and register the UART device
 *
 * This function acts as the constructor for the UART object. It allocates
 * the state machine, configures default hardware states, and attaches
 * the routing callbacks to the system bus.
 */
static int uart_16550a_create(struct machine *mach, void *platform_data)
{
	struct uart_state *uart;
	struct serial_platform_data *pdata = platform_data;
	int ret;

	(void)mach;

	/* Allocate state on heap to persist beyond the init sequence */
	uart = calloc(1, sizeof(*uart));
	if (!uart) {
		pr_err("Failed to allocate memory for UART state\n");
		return -ENOMEM;
	}

	uart->lock = os_mutex_create();
	uart->irq_pin = pdata->irq;
	uart->chr = pdata->chr;

	/* Bind the hardware reception pin to the backend data stream */
	chardev_set_receive_cb(uart->chr, uart_receive_byte, uart);

	uart->dev.name = "16550A";
	uart->dev.ops = &uart_16550a_ops;
	uart->dev.private_data = uart;

	/*
	 * Strict hardware reset states per Table 3.
	 */
	uart->ier = 0x00;
	uart->fcr = 0x00;
	uart->lcr = 0x00;
	uart->mcr = 0x00;
	uart->lsr = 0x60; /* 0110 0000 -> THRE and TEMT bits are high */
	/* The TX buffer is empty on boot, causing an initial interrupt condition */
	uart->thre_int_pending = true;
	uart_update_msr(uart);
	uart->msr &= ~UART_MSR_ANY_DELTA;

	/* Claim the 8 contiguous I/O ports on the motherboard bus */
	ret = bus_register_region(VM_BUS_SPACE_PIO, pdata->base_port, 8,
				  &uart->dev);
	if (ret < 0) {
		pr_err("Failed to register base_port PIO range, err: %d\n",
		       ret);
		os_mutex_destroy(uart->lock);
		free(uart);
		return ret;
	}

	pr_info("Successfully initialized at PIO base 0x%03lx\n",
		pdata->base_port);

	return 0;
}

static const struct vm_device_class uart_class = {
	.name = "uart-16550a",
	.create = uart_16550a_create,
};

static void __attribute__((constructor)) register_uart_16550a(void)
{
	vm_device_class_register(&uart_class);
}