/* SPDX-License-Identifier: GPL-2.0 */
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>

#include <modvm/core/bus.h>
#include <modvm/core/devm.h>
#include <modvm/hw/char/serial.h>
#include <modvm/utils/compiler.h>
#include <modvm/utils/bug.h>
#include <modvm/utils/log.h>
#include <modvm/utils/err.h>
#include <modvm/os/thread.h>

#include "serial_reg.h"

#undef pr_fmt
#define pr_fmt(fmt) "uart_16550a: " fmt

#define UART_FIFO_SIZE 16
#define UART_BACKLOG_SIZE 256
#define UART_BACKLOG_HIGH_WATERMARK 192
#define UART_BACKLOG_LOW_WATERMARK 64

/**
 * struct uart_16550a_ctx - internal state machine for the 16550a uart
 * @lock: mutex to serialize access from multiple executing processors
 * @irq: hardware interrupt line connection to the system board
 * @console: host character device handling the stream presentation
 * @event_loop: asynchronous I/O dispatcher reference
 * @reg_shift: byte shift for register spacing (0 for 8-bit PIO, 2 for 32-bit MMIO aligned)
 * @dll: baud rate divisor register (low byte)
 * @dlm: baud rate divisor register (high byte)
 * @ier: ier register caching enabled logical interrupt sources
 * @fcr: fcr register caching queue depth configuration
 * @lcr: lcr register caching frame formatting and parity settings
 * @mcr: mcr register governing output pins and local loopback
 * @lsr: lsr register reflecting transmission and reception electrical states
 * @msr: msr register reflecting incoming modem control lines
 * @scr: scr register providing general purpose storage
 * @rx_fifo: 16-byte receiver ring buffer
 * @rx_head: write index for rx_fifo
 * @rx_tail: read index for rx_fifo
 * @rx_cnt: current number of bytes in rx_fifo
 * @backlog_fifo: software buffer to handle host backend bursts
 * @backlog_head: write index for backlog_fifo
 * @backlog_tail: read index for backlog_fifo
 * @backlog_cnt: current number of bytes in backlog_fifo
 * @rx_paused: backpressure state indicating the host backend is throttled
 * @tx_fifo: 16-byte transmitter ring buffer
 * @tx_head: write index for tx_fifo
 * @tx_tail: read index for tx_fifo
 * @tx_cnt: current number of bytes in tx_fifo
 * @rx_trigger_level: cached interrupt trigger watermark based on FCR
 * @rx_timeout: FIFO Timeout interrupt pending state
 * @thre_int_pending: internal flip-flop for edge-triggered thre interrupts
 */
struct uart_16550a_ctx {
	struct os_mutex *lock;
	struct modvm_irq *irq;
	struct modvm_chardev *console;
	struct modvm_event_loop *event_loop;

	uint8_t reg_shift;

	uint8_t dll __guarded_by(lock);
	uint8_t dlm __guarded_by(lock);
	uint8_t ier __guarded_by(lock);
	uint8_t fcr __guarded_by(lock);
	uint8_t lcr __guarded_by(lock);
	uint8_t mcr __guarded_by(lock);
	uint8_t lsr __guarded_by(lock);
	uint8_t msr __guarded_by(lock);
	uint8_t scr __guarded_by(lock);

	/*
	 * PC16550D features integrated transmit and receive FIFOs.
	 * Both FIFOs have a maximum capacity of 16 bytes.
	 */
	uint8_t rx_fifo[UART_FIFO_SIZE] __guarded_by(lock);
	uint8_t rx_head __guarded_by(lock);
	uint8_t rx_tail __guarded_by(lock);
	uint8_t rx_cnt __guarded_by(lock);

	/* Software backlog buffer for flow control */
	uint8_t backlog_fifo[UART_BACKLOG_SIZE] __guarded_by(lock);
	uint16_t backlog_head __guarded_by(lock);
	uint16_t backlog_tail __guarded_by(lock);
	uint16_t backlog_cnt __guarded_by(lock);
	bool rx_paused __guarded_by(lock);

	uint8_t tx_fifo[UART_FIFO_SIZE] __guarded_by(lock);
	uint8_t tx_head __guarded_by(lock);
	uint8_t tx_tail __guarded_by(lock);
	uint8_t tx_cnt __guarded_by(lock);

	/*
	 * Trigger level for RCVR FIFO interrupt (1, 4, 8, or 14 bytes).
	 */
	uint8_t rx_trigger_level __guarded_by(lock);

	/*
	 * FIFO Timeout interrupt pending state.
	 */
	bool rx_timeout __guarded_by(lock);

	bool thre_int_pending __guarded_by(lock);
};

static inline bool uart_16550a_dlab_is_set(struct uart_16550a_ctx *ctx)
	__must_hold(ctx->lock)
{
	return (ctx->lcr & UART_LCR_DLAB) != 0;
}

static inline bool uart_16550a_loopback_is_enabled(struct uart_16550a_ctx *ctx)
	__must_hold(ctx->lock)
{
	/* According to 8.6.7, MCR bit 4 enables local loopback feature */
	return (ctx->mcr & UART_MCR_LOOP) != 0;
}

/**
 * uart_16550a_rx_fifo_clear - flush the receiver ring buffer
 * @ctx: the uart state machine context
 *
 * Resets the read/write indices and byte counter. According to the
 * hardware specification, this action must also clear the associated
 * line status bits.
 */
static inline void uart_16550a_rx_fifo_clear(struct uart_16550a_ctx *ctx)
	__must_hold(ctx->lock)
{
	ctx->rx_head = 0;
	ctx->rx_tail = 0;
	ctx->rx_cnt = 0;

	/*
	 * 8.6.4: Writing a 1 to FCR1 clears all bytes in the RCVR FIFO 
	 * and resets its counter logic to 0. 
	 */
	ctx->lsr &= ~(UART_LSR_DR | UART_LSR_OE);
}

/**
 * uart_16550a_tx_fifo_clear - flush the transmitter ring buffer
 * @ctx: the uart state machine context
 */
static inline void uart_16550a_tx_fifo_clear(struct uart_16550a_ctx *ctx)
	__must_hold(ctx->lock)
{
	ctx->tx_head = 0;
	ctx->tx_tail = 0;
	ctx->tx_cnt = 0;

	/*
	 * 8.6.4: Writing a 1 to FCR2 clears all bytes in the XMIT FIFO.
	 * When XMIT FIFO is empty, THRE and TEMT must reflect this state.
	 */
	ctx->lsr |= (UART_LSR_THRE | UART_LSR_TEMT);
	ctx->thre_int_pending = true;
}

/**
 * uart_16550a_rx_fifo_push - enqueue a received character into the ring buffer
 * @ctx: the uart state machine context
 * @c: the byte received from the host backend
 *
 * Datasheet 8.6.3: If the FIFO continues to fill beyond the trigger level,
 * an overrun error will occur only after the FIFO is full. The character
 * in the shift register is overwritten, but not transferred to the FIFO.
 */
static inline void uart_16550a_rx_fifo_push(struct uart_16550a_ctx *ctx,
					    uint8_t c) __must_hold(ctx->lock)
{
	if (unlikely(ctx->rx_cnt >= UART_FIFO_SIZE)) {
		ctx->lsr |= UART_LSR_OE;
		return;
	}

	ctx->rx_fifo[ctx->rx_head] = c;
	ctx->rx_head = (ctx->rx_head + 1) % UART_FIFO_SIZE;
	ctx->rx_cnt++;
	ctx->lsr |= UART_LSR_DR;
}

/**
 * uart_16550a_hw_fifo_refill - transfer bytes from backlog to hardware FIFO
 * @ctx: the uart state machine context
 *
 * Triggers backpressure release (resume_rx) if the backlog falls below
 * the low watermark.
 */
static inline void uart_16550a_hw_fifo_refill(struct uart_16550a_ctx *ctx)
	__must_hold(ctx->lock)
{
	bool pushed = false;

	while (ctx->backlog_cnt > 0 && ctx->rx_cnt < UART_FIFO_SIZE) {
		uart_16550a_rx_fifo_push(ctx,
					 ctx->backlog_fifo[ctx->backlog_tail]);
		ctx->backlog_tail = (ctx->backlog_tail + 1) % UART_BACKLOG_SIZE;
		ctx->backlog_cnt--;
		pushed = true;
	}

	if (unlikely(ctx->rx_paused &&
		     ctx->backlog_cnt < UART_BACKLOG_LOW_WATERMARK)) {
		modvm_chardev_resume_rx(ctx->console);
		ctx->rx_paused = false;
	}

	if (pushed) {
		if ((ctx->fcr & UART_FCR_ENABLE_FIFO) && ctx->rx_cnt > 0 &&
		    ctx->rx_cnt < ctx->rx_trigger_level)
			ctx->rx_timeout = true;
		else
			ctx->rx_timeout = false;
	}
}

/**
 * uart_16550a_rx_fifo_pop - dequeue a character from the ring buffer
 * @ctx: the uart state machine context
 *
 * Datasheet 8.4.1: Reading the RCVR FIFO clears the timeout interrupt.
 * Returns the dequeued byte, or 0 if empty.
 */
static inline uint8_t uart_16550a_rx_fifo_pop(struct uart_16550a_ctx *ctx)
	__must_hold(ctx->lock)
{
	uint8_t val;

	if (unlikely(ctx->rx_cnt == 0))
		return 0;

	val = ctx->rx_fifo[ctx->rx_tail];
	ctx->rx_tail = (ctx->rx_tail + 1) % UART_FIFO_SIZE;
	ctx->rx_cnt--;

	ctx->rx_timeout = false;

	if (ctx->rx_cnt == 0)
		ctx->lsr &= ~UART_LSR_DR;

	return val;
}

/**
 * uart_16550a_tx_flush - drain the transmitter ring buffer to the host backend
 * @ctx: the uart state machine context
 *
 * Flushes all pending bytes in the TX FIFO to the attached character device.
 * Upon completion, updates the Line Status Register to reflect an empty
 * shift register and triggers the THRE interrupt.
 */
static void uart_16550a_tx_flush(struct uart_16550a_ctx *ctx)
	__must_hold(ctx->lock)
{
	if (likely(ctx->tx_cnt > 0)) {
		if (likely(ctx->console && ctx->console->ops->write)) {
			uint8_t buf[UART_FIFO_SIZE];
			int i;

			for (i = 0; i < ctx->tx_cnt; i++) {
				buf[i] = ctx->tx_fifo[ctx->tx_tail];
				ctx->tx_tail =
					(ctx->tx_tail + 1) % UART_FIFO_SIZE;
			}

			ctx->console->ops->write(ctx->console, buf,
						 ctx->tx_cnt);
		}
		ctx->tx_cnt = 0;
	}

	/*
	 * Datasheet 8.6.3: TEMT and THRE are set to 1 when both the THR 
	 * and TSR are empty.
	 */
	ctx->lsr |= (UART_LSR_THRE | UART_LSR_TEMT);
	ctx->thre_int_pending = true;
}

static uint8_t uart_16550a_iir_get(struct uart_16550a_ctx *ctx)
	__must_hold(ctx->lock)
{
	/*
         * Bits 6 and 7 are set to 1 if FIFOs are enabled.
         * We initialize the base value here to prevent subsequent
         * bitwise OR operations from accidentally clobbering the state
         * if the logic tree grows more complex in the future.
         */
	uint8_t iir = (ctx->fcr & UART_FCR_ENABLE_FIFO) ? 0xc0 : 0x00;
	bool trigger_reached;

	/* Priority 1: Receiver Line Status (Errors) */
	if ((ctx->ier & UART_IER_RLSI) &&
	    (ctx->lsr & UART_LSR_BRK_ERROR_BITS)) {
		return iir | 0x06;
	}

	/*
	 * Priority 2: Received Data Available OR Character Timeout.
	 * Check if the FIFO byte count meets the programmed trigger level.
	 */
	trigger_reached = (ctx->fcr & UART_FCR_ENABLE_FIFO) ?
				  (ctx->rx_cnt >= ctx->rx_trigger_level) :
				  (ctx->rx_cnt > 0);
	if (ctx->ier & UART_IER_RDI) {
		if (trigger_reached)
			return iir | 0x04;
		else if (ctx->rx_timeout)
			return iir | 0x0C; /* Timeout indication */
	}

	/* Priority 3: Transmitter Holding Register Empty */
	if ((ctx->ier & UART_IER_THRI) && ctx->thre_int_pending) {
		return iir | 0x02;
	}

	/* Priority 4: Modem Status */
	if ((ctx->ier & UART_IER_MSI) && (ctx->msr & UART_MSR_ANY_DELTA)) {
		return iir | 0x00;
	}

	/* No pending logical interrupts */
	return iir | UART_IIR_NO_INT;
}

static void uart_16550a_irq_update(struct uart_16550a_ctx *ctx)
	__must_hold(ctx->lock)
{
	uint8_t iir = uart_16550a_iir_get(ctx);

	/*
         * Bit 0 of IIR is logic 0 when an interrupt is pending.
         * Otherwise, it is logic 1 (No interrupt pending).
         */
	int level = (iir & UART_IIR_NO_INT) ? 0 : 1;

	modvm_irq_set_level(ctx->irq, level);
}

static void uart_16550a_msr_update(struct uart_16550a_ctx *ctx)
	__must_hold(ctx->lock)
{
	uint8_t old_msr = ctx->msr;
	uint8_t new_msr = 0;
	uint8_t delta;

	if (!uart_16550a_loopback_is_enabled(ctx)) {
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

static uint8_t uart_16550a_reg_read(struct uart_16550a_ctx *ctx,
				    uint16_t offset) __must_hold(ctx->lock)
{
	uint8_t val = 0;

	switch (offset) {
	case UART_RX:
		if (unlikely(uart_16550a_dlab_is_set(ctx)))
			return ctx->dll;

		/*
		 * Datasheet 8.6.3: Reading the Receiver Buffer clears the DR bit
		 * implicitly via our uart_16550a_rx_fifo_pop() helper. It also clears 
		 * the timeout condition.
		 */
		return uart_16550a_rx_fifo_pop(ctx);

	case UART_IER:
		if (unlikely(uart_16550a_dlab_is_set(ctx)))
			return ctx->dlm;
		return ctx->ier;

	case UART_IIR:
		val = uart_16550a_iir_get(ctx);
		/*
		 * Datasheet 8.4.1 & Table 5: Reading the IIR Register (if source of 
		 * interrupt) clears the Transmitter Holding Register Empty interrupt. 
		 */
		if ((val & UART_IIR_ID) == 0x02)
			ctx->thre_int_pending = false;
		return val;

	case UART_LCR:
		return ctx->lcr;

	case UART_MCR:
		return ctx->mcr;

	case UART_LSR:
		val = ctx->lsr;
		/*
		 * Datasheet 8.6.3: In the FIFO mode LSR7 is set when there is at least 
		 * one parity error, framing error or break indication in the FIFO.
		 */
		val &= ~0x80;

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
		pr_warn_once("read from undefined peripheral offset: 0x%x\n",
			     offset);
		return 0xff;
	}
}

static void uart_16550a_reg_write(struct uart_16550a_ctx *ctx, uint16_t offset,
				  uint8_t val) __must_hold(ctx->lock)
{
	uint8_t old_ier;

	switch (offset) {
	case UART_TX:
		if (unlikely(uart_16550a_dlab_is_set(ctx))) {
			ctx->dll = val;
			return;
		}

		if (unlikely(uart_16550a_loopback_is_enabled(ctx))) {
			/*
			 * Datasheet 8.6.7: In loopback mode, data that is transmitted 
			 * is immediately received.
			 */
			uart_16550a_rx_fifo_push(ctx, val);
		} else {
			ctx->tx_fifo[ctx->tx_head] = val;
			ctx->tx_head = (ctx->tx_head + 1) % UART_FIFO_SIZE;
			ctx->tx_cnt++;

			ctx->lsr &= ~(UART_LSR_THRE | UART_LSR_TEMT);
			ctx->thre_int_pending = false;

			/*
			 * Don't do this:
			 *
			 * if (!(ctx->fcr & UART_FCR_ENABLE_FIFO) ||
			 *     ctx->tx_cnt >= UART_FIFO_SIZE) {
			 * 	uart_16550a_tx_flush(ctx);
			 * }
			 */
			uart_16550a_tx_flush(ctx);
		}
		break;

	case UART_IER: {
		if (unlikely(uart_16550a_dlab_is_set(ctx))) {
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
		if (!(old_ier & UART_IER_THRI) && (ctx->ier & UART_IER_THRI)) {
			if (ctx->tx_cnt == 0) {
				ctx->thre_int_pending = true;
			}
		}
		break;
	}

	case UART_FCR:
		/* Mask out reserved bits 4 and 5 (0xcf = 1100 1111) */
		val &= 0xcf;

		/*
		 * Datasheet 8.6.4: Resetting FCR0 will clear all bytes in both FIFOs.
		 * When changing from the FIFO Mode to the 16450 Mode and vice versa,
		 * data is automatically cleared from the FIFOs.
		 */
		if (!(val & UART_FCR_ENABLE_FIFO)) {
			ctx->fcr = 0;
			ctx->rx_trigger_level = 1;
			uart_16550a_rx_fifo_clear(ctx);
			uart_16550a_tx_fifo_clear(ctx);
			break;
		}

		/*
		 * Datasheet 8.6.4: Bits 1 and 2 are self-clearing. We store the
		 * other bits (like DMA mode and trigger levels) but strip these two.
		 */
		ctx->fcr = val & ~(UART_FCR_CLEAR_RCVR | UART_FCR_CLEAR_XMIT);

		if (val & UART_FCR_CLEAR_RCVR)
			uart_16550a_rx_fifo_clear(ctx);

		if (val & UART_FCR_CLEAR_XMIT)
			uart_16550a_tx_fifo_clear(ctx);

		/*
		 * Datasheet 8.6.4: FCR6 and FCR7 are used to set the trigger level
		 * for the RCVR FIFO interrupt.
		 */
		switch (val & 0xc0) {
		case 0x00:
			ctx->rx_trigger_level = 1;
			break;
		case 0x40:
			ctx->rx_trigger_level = 4;
			break;
		case 0x80:
			ctx->rx_trigger_level = 8;
			break;
		case 0xc0:
			ctx->rx_trigger_level = 14;
			break;
		}
		break;

	case UART_LCR:
		ctx->lcr = val;
		/* Emulate Break signal reflection in loopback mode */
		if (unlikely(uart_16550a_loopback_is_enabled(ctx))) {
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
		uart_16550a_msr_update(ctx);
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
		pr_warn_once("write to undefined peripheral offset: 0x%x\n",
			     offset);
		break;
	}
}

/**
 * uart_bus_read - generic read dispatcher resolving dynamic bus shift
 * @dev: the abstract device
 * @offset: raw byte offset from the bus
 * @size: requested access size
 *
 * Resolves the physical bus offset to the logical 16550A register index
 * based on the board-provided reg_shift. Ignores access sizes wider than
 * 8-bit, zero-extending the response to satisfy MMIO requirements.
 *
 * Return: register payload.
 */
static uint64_t uart_bus_read(struct modvm_device *dev, uint64_t offset,
			      uint8_t size)
{
	struct uart_16550a_ctx *ctx = dev->priv;
	uint64_t ret = 0;
	uint16_t reg_idx = offset >> ctx->reg_shift;

	(void)size;

	os_mutex_lock(ctx->lock);

	if (reg_idx == UART_RX)
		modvm_irq_set_level(ctx->irq, 0);

	ret = uart_16550a_reg_read(ctx, reg_idx);

	uart_16550a_irq_update(ctx);
	uart_16550a_hw_fifo_refill(ctx);
	uart_16550a_irq_update(ctx);

	os_mutex_unlock(ctx->lock);

	return ret;
}

/**
 * uart_bus_write - generic write dispatcher resolving dynamic bus shift
 * @dev: the abstract device
 * @offset: raw byte offset from the bus
 * @val: payload from vcpu
 * @size: requested access size
 *
 * Extracts the lowest 8 bits of the payload to write into the targeted
 * 16550A register, ignoring padding bytes from wide MMIO accesses.
 */
static void uart_bus_write(struct modvm_device *dev, uint64_t offset,
			   uint64_t val, uint8_t size)
{
	struct uart_16550a_ctx *ctx = dev->priv;
	uint16_t reg_idx = offset >> ctx->reg_shift;

	(void)size;

	os_mutex_lock(ctx->lock);

	uart_16550a_reg_write(ctx, reg_idx, (uint8_t)(val & 0xff));

	/* writing registers may clear or trigger new interrupts */
	uart_16550a_irq_update(ctx);
	os_mutex_unlock(ctx->lock);
}

static void uart_rx_cb(void *data, const uint8_t *buf, size_t len)
{
	struct uart_16550a_ctx *ctx = data;
	size_t i;

	if (unlikely(len == 0))
		return;

	os_mutex_lock(ctx->lock);

	for (i = 0; i < len; i++) {
		if (unlikely(ctx->backlog_cnt >= UART_BACKLOG_SIZE)) {
			ctx->lsr |= UART_LSR_OE;
			break;
		}
		ctx->backlog_fifo[ctx->backlog_head] = buf[i];
		ctx->backlog_head = (ctx->backlog_head + 1) % UART_BACKLOG_SIZE;
		ctx->backlog_cnt++;
	}

	if (!ctx->rx_paused &&
	    ctx->backlog_cnt >= UART_BACKLOG_HIGH_WATERMARK) {
		modvm_chardev_pause_rx(ctx->console);
		ctx->rx_paused = true;
	}

	uart_16550a_hw_fifo_refill(ctx);
	/* evaluate the interrupt lines after state mutation */
	uart_16550a_irq_update(ctx);
	os_mutex_unlock(ctx->lock);
}

static const struct modvm_device_ops uart_ops = {
	.read = uart_bus_read,
	.write = uart_bus_write,
};

static void uart_clear_rx_cb(struct modvm_device *dev)
{
	struct uart_16550a_ctx *ctx = dev->priv;

	if (ctx->console)
		modvm_chardev_set_rx_cb(ctx->console, ctx->event_loop, NULL,
					NULL);
}

/**
 * uart_instantiate - dynamically wire the UART to the requested bus
 * @dev: the abstract device object
 * @pdata: immutable routing configuration including target bus type
 *
 * Return: 0 upon successful initialization, or a negative error code.
 */
static int uart_instantiate(struct modvm_device *dev, void *pdata)
{
	struct uart_16550a_ctx *ctx;
	struct modvm_serial_pdata *plat = pdata;
	uint64_t region_size;
	int ret;

	if (WARN_ON(!plat || !plat->irq))
		return -EINVAL;

	ctx = modvm_devm_zalloc(dev, sizeof(*ctx));
	if (!ctx)
		return -ENOMEM;

	dev->ops = &uart_ops;
	dev->priv = ctx;

	ctx->reg_shift = plat->reg_shift;

	ctx->lock = os_mutex_create();
	if (IS_ERR(ctx->lock))
		return PTR_ERR(ctx->lock);

	ret = modvm_devm_add_action(dev, os_mutex_destroy, ctx->lock);
	if (ret < 0) {
		os_mutex_destroy(ctx->lock);
		return ret;
	}

	ctx->irq = plat->irq;
	ctx->console = plat->console;
	ctx->event_loop = plat->event_loop;

	/* bind the hardware reception pin to the backend data stream */
	modvm_chardev_set_rx_cb(ctx->console, ctx->event_loop, uart_rx_cb, ctx);

	ret = modvm_devm_add_action(dev, uart_clear_rx_cb, dev);
	if (ret < 0)
		return ret;

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
	uart_16550a_msr_update(ctx);
	ctx->msr &= ~UART_MSR_ANY_DELTA;

	/* Dynamically calculate region bounds based on register shift */
	region_size = 8ULL << ctx->reg_shift;

	ret = modvm_bus_register_region(plat->bus_type, plat->base, region_size,
					dev);
	if (ret < 0)
		return ret;

	pr_info("initialized serial terminal at %s 0x%08lx (shift: %u)\n",
		plat->bus_type == MODVM_BUS_MMIO ? "mmio" : "pio", plat->base,
		ctx->reg_shift);
	return 0;
}

static const struct modvm_device_class uart_class = {
	.name = "uart-16550a",
	.instantiate = uart_instantiate,
};

static void __attribute__((constructor)) register_uart_class(void)
{
	modvm_device_class_register(&uart_class);
}