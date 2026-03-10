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
 * struct uart_state - Internal state machine for the 16550A UART
 * @device: Base virtual device structure for bus registration
 * @hardware_lock: Mutex to serialize access from multiple executing processors
 * @interrupt_pin: Hardware interrupt line connection to the system board
 * @console_backend: Host character device handling the stream presentation
 * @divisor_latch_low: Baud rate divisor register (low byte)
 * @divisor_latch_high: Baud rate divisor register (high byte)
 * @interrupt_enable: IER register caching enabled logical interrupt sources
 * @fifo_control: FCR register caching queue depth configuration
 * @line_control: LCR register caching frame formatting and parity settings
 * @modem_control: MCR register governing output pins and local loopback
 * @line_status: LSR register reflecting transmission and reception electrical states
 * @modem_status: MSR register reflecting incoming modem control lines
 * @scratchpad: SCR register providing general purpose storage
 * @transmit_holding_buffer: Emulated outgoing data latch
 * @receive_buffer: Emulated incoming data latch
 * @transmit_interrupt_pending: Internal flip-flop for edge-triggered THRE interrupts
 */
struct uart_state {
	struct vm_device device;
	struct os_mutex *hardware_lock;
	struct vm_interrupt_line *interrupt_pin;
	struct vm_character_device *console_backend;

	uint8_t divisor_latch_low;
	uint8_t divisor_latch_high;
	uint8_t interrupt_enable;
	uint8_t fifo_control;
	uint8_t line_control;
	uint8_t modem_control;
	uint8_t line_status;
	uint8_t modem_status;
	uint8_t scratchpad;

	uint8_t transmit_holding_buffer;
	uint8_t receive_buffer;

	bool transmit_interrupt_pending;
};

/**
 * uart_is_dlab_set - check if the Divisor Latch Access Bit is enabled
 * @uart: pointer to the uart state machine
 */
static inline bool uart_is_dlab_enabled(struct uart_state *uart)
{
	return (uart->line_control & UART_LCR_DLAB) != 0;
}

static inline bool uart_is_loopback_enabled(struct uart_state *uart)
{
	/* According to 8.6.7, MCR bit 4 enables local loopback feature */
	return (uart->modem_control & UART_MCR_LOOP) != 0;
}

static uint8_t uart_synthesize_interrupt_identity(struct uart_state *uart)
{
	/*
         * Bits 6 and 7 are set to 1 if FIFOs are enabled.
         * We initialize the base value here to prevent subsequent
         * bitwise OR operations from accidentally clobbering the state
         * if the logic tree grows more complex in the future.
         */
	uint8_t identity_register =
		(uart->fifo_control & UART_FCR_ENABLE_FIFO) ? 0xc0 : 0x00;

	/* Priority 1: Receiver Line Status*/
	if ((uart->interrupt_enable & UART_IER_RLSI) &&
	    (uart->line_status & UART_LSR_BRK_ERROR_BITS)) {
		identity_register |= 0x06;
		return identity_register;
	}

	/*
	 * Priority 2: Received Data Available.
	 * Triggered if Data Ready (DR) is set AND the interrupt is enabled.
	 */
	if ((uart->interrupt_enable & UART_IER_RDI) &&
	    (uart->line_status & UART_LSR_DR)) {
		identity_register |= 0x04;
		return identity_register;
	}

	/* Priority 3: Transmitter Holding Register Empty */
	if ((uart->interrupt_enable & UART_IER_THRI) &&
	    uart->transmit_interrupt_pending) {
		identity_register |= 0x02;
		return identity_register;
	}

	/* Priority 4: Modem Status */
	if ((uart->interrupt_enable & UART_IER_MSI) &&
	    (uart->modem_status & UART_MSR_ANY_DELTA)) {
		identity_register |= 0x00;
		return identity_register;
	}

	/* No pending logical interrupts */
	identity_register |= UART_IIR_NO_INT;

	return identity_register;
}
static void uart_evaluate_interrupt_pin(struct uart_state *uart)
{
	uint8_t identity_register = uart_synthesize_interrupt_identity(uart);

	/*
         * Bit 0 of IIR is logic 0 when an interrupt is pending.
         * Otherwise, it is logic 1 (No interrupt pending).
         */
	int electrical_level = (identity_register & UART_IIR_NO_INT) ? 0 : 1;

	vm_interrupt_line_set_level(uart->interrupt_pin, electrical_level);
}

static void uart_synchronize_modem_status(struct uart_state *uart)
{
	uint8_t previous_status = uart->modem_status;
	uint8_t current_status = 0;

	if (!uart_is_loopback_enabled(uart)) {
		/*
		 * In normal mode, MSR is driven by external hardware lines.
		 * We simulate unconnected lines where Carrier Detect, Data Set Ready,
		 * and Clear To Send are asserted (typical null-modem behavior).
		 */
		current_status = UART_MSR_DCD | UART_MSR_DSR | UART_MSR_CTS;
	} else {
		/*
		 * Loopback wiring mapping per Datasheet 8.6.7:
		 * MCR Bit 0 (DTR)  -> MSR Bit 5 (DSR)
		 * MCR Bit 1 (RTS)  -> MSR Bit 4 (CTS)
		 * MCR Bit 2 (OUT1) -> MSR Bit 6 (RI)
		 * MCR Bit 3 (OUT2) -> MSR Bit 7 (DCD)
		 */
		if (uart->modem_control & UART_MCR_DTR)
			current_status |= UART_MSR_DSR;
		if (uart->modem_control & UART_MCR_RTS)
			current_status |= UART_MSR_CTS;
		if (uart->modem_control & UART_MCR_OUT1)
			current_status |= UART_MSR_RI;
		if (uart->modem_control & UART_MCR_OUT2)
			current_status |= UART_MSR_DCD;
	}

	uint8_t toggled_bits = ((previous_status ^ current_status) &
				(UART_MSR_DCD | UART_MSR_DSR | UART_MSR_CTS)) >>
			       4;

	/*
	 * Trailing Edge of Ring Indicator (TERI).
	 * Triggers when physical RI goes 0->1, which means MSR bit 6 goes 1->0
	 * due to the pin complement logic described in the datasheet.
	 */
	if ((previous_status & UART_MSR_RI) &&
	    !(current_status & UART_MSR_RI)) {
		toggled_bits |= UART_MSR_TERI;
	}

	uart->modem_status = current_status |
			     (previous_status & UART_MSR_ANY_DELTA) |
			     toggled_bits;
}

static uint8_t uart_read_hardware_register(struct uart_state *uart,
					   uint16_t port_offset)
{
	uint8_t returned_value = 0;

	switch (port_offset) {
	case UART_RX:
		if (uart_is_dlab_enabled(uart))
			return uart->divisor_latch_low;

		returned_value = uart->receive_buffer;
		/*
		 * Reading the Receiver Buffer clears the Data Ready (DR) bit
		 * in the Line Status Register, per 8.6.3.
		 */
		uart->line_status &= ~UART_LSR_DR;
		return returned_value;

	case UART_IER:
		if (uart_is_dlab_enabled(uart))
			return uart->divisor_latch_high;
		return uart->interrupt_enable;

	case UART_IIR:
		returned_value = uart_synthesize_interrupt_identity(uart);
		/*
		 * Reading IIR acknowledges the THRE interrupt per datasheet.
		 * Priority 3 mask is 0x02.
		 */
		if ((returned_value & 0x0f) == 0x02)
			uart->transmit_interrupt_pending = false;
		return returned_value;

	case UART_LCR:
		return uart->line_control;

	case UART_MCR:
		return uart->modem_control;

	case UART_LSR:
		/*
		 * Physical state: transmission is infinitely fast,
		 * so holding register and shift register are always empty.
		 */
		uart->line_status |= (UART_LSR_THRE | UART_LSR_TEMT);
		returned_value = uart->line_status;
		uart->line_status &= ~UART_LSR_BRK_ERROR_BITS;
		return returned_value;

	case UART_MSR:
		returned_value = uart->modem_status;
		/* Datasheet 8.6.8: Delta is cleared after CPU reads MSR */
		uart->modem_status &= ~UART_MSR_ANY_DELTA;
		return returned_value;

	case UART_SCR:
		return uart->scratchpad;

	default:
		pr_warn_once("read from unknown uart offset: 0x%x\n",
			     port_offset);
		return 0xff;
	}
}

static void uart_write_hardware_register(struct uart_state *uart,
					 uint16_t port_offset, uint8_t payload)
{
	switch (port_offset) {
	case UART_TX:
		if (uart_is_dlab_enabled(uart)) {
			uart->divisor_latch_low = payload;
			return;
		}

		if (uart_is_loopback_enabled(uart)) {
			/*
			 * In loopback mode, transmitted data is immediately
			 * received in the Receive Buffer Register, and Data Ready
			 * bit is set in LSR.
			 */
			if (uart->line_status & UART_LSR_DR) {
				uart->line_status |= UART_LSR_OE;
			} else {
				uart->receive_buffer = payload;
				uart->line_status |= UART_LSR_DR;
			}
		} else {
			/*
			 * Dispatch the payload to the attached host backend.
			 * The frontend hardware does not care how the backend processes it.
			 */
			if (uart->console_backend &&
			    uart->console_backend->operations->write)
				uart->console_backend->operations->write(
					uart->console_backend, &payload, 1);
		}

		/**
		 * Emulated transmission is instantaneous. The transmit
                 * holding register (THR) and transmit shift register (TSR)
                 * remain empty at all times in this model.
                 * We trigger the THRE interrupt pending flag but do not
                 * clear the THRE status bit in the LSR, reflecting the
                 * immediate consumption of the data.
                 */
		uart->transmit_interrupt_pending = true;
		/* uart->lsr &= ~UART_LSR_THRE; */
		break;

	case UART_IER: {
		if (uart_is_dlab_enabled(uart)) {
			uart->divisor_latch_high = payload;
			return;
		}
		/* 
		 * Datasheet 8.6.3: IER bits 4-7 are always logic 0
		 */
		uint8_t previous_enable = uart->interrupt_enable;
		uart->interrupt_enable = payload & 0x0f;

		/*
		 * If the THRE interrupt was just enabled, and the buffer is
		 * currently empty (which it always is in our simulation),
		 * the interrupt must fire immediately to satisfy OS IRQ probing.
		 */
		if (!(previous_enable & UART_IER_THRI) &&
		    (uart->interrupt_enable & UART_IER_THRI)) {
			uart->transmit_interrupt_pending = true;
		}
		break;
	}

	case UART_FCR:
		/* Mask out reserved bits 4 and 5 (0xcf = 1100 1111) */
		payload &= 0xcf;

		/*
		 * Datasheet 8.6.4:
		 * 1. Writing 0 to bit 0 (ENABLE_FIFO) will disable other bits
		 * 2. Writing 1 to bit 1 (CLEAR_RCVR) or bit 2 (CLEAR_XMIT) clears themselves
		 */
		if (!(payload & UART_FCR_ENABLE_FIFO)) {
			uart->fifo_control = payload & UART_FCR_ENABLE_FIFO;
			uart->line_status &= ~(UART_LSR_DR | UART_LSR_OE);
		} else {
			uart->fifo_control = payload & ~(UART_FCR_CLEAR_RCVR |
							 UART_FCR_CLEAR_XMIT);
			if (payload & UART_FCR_CLEAR_RCVR)
				uart->line_status &=
					~(UART_LSR_DR | UART_LSR_OE);
		}
		break;

	case UART_LCR:
		uart->line_control = payload;
		/* Emulate Break signal reflection in loopback mode */
		if (uart_is_loopback_enabled(uart)) {
			if (uart->line_control & UART_LCR_SBC)
				uart->line_status |= UART_LSR_BI;
			else
				uart->line_status &= ~UART_LSR_BI;
		}
		break;

	case UART_MCR:
		/*
		 * Datasheet 8.6.7: MCR bits 5-7 are always logic 0
		 */
		uart->modem_control = payload & 0x1f;
		/* Changing MCR requires re-evaluating MSR wiring */
		uart_synchronize_modem_status(uart);
		break;

	case UART_LSR:
		/*
		 * Note in 8.6.3: "Writing to this register is not recommended."
		 * Real hardware ignores most writes here.
		 */
		break;

	case UART_SCR:
		uart->scratchpad = payload;
		break;

	default:
		pr_warn_once("write to unknown uart offset: 0x%x\n",
			     port_offset);
		break;
	}
}

static uint64_t uart_bus_read(struct vm_device *device, uint64_t bus_offset,
			      uint8_t access_size)
{
	struct uart_state *uart = device->private_data;
	uint64_t aggregated_result = 0;
	uint8_t byte_index;

	os_mutex_lock(uart->hardware_lock);

	for (byte_index = 0; byte_index < access_size; byte_index++) {
		uint8_t register_value = uart_read_hardware_register(
			uart, bus_offset + byte_index);
		aggregated_result |=
			((uint64_t)register_value << (byte_index * 8));
	}

	/* reading registers may clear pending interrupts */
	uart_evaluate_interrupt_pin(uart);
	os_mutex_unlock(uart->hardware_lock);

	return aggregated_result;
}

static void uart_bus_write(struct vm_device *device, uint64_t bus_offset,
			   uint64_t payload, uint8_t access_size)
{
	struct uart_state *uart = device->private_data;
	uint8_t byte_index;

	os_mutex_lock(uart->hardware_lock);

	for (byte_index = 0; byte_index < access_size; byte_index++) {
		uint8_t byte_slice =
			(uint8_t)((payload >> (byte_index * 8)) & 0xff);
		uart_write_hardware_register(uart, bus_offset + byte_index,
					     byte_slice);
	}

	/* writing registers may clear or trigger new interrupts */
	uart_evaluate_interrupt_pin(uart);
	os_mutex_unlock(uart->hardware_lock);
}

static void uart_handle_backend_data(void *context_data,
				     const uint8_t *stream_buffer,
				     size_t length)
{
	struct uart_state *uart = context_data;
	size_t data_index;

	if (length == 0)
		return;

	os_mutex_lock(uart->hardware_lock);

	/*
	 * we currently emulate a 16450-style single-byte buffer (fifo disabled).
	 * in a robust implementation, we would queue this into the fcr-managed
	 * fifo ring buffer.
	 */
	for (data_index = 0; data_index < length; data_index++) {
		/*
		 * overrun condition: previous character was not read by the cpu
		 * before the next one arrived. the old character is overwritten.
		 */
		if (uart->line_status & UART_LSR_DR) {
			uart->line_status |= UART_LSR_OE;
		}

		uart->receive_buffer = stream_buffer[data_index];
		uart->line_status |= UART_LSR_DR;
	}

	/* evaluate the interrupt lines after state mutation */
	uart_evaluate_interrupt_pin(uart);
	os_mutex_unlock(uart->hardware_lock);
}

static const struct vm_device_operations uart_16550a_operations = {
	.read = uart_bus_read,
	.write = uart_bus_write,
};

/**
 * uart_16550a_instantiate - allocate and register the serial peripheral
 * @machine: the container representing the host topology
 * @platform_data: immutable routing configuration
 *
 * return: 0 upon successful initialization, or a negative error code.
 */
static int uart_16550a_instantiate(struct vm_machine *machine,
				   void *platform_data)
{
	struct uart_state *uart;
	struct vm_serial_platform_data *config = platform_data;
	int return_code;

	(void)machine;

	/* allocate state on heap to persist beyond the init sequence */
	uart = calloc(1, sizeof(*uart));
	if (!uart) {
		pr_err("failed to allocate memory for serial state\n");
		return -ENOMEM;
	}

	uart->hardware_lock = os_mutex_create();
	uart->interrupt_pin = config->interrupt_line;
	uart->console_backend = config->console_backend;

	/* bind the hardware reception pin to the backend data stream */
	vm_character_device_set_receive_callback(
		uart->console_backend, uart_handle_backend_data, uart);

	uart->device.name = "16550A";
	uart->device.operations = &uart_16550a_operations;
	uart->device.private_data = uart;

	/*
	 * strict hardware reset states per table 3.
	 */
	uart->interrupt_enable = 0x00;
	uart->fifo_control = 0x00;
	uart->line_control = 0x00;
	uart->modem_control = 0x00;
	uart->line_status = 0x60; /* 0110 0000 -> THRE and TEMT bits are high */
	/* the TX buffer is empty on boot, causing an initial interrupt condition */
	uart->transmit_interrupt_pending = true;
	uart_synchronize_modem_status(uart);
	uart->modem_status &= ~UART_MSR_ANY_DELTA;

	/* Claim the 8 contiguous I/O ports on the motherboard bus */
	return_code = vm_bus_register_region(VM_BUS_SPACE_PORT_IO,
					     config->base_port_address, 8,
					     &uart->device);
	if (return_code < 0) {
		pr_err("failed to register serial I/O space, error: %d\n",
		       return_code);
		os_mutex_destroy(uart->hardware_lock);
		free(uart);
		return return_code;
	}

	pr_info("successfully initialized serial terminal at port 0x%03lx\n",
		config->base_port_address);

	return 0;
}

static const struct vm_device_class uart_class = {
	.name = "uart-16550a",
	.instantiate = uart_16550a_instantiate,
};

static void __attribute__((constructor)) register_uart_class(void)
{
	vm_device_class_register(&uart_class);
}