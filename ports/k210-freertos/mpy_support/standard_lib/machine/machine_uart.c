/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "py/nlr.h"
#include "py/obj.h"
#include "py/binary.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "py/parsenum.h"
#include "py/formatfloat.h"
#include "py/runtime.h"
#include "lib/utils/interrupt_char.h"

#include "mpconfigboard.h"
#include "modmachine.h"
#include "uart.h"
#include "uarths.h"
#include "syslog.h"
#include "plic.h"

#define CHAR_NONE 256

#define Maix_DEBUG 0
#if Maix_DEBUG==1
#define debug_print(x,arg...) printf("[MaixPy]"x,##arg)
#else 
#define debug_print(x,arg...) 
#endif

#define Maix_KDEBUG 0
#if Maix_KDEBUG==1
#define debug_printk(x,arg...) printk("[MaixPy]"x,##arg)
#else 
#define debug_printk(x,arg...) 
#endif



STATIC const char *_parity_name[] = {"None", "1", "0"};


//QueueHandle_t UART_QUEUE[UART_DEVICE_MAX] = {};

/******************************************************************************/
// MicroPython bindings for UART

void DISABLE_RX_INT(machine_uart_obj_t *self)
{
	uint8_t data;
	self->rx_int_flag = 0;
	//printf("[MaixPy] %s | befor\n",__func__);
	uart_irq_unregister(self->uart_num, UART_RECEIVE);
	//printf("[MaixPy] %s | after\n",__func__);
}
//extern uarths_context_t g_uarths_context;
void DISABLE_HSRX_INT(machine_uart_obj_t *self)
{
	self->rx_int_flag = 0;
	uint8_t data;
    //g_uarths_context.callback = NULL;
    //g_uarths_context.ctx = NULL;
	plic_irq_disable(IRQN_UARTHS_INTERRUPT);
    plic_irq_unregister(IRQN_UARTHS_INTERRUPT);

}
mp_uint_t uart_rx_any(machine_uart_obj_t *self) 
{
	int buffer_bytes = self->read_buf_head - self->read_buf_tail;
	if (buffer_bytes < 0)
	{
		return buffer_bytes + self->read_buf_len ;
	} 
	else if (buffer_bytes > 0)
	{
		return buffer_bytes ;
	} 
	else 
	{
		//__HAL_UART_GET_FLAG(&self->uart, UART_FLAG_RXNE) != RESET
		return 0;
	}
}

int uart_rx_irq(void *ctx)
{
    machine_uart_obj_t *self = ctx;
	uint8_t data = 0;
    if (self == NULL) {
        return 0;
    }
	if (self->read_buf_len != 0) {
		if(self->attached_to_repl)
		{
			uint16_t next_head = (self->read_buf_head + 1) % self->read_buf_len;
			// only read data if room in buf
			if (next_head != self->read_buf_tail) {
				int ret = 0;
				if(MICROPY_UARTHS_DEVICE == self->uart_num)
					ret = uarths_receive_data(&data,1);
				else if(UART_DEVICE_MAX > self->uart_num)
					ret = uart_receive_data(self->uart_num,&data , 1);
				// can not receive any data ,return 
				if(0 == ret)
					return 0;
				self->read_buf[self->read_buf_head] = data;
				self->read_buf_head = next_head;
				self->data_len++;
				// Handle interrupt coming in on a UART REPL
				if (data == mp_interrupt_char) {
					if (MP_STATE_VM(mp_pending_exception) == MP_OBJ_NULL) {
						mp_keyboard_interrupt();
					} else {
						MP_STATE_VM(mp_pending_exception) = MP_OBJ_NULL;
						//pendsv_object = &MP_STATE_VM(mp_kbd_exception);
					}
				}

			}
			else {
				// No room: leave char in buf, disable interrupt,open it util rx char
				if(MICROPY_UARTHS_DEVICE == self->uart_num)
					uarths_receive_data(&data,1);
				else if(UART_DEVICE_MAX > self->uart_num)
					uart_receive_data(self->uart_num,&data , 1);
			}
			return 0;
		}
		else
		{
			uint16_t rx_ret = 0;
			uint16_t next_head = (self->read_buf_head + 1) % self->read_buf_len;
			while (next_head != self->read_buf_tail)
			{
				if(MICROPY_UARTHS_DEVICE == self->uart_num)
					rx_ret = uarths_receive_data(&self->read_buf[self->read_buf_head],1);
				else if(UART_DEVICE_MAX > self->uart_num)
					rx_ret = uart_receive_data(self->uart_num,&self->read_buf[self->read_buf_head],1);
				if(0 == rx_ret)
					break;
				self->read_buf_head = next_head;
				self->data_len++;
				next_head = (self->read_buf_head + 1) % self->read_buf_len;
			}
			if(next_head == self->read_buf_tail)
			{
				if(MICROPY_UARTHS_DEVICE == self->uart_num)
					uarths_receive_data(&data,1);
				else if(UART_DEVICE_MAX > self->uart_num)
					uart_receive_data(self->uart_num,&data,1);
			
			}
			return 0;
		}
	}

}

void ENABLE_RX_INT(machine_uart_obj_t *self)
{
	self->rx_int_flag = 1;
	//printf("[MaixPy] %s | befor\n",__func__);
	uart_irq_register(self->uart_num, UART_RECEIVE, uart_rx_irq, self, 2);
	//printf("[MaixPy] %s | after\n",__func__);
}

void ENABLE_HSRX_INT(machine_uart_obj_t *self)
{
	self->rx_int_flag = 1;
	uarths_set_irq(UARTHS_RECEIVE,uart_rx_irq,self,2);
}


bool uart_rx_wait(machine_uart_obj_t *self, uint32_t timeout) 
{
    uint32_t start = mp_hal_ticks_ms();
	debug_print("uart_rx_wait | read_buf_head = %d\n",self->read_buf_head);
	debug_print("uart_rx_wait | read_buf_tail = %d\n",self->read_buf_tail);
    for (;;) {
        if (self->read_buf_tail != self->read_buf_head) {
            return true; // have at least 1 char ready for reading
        }
        if (mp_hal_ticks_ms() - start >= timeout) {
            return false; // timeout
        }
    }
}


// assumes there is a character available
int uart_rx_char(machine_uart_obj_t *self) 
{
    if (self->read_buf_tail != self->read_buf_head) {
        uint8_t data;
        data = self->read_buf[self->read_buf_tail];
        self->read_buf_tail = (self->read_buf_tail + 1) % self->read_buf_len;
		self->data_len--;
        if (self->rx_int_flag == 0) {
            //re-enable IRQ now we have room in buffer
      		if(MICROPY_UARTHS_DEVICE == self->uart_num)
				ENABLE_HSRX_INT(self);	
			else if(UART_DEVICE_MAX > self->uart_num)
				ENABLE_RX_INT(self);
        }
        return data;
    }
	return CHAR_NONE;
}

int uart_rx_data(machine_uart_obj_t *self,uint8_t* buf_in,uint32_t size) 
{
	uint16_t data_num = 0;
	uint8_t* buf = buf_in;
    while(self->read_buf_tail != self->read_buf_head && size > 0) 
	{
        *buf = self->read_buf[self->read_buf_tail];
        self->read_buf_tail = (self->read_buf_tail + 1) % self->read_buf_len;
		self->data_len--;
		buf++;
		data_num++;
		size--;

    }
	return data_num;
}

STATIC bool uart_tx_wait(machine_uart_obj_t *self, uint32_t timeout) 
{
	//TODO add time out function for tx
	//uint32_t start = mp_hal_ticks_ms();
	return true;
}

STATIC size_t uart_tx_data(machine_uart_obj_t *self, const void *src_data, size_t size, int *errcode) 
{
    if (size == 0) {
        *errcode = 0;
        return 0;
    }

    uint32_t timeout;
	//K210 does not have cts function API at present
	//TODO:
	/*
    if (Determine whether to use CTS) {
        // CTS can hold off transmission for an arbitrarily long time. Apply
        // the overall timeout rather than the character timeout.
        timeout = self->timeout;
    } 
    */
    timeout = 2 * self->timeout_char;
    const uint8_t *src = (uint8_t*)src_data;
    size_t num_tx = 0;
	size_t cal = 0;	
	if(self->attached_to_repl)
	{
	    while (num_tx < size) {
			/*
	        if (Determine whether to send data(timeout)) {
	            *errcode = MP_ETIMEDOUT;
	            return num_tx;
	        }
	        */	        
	        uint8_t data;
	        data = *src++;
			if(MICROPY_UARTHS_DEVICE == self->uart_num)
				cal = uarths_send_data(&data,1);
			else if(UART_DEVICE_MAX > self->uart_num)
				cal= uart_send_data(self->uart_num, &data,1);		
	        num_tx = num_tx + cal;
	    }
	}
	else
	{
		while (num_tx < size) {        
			if(MICROPY_UARTHS_DEVICE == self->uart_num)
				cal = uarths_send_data(src,size);
			else if(UART_DEVICE_MAX > self->uart_num)
				cal= uart_send_data(self->uart_num, src,size);	
			src = src + cal;
 	        num_tx = num_tx + cal;
	    }
	}
    // wait for the UART frame to complete
    /*
    if (Determine whether the transmission is completed(timeout)) {
        *errcode = MP_ETIMEDOUT;
        return num_tx;
    }
	*/
    *errcode = 0;
    return num_tx;
}

void uart_tx_strn(machine_uart_obj_t *uart_obj, const char *str, uint len) {
    int errcode;
    uart_tx_data(uart_obj, str, len, &errcode);
}


void uart_attach_to_repl(machine_uart_obj_t *self, bool attached) {
    self->attached_to_repl = attached;
}

STATIC void machine_uart_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    //uart_get_baudrate(self->uart_num, &baudrate);
    mp_printf(print, "[MAIXPY]UART%d:( baudrate=%u, bits=%u, parity=%s, stop=%u)",
        self->uart_num,self->baudrate, self->bitwidth, _parity_name[self->parity],
        self->stop);
}

STATIC void machine_uart_init_helper(machine_uart_obj_t *self, size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args) {
    enum { ARG_baudrate, ARG_bitwidth, ARG_parity, ARG_stop ,ARG_timeout,ARG_timeout_char,ARG_read_buf_len};
    static const mp_arg_t allowed_args[] = {
        { MP_QSTR_baudrate, MP_ARG_INT, {.u_int = 115200} },
        { MP_QSTR_bits, MP_ARG_INT, {.u_int = 8} },
        { MP_QSTR_parity, MP_ARG_INT, {.u_int = UART_PARITY_NONE} },
        { MP_QSTR_stop, MP_ARG_INT, {.u_int = UART_STOP_1} },
        { MP_QSTR_timeout, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 1000} },
        { MP_QSTR_timeout_char, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = 10} },
        { MP_QSTR_read_buf_len, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = MAIX_UART_BUF} },
    };
    mp_arg_val_t args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, args);
    // set baudrate
    if (args[ARG_baudrate].u_int < 0 || args[ARG_baudrate].u_int > 0x500000 ) {
        mp_raise_ValueError("[MAIXPY]UART:invalid baudrate");
    }else{	
    	self->baudrate =args[ARG_baudrate].u_int;
    }
	
    // set data bits
    if (args[ARG_bitwidth].u_int >=5 && args[ARG_bitwidth].u_int <=8) {
            self->bitwidth=args[ARG_bitwidth].u_int;
    }else{
            mp_raise_ValueError("[MAIXPY]UART:invalid data bits");
    }

    // set parity
    if (UART_PARITY_NONE <= args[ARG_parity].u_int && args[ARG_parity].u_int <= UART_PARITY_EVEN) {
		self->parity = args[ARG_parity].u_int;
    }
	else{
		mp_raise_ValueError("[MAIXPY]UART:invalid parity");
	}

    // set stop bits  
    if( UART_STOP_1 <= args[ARG_stop].u_int && args[ARG_stop].u_int <= UART_STOP_2)
    {
	    switch (args[ARG_stop].u_int) {
	        case UART_STOP_1:
	            self->stop = UART_STOP_1;
	            break;
	        case UART_STOP_1_5:
	            self->stop = UART_STOP_1_5;
	            break;
	        case UART_STOP_2:
	            self->stop = UART_STOP_2;
	            break;
	        default:
	            mp_raise_ValueError("[MAIXPY]UART:invalid stop bits");
	            break;
	    }
    }
	// set timeout 
	if(args[ARG_timeout].u_int >= 0)
		self->timeout = args[ARG_timeout].u_int;
	if(args[ARG_timeout_char].u_int >= 0)
		self->timeout_char = args[ARG_timeout_char].u_int;
	self->active = true;
	m_del(byte, self->read_buf, self->read_buf_len);
	if(args[ARG_read_buf_len].u_int <= 0)
	{
		self->read_buf = NULL;
		self->read_buf_len = 0;
	}
	else
	{
        self->read_buf_len = args[ARG_read_buf_len].u_int + 1;
        self->read_buf = m_new(byte, self->read_buf_len );
	}
	self->read_buf_head = 0;
    self->read_buf_tail = 0;
	if(MICROPY_UARTHS_DEVICE == self->uart_num){
		self->bitwidth = 8;
		self->parity = 0;
		uarths_init();
		uarths_config(self->baudrate,self->stop);
		uarths_set_interrupt_cnt(UARTHS_RECEIVE,0);
		ENABLE_HSRX_INT(self);

	}
	else if(UART_DEVICE_MAX > self->uart_num){
	    uart_init(self->uart_num);
	    uart_config(self->uart_num, (size_t)self->baudrate, (size_t)self->bitwidth, self->stop,  self->parity);
		uart_set_receive_trigger(self->uart_num, UART_RECEIVE_FIFO_1);
		ENABLE_RX_INT(self);
	}
}

STATIC mp_obj_t machine_uart_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_arg_check_num(n_args, n_kw, 1, MP_OBJ_FUN_ARGS_MAX, true);
    // get uart id
    mp_int_t uart_num = mp_obj_get_int(args[0]);
    if (uart_num < 0 || UART_DEVICE_MAX == uart_num) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "[MAIXPY]UART%b:does not exist", uart_num));
    }else if (uart_num > MICROPY_UARTHS_DEVICE) {
    	nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "[MAIXPY]UART%b:does not exist", uart_num));
    }
    // create instance
    machine_uart_obj_t *self = m_new_obj(machine_uart_obj_t);
    self->base.type = &machine_uart_type;
    self->uart_num = uart_num;
    self->baudrate = 0;
    self->bitwidth = 8;
    self->parity = 0;
    self->stop = 1;
	self->read_buf_len = 0;
	self->data_len = 0;
    mp_map_t kw_args;
    mp_map_init_fixed_table(&kw_args, n_kw, args + n_args);
    machine_uart_init_helper(self, n_args - 1, args + 1, &kw_args);
	
    return MP_OBJ_FROM_PTR(self);
}


STATIC mp_obj_t machine_uart_init(size_t n_args, const mp_obj_t *args, mp_map_t *kw_args) {
    machine_uart_init_helper(args[0], n_args -1 , args + 1, kw_args);
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(machine_uart_init_obj, 0, machine_uart_init);


STATIC mp_obj_t machine_uart_deinit(mp_obj_t self_in) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
	self->active = false;
	if(MICROPY_UARTHS_DEVICE == self->uart_num)
		DISABLE_HSRX_INT(self); 
	else if(UART_DEVICE_MAX > self->uart_num)
		DISABLE_RX_INT(self);
	m_del_obj(machine_uart_obj_t, self);
	m_del(byte, self->read_buf, self->read_buf_len);
    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(machine_uart_deinit_obj, machine_uart_deinit);



STATIC const mp_rom_map_elem_t machine_uart_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR_init), MP_ROM_PTR(&machine_uart_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_deinit), MP_ROM_PTR(&machine_uart_deinit_obj) },
    
    { MP_ROM_QSTR(MP_QSTR_readline), MP_ROM_PTR(&mp_stream_unbuffered_readline_obj)},
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },
    { MP_ROM_QSTR(MP_QSTR_write), MP_ROM_PTR(&mp_stream_write_obj) },
	{ MP_ROM_QSTR(MP_QSTR_read), MP_ROM_PTR(&mp_stream_read_obj) },
	
	{ MP_ROM_QSTR(MP_QSTR_UART1), MP_ROM_INT(UART_DEVICE_1) },
	{ MP_ROM_QSTR(MP_QSTR_UART2), MP_ROM_INT(UART_DEVICE_2) },
	{ MP_ROM_QSTR(MP_QSTR_UART3), MP_ROM_INT(UART_DEVICE_3) },
	{ MP_ROM_QSTR(MP_QSTR_UARTHS), MP_ROM_INT(MICROPY_UARTHS_DEVICE) },
};

STATIC MP_DEFINE_CONST_DICT(machine_uart_locals_dict, machine_uart_locals_dict_table);

STATIC mp_uint_t machine_uart_read(mp_obj_t self_in, void *buf_in, mp_uint_t size, int *errcode) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    char *buf = buf_in;
	if(self->active == 0)
		return 0;
    // make sure we want at least 1 char
    if (size == 0) {
        return 0;
    }
	uint16_t next_head = 0;
    // read the data
	int data_num = 0;
	if(uart_rx_wait(self, self->timeout_char))
	{
		if(self->attached_to_repl)
		{
		    while(size) 
			{
		        int data = uart_rx_char(self);
				if(CHAR_NONE != data)
				{
		        	*buf++ = data;
					data_num++;
					size--;
					debug_print("[machine_uart_read] data is valid,size = %d,data = %c\n",size,data);
				}
		        else if (CHAR_NONE == data || !uart_rx_any(self)) 
				{
		            break;
		        }	
		    }
		}
		else
		{
			int ret_num = 0;
			while(size > 0)
			{
				uint8_t* buf = buf_in;
//				next_head = (self->read_buf_head + 1) % self->read_buf_len;
//				if(next_head == self->read_buf_tail)
//				{
//					TODO:solve buf - when enable rx irq,machine will stop running
//					if(MICROPY_UARTHS_DEVICE == self->uart_num)
//						DISABLE_HSRX_INT(self); 
//					else if(UART_DEVICE_MAX > self->uart_num)
//						DISABLE_RX_INT(self);
//				}
				ret_num = uart_rx_data(self, buf, size);
//				if (self->rx_int_flag == 0) {
//					//re-enable IRQ now we have room in buffer
//					if(MICROPY_UARTHS_DEVICE == self->uart_num)
//						ENABLE_HSRX_INT(self);
//					else if(UART_DEVICE_MAX > self->uart_num)
//						ENABLE_RX_INT(self);
//				}
				if(0 != ret_num)
				{
					data_num = data_num + ret_num;
					buf = buf + ret_num;
				}
				else if(0 == ret_num || !uart_rx_any(self)) 
				{
					break;
				}
				size = size - ret_num;
			}
		}
	}
	if(data_num != 0)
	{
		return data_num;
	}
	else
	{
		debug_print("[machine_uart_read] retrun error\n");
		*errcode = MP_EAGAIN;
		//return MP_STREAM_ERROR;//don't return MP_STREAM_ERROR.It will lead error which can't get reading buf
		return 0;
	}
}

STATIC mp_uint_t machine_uart_write(mp_obj_t self_in, const void *buf_in, mp_uint_t size, int *errcode) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    const byte *buf = buf_in;

	if(self->active == 0)
		return 0;
	
    // wait to be able to write the first character. EAGAIN causes write to return None
    if (!uart_tx_wait(self, self->timeout)) {
        *errcode = MP_EAGAIN;
        return MP_STREAM_ERROR;
    }

    // write the data
    size_t num_tx = uart_tx_data(self, buf, size, errcode);

    if (*errcode == 0 || *errcode == MP_ETIMEDOUT) {
        // return number of bytes written, even if there was a timeout
        return num_tx;
    } else {
        return MP_STREAM_ERROR;
    }
}


STATIC mp_uint_t machine_uart_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    machine_uart_obj_t *self = MP_OBJ_TO_PTR(self_in);
    mp_uint_t ret;
	if(self->active == 0)
		return 0;
    if (request == MP_STREAM_POLL) {
        uintptr_t flags = arg;
        ret = 0;
        if ((flags & MP_STREAM_POLL_RD) && uart_rx_any(self)) {
            ret |= MP_STREAM_POLL_RD;
        }
		//TODO:add Judging transmission enable
        if ((flags & MP_STREAM_POLL_WR) ) {
            ret |= MP_STREAM_POLL_WR;
        }
    } else {
        *errcode = MP_EINVAL;
        ret = MP_STREAM_ERROR;
    }
    return ret;
}


STATIC const mp_stream_p_t uart_stream_p = {
    .read = machine_uart_read,
    .write = machine_uart_write,
    .ioctl = machine_uart_ioctl,
    .is_text = false,
};

const mp_obj_type_t machine_uart_type = {
    { &mp_type_type },
    .name = MP_QSTR_UART,
    .print = machine_uart_print,
    .make_new = machine_uart_make_new,
    .getiter = mp_identity_getiter,
    .iternext = mp_stream_unbuffered_iter,
    .protocol = &uart_stream_p,
    .locals_dict = (mp_obj_dict_t*)&machine_uart_locals_dict,
};