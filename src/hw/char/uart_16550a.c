/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

#include <modvm/core/bus.h>
#include <modvm/hw/serial_reg.h>
#include <modvm/hw/serial.h>
#include <modvm/utils/compiler.h>
#include <modvm/utils/log.h>
#include <modvm/utils/bug.h>
#include <modvm/os/thread.h>

#undef pr_fmt
#define pr_fmt(fmt) "uart_16550a: " fmt

/**
 * struct uart_ctx - internal state machine for the 16550a uart
 * @dev: base virtual device structure for bus registration
 * @lock: mutex to serialize access from multiple executing processors
 * @irq: hardware interrupt line connection to the system board
 * @console: host character device handling the stream presentation
 * @dll: baud rate divisor register (low byte)
 * @dlm: baud rate divisor register (high byte)
 * @ier: ier register caching enabled logical interrupt sources
 * @fcr: fcr register caching queue depth configuration
 * @lcr: lcr register caching frame formatting and parity settings
 * @mcr: mcr register governing output pins and local loopback
 * @lsr: lsr register reflecting transmission and reception electrical states
 * @msr: msr register reflecting incoming modem control lines
 * @scr: scr register providing general purpose storage
 * @thr: emulated outgoing data latch
 * @rbr: emulated incoming data latch
 * @thre_int_pending: internal flip-flop for edge-triggered thre interrupts
 */
struct uart_ctx {
	struct vm_device dev;
	struct os_mutex *lock;
	struct vm_irq *irq;
	struct vm_chardev *console;

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

static inline bool uart_is_dlab_set(struct uart_ctx *ctx)
{
	return (ctx->lcr & UART_LCR_DLAB) != 0;
}

static inline bool uart_is_loopback(struct uart_ctx *ctx)
{
	/* According to 8.6.7, MCR bit 4 enables local loopback feature */
	return (ctx->mcr & UART_MCR_LOOP) != 0;
}

static uint8_t uart_get_iir(struct uart_ctx *ctx)
{
	/*
         * Bits 6 and 7 are set to 1 if FIFOs are enabled.
         * We initialize the base value here to prevent subsequent
         * bitwise OR operations from accidentally clobbering the state
         * if the logic tree grows more complex in the future.
         */
	uint8_t iir = (ctx->fcr & UART_FCR_ENABLE_FIFO) ? 0xc0 : 0x00;

	/* Priority 1: Receiver Line Status*/
	if ((ctx->ier & UART_IER_RLSI) &&
	    (ctx->lsr & UART_LSR_BRK_ERROR_BITS)) {
		iir |= 0x06;
		return iir;
	}

	/*
	 * Priority 2: Received Data Available.
	 * Triggered if Data Ready (DR) is set AND the interrupt is enabled.
	 */
	if ((ctx->ier & UART_IER_RDI) && (ctx->lsr & UART_LSR_DR)) {
		iir |= 0x04;
		return iir;
	}

	/* Priority 3: Transmitter Holding Register Empty */
	if ((ctx->ier & UART_IER_THRI) && ctx->thre_int_pending) {
		iir |= 0x02;
		return iir;
	}

	/* Priority 4: Modem Status */
	if ((ctx->ier & UART_IER_MSI) && (ctx->msr & UART_MSR_ANY_DELTA)) {
		iir |= 0x00;
		return iir;
	}

	/* No pending logical interrupts */
	iir |= UART_IIR_NO_INT;
	return iir;
}

static void uart_update_irq(struct uart_ctx *ctx)
{
	uint8_t iir = uart_get_iir(ctx);

	/*
         * Bit 0 of IIR is logic 0 when an interrupt is pending.
         * Otherwise, it is logic 1 (No interrupt pending).
         */
	int level = (iir & UART_IIR_NO_INT) ? 0 : 1;

	vm_irq_set_level(ctx->irq, level);
}

static void uart_update_msr(struct uart_ctx *ctx)
{
	uint8_t old_msr = ctx->msr;
	uint8_t new_msr = 0;
	uint8_t delta;

	if (!uart_is_loopback(ctx)) {
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
		if (ctx->mcr & UART_MCR_DTR)
			new_msr |= UART_MSR_DSR;
		if (ctx->mcr & UART_MCR_RTS)
			new_msr |= UART_MSR_CTS;
		if (ctx->mcr & UART_MCR_OUT1)
			new_msr |= UART_MSR_RI;
		if (ctx->mcr & UART_MCR_OUT2)
			new_msr |= UART_MSR_DCD;
	}

	delta = ((old_msr ^ new_msr) &
		 (UART_MSR_DCD | UART_MSR_DSR | UART_MSR_CTS)) >>
		4;

	/*
	 * Trailing Edge of Ring Indicator (TERI).
	 * Triggers when physical RI goes 0->1, which means MSR bit 6 goes 1->0
	 * due to the pin complement logic described in the datasheet.
	 */
	if ((old_msr & UART_MSR_RI) && !(new_msr & UART_MSR_RI))
		delta |= UART_MSR_TERI;

	ctx->msr = new_msr | (old_msr & UART_MSR_ANY_DELTA) | delta;
}

static uint8_t uart_read_reg(struct uart_ctx *ctx, uint16_t offset)
{
	uint8_t val = 0;

	switch (offset) {
	case UART_RX:
		if (uart_is_dlab_set(ctx))
			return ctx->dll;

		val = ctx->rbr;
		/*
		 * Reading the Receiver Buffer clears the Data Ready (DR) bit
		 * in the Line Status Register, per 8.6.3.
		 */
		ctx->lsr &= ~UART_LSR_DR;
		return val;

	case UART_IER:
		if (uart_is_dlab_set(ctx))
			return ctx->dlm;
		return ctx->ier;

	case UART_IIR:
		val = uart_get_iir(ctx);
		/*
		 * Reading IIR acknowledges the THRE interrupt per datasheet.
		 * Priority 3 mask is 0x02.
		 */
		if ((val & 0x0f) == 0x02)
			ctx->thre_int_pending = false;
		return val;

	case UART_LCR:
		return ctx->lcr;

	case UART_MCR:
		return ctx->mcr;

	case UART_LSR:
		/*
		 * Physical state: transmission is infinitely fast,
		 * so holding register and shift register are always empty.
		 */
		ctx->lsr |= (UART_LSR_THRE | UART_LSR_TEMT);
		val = ctx->lsr;
		ctx->lsr &= ~UART_LSR_BRK_ERROR_BITS;
		return val;

	case UART_MSR:
		val = ctx->msr;
		/* Datasheet 8.6.8: Delta is cleared after CPU reads MSR */
		ctx->msr &= ~UART_MSR_ANY_DELTA;
		return val;

	case UART_SCR:
		return ctx->scr;

	default:
		pr_warn_once("read from unknown offset: 0x%x\n", offset);
		return 0xff;
	}
}

static void uart_write_reg(struct uart_ctx *ctx, uint16_t offset, uint8_t val)
{
	uint8_t old_ier;

	switch (offset) {
	case UART_TX:
		if (uart_is_dlab_set(ctx)) {
			ctx->dll = val;
			return;
		}

		if (uart_is_loopback(ctx)) {
			/*
			 * In loopback mode, transmitted data is immediately
			 * received in the Receive Buffer Register, and Data Ready
			 * bit is set in LSR.
			 */
			if (ctx->lsr & UART_LSR_DR) {
				ctx->lsr |= UART_LSR_OE;
			} else {
				ctx->rbr = val;
				ctx->lsr |= UART_LSR_DR;
			}
		} else {
			/*
			 * Dispatch the payload to the attached host backend.
			 * The frontend hardware does not care how the backend processes it.
			 */
			if (ctx->console && ctx->console->ops->write)
				ctx->console->ops->write(ctx->console, &val, 1);
		}

		/**
		 * Emulated transmission is instantaneous. The transmit
                 * holding register (THR) and transmit shift register (TSR)
                 * remain empty at all times in this model.
                 * We trigger the THRE interrupt pending flag but do not
                 * clear the THRE status bit in the LSR, reflecting the
                 * immediate consumption of the data.
                 */
		ctx->thre_int_pending = true;
		/* uart->lsr &= ~UART_LSR_THRE; */
		break;

	case UART_IER: {
		if (uart_is_dlab_set(ctx)) {
			ctx->dlm = val;
			return;
		}
		/* 
		 * Datasheet 8.6.3: IER bits 4-7 are always logic 0
		 */
		old_ier = ctx->ier;
		ctx->ier = val & 0x0f;

		/*
		 * If the THRE interrupt was just enabled, and the buffer is
		 * currently empty (which it always is in our simulation),
		 * the interrupt must fire immediately to satisfy OS IRQ probing.
		 */
		if (!(old_ier & UART_IER_THRI) && (ctx->ier & UART_IER_THRI))
			ctx->thre_int_pending = true;
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
			ctx->fcr = val & UART_FCR_ENABLE_FIFO;
			ctx->lsr &= ~(UART_LSR_DR | UART_LSR_OE);
		} else {
			ctx->fcr = val &
				   ~(UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);
			if (val & UART_FCR_CLEAR_RCVR)
				ctx->lsr &= ~(UART_LSR_DR | UART_LSR_OE);
		}
		break;

	case UART_LCR:
		ctx->lcr = val;
		/* Emulate Break signal reflection in loopback mode */
		if (uart_is_loopback(ctx)) {
			if (ctx->lcr & UART_LCR_SBC)
				ctx->lsr |= UART_LSR_BI;
			else
				ctx->lsr &= ~UART_LSR_BI;
		}
		break;

	case UART_MCR:
		/*
		 * Datasheet 8.6.7: MCR bits 5-7 are always logic 0
		 */
		ctx->mcr = val & 0x1f;
		/* Changing MCR requires re-evaluating MSR wiring */
		uart_update_msr(ctx);
		break;

	case UART_LSR:
		/*
		 * Note in 8.6.3: "Writing to this register is not recommended."
		 * Real hardware ignores most writes here.
		 */
		break;

	case UART_SCR:
		ctx->scr = val;
		break;

	default:
		pr_warn_once("write to unknown offset: 0x%x\n", offset);
		break;
	}
}

static uint64_t uart_bus_read(struct vm_device *dev, uint64_t addr,
			      uint8_t size)
{
	struct uart_ctx *ctx = dev->priv;
	uint64_t ret = 0;
	uint8_t i;

	os_mutex_lock(ctx->lock);

	for (i = 0; i < size; i++) {
		uint8_t reg_val = uart_read_reg(ctx, addr + i);
		ret |= ((uint64_t)reg_val << (i * 8));
	}

	/* reading registers may clear pending interrupts */
	uart_update_irq(ctx);
	os_mutex_unlock(ctx->lock);

	return ret;
}

static void uart_bus_write(struct vm_device *dev, uint64_t addr, uint64_t val,
			   uint8_t size)
{
	struct uart_ctx *ctx = dev->priv;
	uint8_t i;

	os_mutex_lock(ctx->lock);

	for (i = 0; i < size; i++) {
		uint8_t byte_val = (uint8_t)((val >> (i * 8)) & 0xff);
		uart_write_reg(ctx, addr + i, byte_val);
	}

	/* writing registers may clear or trigger new interrupts */
	uart_update_irq(ctx);
	os_mutex_unlock(ctx->lock);
}

static void uart_rx_cb(void *data, const uint8_t *buf, size_t len)
{
	struct uart_ctx *ctx = data;
	size_t i;

	if (len == 0)
		return;

	os_mutex_lock(ctx->lock);

	/*
	 * we currently emulate a 16450-style single-byte buffer (fifo disabled).
	 * in a robust implementation, we would queue this into the fcr-managed
	 * fifo ring buffer.
	 */
	for (i = 0; i < len; i++) {
		/*
		 * overrun condition: previous character was not read by the cpu
		 * before the next one arrived. the old character is overwritten.
		 */
		if (ctx->lsr & UART_LSR_DR)
			ctx->lsr |= UART_LSR_OE;

		ctx->rbr = buf[i];
		ctx->lsr |= UART_LSR_DR;
	}

	/* evaluate the interrupt lines after state mutation */
	uart_update_irq(ctx);
	os_mutex_unlock(ctx->lock);
}

static const struct vm_device_ops uart_ops = {
	.read = uart_bus_read,
	.write = uart_bus_write,
};

/**
 * uart_instantiate - allocate and register the serial peripheral
 * @machine: the container representing the host topology
 * @pdata: immutable routing configuration
 *
 * return: 0 upon successful initialization, or a negative error code.
 */
static int uart_instantiate(struct vm_machine *machine, void *pdata)
{
	struct uart_ctx *ctx;
	struct serial_pdata *plat = pdata;
	int ret;

	(void)machine;

	/* allocate state on heap to persist beyond the init sequence */
	ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return -ENOMEM;

	ctx->lock = os_mutex_create();
	ctx->irq = plat->irq;
	ctx->console = plat->console;

	/* bind the hardware reception pin to the backend data stream */
	vm_chardev_set_rx_cb(ctx->console, uart_rx_cb, ctx);

	ctx->dev.name = "16550A";
	ctx->dev.ops = &uart_ops;
	ctx->dev.priv = ctx;

	/*
	 * strict hardware reset states per table 3.
	 */
	ctx->ier = 0x00;
	ctx->fcr = 0x00;
	ctx->lcr = 0x00;
	ctx->mcr = 0x00;
	ctx->lsr = 0x60; /* 0110 0000 -> THRE and TEMT bits are high */
	/* the TX buffer is empty on boot, causing an initial interrupt condition */
	ctx->thre_int_pending = true;
	uart_update_msr(ctx);
	ctx->msr &= ~UART_MSR_ANY_DELTA;

	/* Claim the 8 contiguous I/O ports on the motherboard bus */
	ret = vm_bus_register_region(VM_BUS_PIO, plat->base, 8, &ctx->dev);
	if (ret < 0) {
		pr_err("failed to register pio region: %d\n", ret);
		os_mutex_destroy(ctx->lock);
		free(ctx);
		return ret;
	}

	pr_info("initialized serial terminal at port 0x%03lx\n", plat->base);
	return 0;
}

static const struct vm_device_class uart_class = {
	.name = "uart-16550a",
	.instantiate = uart_instantiate,
};

static void __attribute__((constructor)) register_uart_class(void)
{
	vm_device_class_register(&uart_class);
}