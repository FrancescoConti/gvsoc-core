/*
 * Copyright (C) 2018 GreenWaves Technologies
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* 
 * Authors: Germain Haugou, GreenWaves Technologies (germain.haugou@greenwaves-technologies.com)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/uart.hpp>
#include <vp/itf/clock.hpp>
#include <vp/itf/i2c.hpp>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <iostream>


#define PI_TESTBENCH_CMD_GPIO_LOOPBACK 1
#define PI_TESTBENCH_MAX_REQ_SIZE 256


class Testbench;


typedef struct {
    uint8_t input;
    uint8_t output;
    uint8_t enabled;
} pi_testbench_req_t;


typedef enum {
    STATE_WAITING_CMD,
    STATE_WAITING_REQUEST
} testbench_state_e;


class Gpio
{
public:
    vp::wire_slave<int> itf;

    int loopback = -1;
    uint32_t value;
};



typedef enum
{
  I2C_STATE_WAIT_START,
  I2C_STATE_WAIT_ADDRESS,
  I2C_STATE_GET_DATA,
  I2C_STATE_ACK
} I2c_state_e;


class I2C
{
public:
    void conf(Testbench *top, int id);
    void sync(int scl, int sda);
    void handle_byte();

    vp::i2c_slave itf;

    Testbench *top;
    int id;
    I2c_state_e state;
    int prev_sda;
    int pending_send_ack;
    uint32_t address;
    uint32_t pending_data;
    int pending_bits;
    int is_read;
};


class Testbench : public vp::component
{
public:
    Testbench(js::config *config);

    int build();

    void uart_tx_sampling();

    vp::trace trace;

private:

    void uart_start_tx_sampling(int baudrate);
    void uart_stop_tx_sampling();
    void handle_received_byte(uint8_t byte);

    void handle_gpio_loopback();

    static void uart_sync(void *__this, int data);
    static void gpio_sync(void *__this, int value, int id);
    static void i2c_sync(void *__this, int scl, int sda, int id);

    static void uart_sampling_handler(void *__this, vp::clock_event *event);

    testbench_state_e state;
    string ctrl_type;
    uint64_t period;
    bool uart_tx_wait_start = true;
    bool uart_tx_wait_stop = false;
    int uart_current_tx;
    uint64_t uart_baudrate;
    int uart_nb_bits;
    bool uart_sampling_tx = false;
    uint8_t uart_byte;
    int nb_gpio;
    int nb_i2c;
    int req_size;
    int current_req_size;
    uint8_t req[PI_TESTBENCH_MAX_REQ_SIZE];
    uint8_t cmd;

    std::vector<Gpio> gpios;
    std::vector<I2C> i2cs;
    vp::uart_slave uart_in;

    vp::clock_event *uart_sampling_event;
    vp::clock_master clock_cfg;
};

Testbench::Testbench(js::config *config)
    : vp::component(config)
{
}


int Testbench::build()
{
    traces.new_trace("trace", &trace, vp::DEBUG);

    this->new_master_port("clock_cfg", &clock_cfg);

    this->ctrl_type = get_js_config()->get("ctrl_type")->get_str();
    this->nb_gpio = get_js_config()->get("nb_gpio")->get_int();
    this->nb_i2c = get_js_config()->get("nb_i2c")->get_int();

    if (this->ctrl_type == "uart")
    {
        this->uart_baudrate = get_js_config()->get("uart_baudrate")->get_int();
        this->uart_in.set_sync_meth(&Testbench::uart_sync);
        this->new_slave_port("ctrl", &this->uart_in);
        this->uart_sampling_event = event_new(Testbench::uart_sampling_handler);
    }

    this->gpios.resize(this->nb_gpio);
    
    for (int i=0; i<this->nb_gpio; i++)
    {
        this->gpios[i].itf.set_sync_meth_muxed(&Testbench::gpio_sync, i);
        this->new_slave_port("gpio" + std::to_string(i), &this->gpios[i].itf);
    }

    this->i2cs.resize(this->nb_i2c);
    
    for (int i=0; i<this->nb_i2c; i++)
    {
        this->i2cs[i].conf(this, i);
        this->i2cs[i].itf.set_sync_meth_muxed(&Testbench::i2c_sync, i);
        this->new_slave_port("i2c" + std::to_string(i), &this->i2cs[i].itf);
    }

    this->state = STATE_WAITING_CMD;

    return 0;
}


void Testbench::uart_tx_sampling()
{
    this->trace.msg(vp::trace::LEVEL_TRACE, "Sampling bit (value: %d)\n", uart_current_tx);

    if (uart_tx_wait_stop)
    {
        if (uart_current_tx == 1)
        {
            this->trace.msg(vp::trace::LEVEL_TRACE, "Received stop bit\n", uart_current_tx);
            uart_tx_wait_start = true;
            uart_tx_wait_stop = false;
            this->uart_stop_tx_sampling();
        }
    }
    else
    {
        this->trace.msg(vp::trace::LEVEL_TRACE, "Received data bit (data: %d)\n", uart_current_tx);
        uart_byte = (uart_byte >> 1) | (uart_current_tx << 7);
        uart_nb_bits++;
        if (uart_nb_bits == 8)
        {
            this->trace.msg(vp::trace::LEVEL_DEBUG, "Sampled TX byte (value: 0x%x)\n", uart_byte);
            this->trace.msg(vp::trace::LEVEL_TRACE, "Waiting for stop bit\n");
            uart_tx_wait_stop = true;
            this->handle_received_byte(uart_byte);
        }
    }
}


void Testbench::uart_sampling_handler(void *__this, vp::clock_event *event)
{
    Testbench *_this = (Testbench *)__this;

    _this->uart_tx_sampling();

    if (_this->uart_sampling_tx)
    {
        _this->event_enqueue(_this->uart_sampling_event, 2);
    }
}


void Testbench::uart_sync(void *__this, int data)
{
    Testbench *_this = (Testbench *)__this;

    _this->trace.msg(vp::trace::LEVEL_TRACE, "UART sync (value: %d, waiting_start: %d)\n", data, _this->uart_tx_wait_start);

    _this->uart_current_tx = data;

    if (_this->uart_tx_wait_start && data == 0)
    {
        _this->trace.msg(vp::trace::LEVEL_TRACE, "Received start bit\n");

        _this->uart_start_tx_sampling(_this->uart_baudrate);
        _this->uart_tx_wait_start = false;
        _this->uart_nb_bits = 0;
    }
}


void Testbench::gpio_sync(void *__this, int value, int id)
{
    Testbench *_this = (Testbench *)__this;
    Gpio *gpio = &_this->gpios[id];

    _this->trace.msg(vp::trace::LEVEL_DEBUG, "Received GPIO sync (id: %d)\n", id);

    gpio->value = value;

    if (gpio->loopback != -1)
    {
        _this->trace.msg(vp::trace::LEVEL_DEBUG, "Generating gpio on loopback (id: %d)\n", gpio->loopback);
        _this->gpios[gpio->loopback].itf.sync(value);
    }
}


void I2C::conf(Testbench *top, int id)
{
    this->top = top;
    this->id = id;
    this->state = I2C_STATE_WAIT_START;
    this->prev_sda = 1;
    this->pending_send_ack = false;
}


void I2C::handle_byte()
{
    this->top->trace.msg(vp::trace::LEVEL_DEBUG, "Received I2C byte (id: %d, value: 0x%x)\n", this->id, this->pending_data & 0xff);
}


void I2C::sync(int scl, int sda)
{
    this->top->trace.msg(vp::trace::LEVEL_TRACE, "Received I2C sync (id: %d, scl: %d, sda: %d)\n", this->id, scl, sda);

    if (scl == 1 && this->prev_sda != sda)
    {
        if (this->prev_sda == 1)
        {
            this->top->trace.msg(vp::trace::LEVEL_TRACE, "Received I2C start bit (id: %d)\n", id);

            this->state = I2C_STATE_WAIT_ADDRESS;
            this->address = 0;
            this->pending_bits = 8;
        }
        else
        {
            this->top->trace.msg(vp::trace::LEVEL_TRACE, "Received I2C stop bit (id: %d)\n", id);
            this->state = I2C_STATE_WAIT_START;
        }
    }
    else
    {
        if (scl == 0)
        {
            if (this->pending_send_ack)
            {
                this->pending_send_ack = false;
                this->itf.sync(1);
            }
        }
        else
        {
            switch (this->state)
            {
                case I2C_STATE_WAIT_ADDRESS:
                {
                    if (this->pending_bits > 1)
                    {
                        this->address = (this->address << 1) | sda;
                    }
                    else
                    {
                        this->is_read = sda;
                    }
                    this->pending_bits--;
                    if (this->pending_bits == 0)
                    {
                        this->state = I2C_STATE_ACK;
                        this->pending_bits = 8;
                    }
                    break;
                }

                case I2C_STATE_GET_DATA:
                {
                    //if (sda_out)
                    //{
                    //    *sda_out = (this->pending_send_byte >> 7) & 1;
                    //    this->pending_send_byte <<= 1;
                    //}
                    this->top->trace.msg(vp::trace::LEVEL_TRACE, "Got I2C data (id: %d, sda: %d)\n", id, this->pending_bits);
                    this->pending_data = (this->pending_data << 1) | sda;
                    this->pending_bits--;
                    if (this->pending_bits == 0)
                    {
                        this->pending_bits = 8;
                        this->handle_byte();
                        this->state = I2C_STATE_ACK;
                    }
                    break;
                }
                
                case I2C_STATE_ACK:
                {
                    this->top->trace.msg(vp::trace::LEVEL_TRACE, "Generate I2C ack (id: %d)\n", id);

                    this->itf.sync(0);
                    this->state = I2C_STATE_GET_DATA;
                    break;
                }
            }
        }
    }

    this->prev_sda = sda;
}


void Testbench::i2c_sync(void *__this, int scl, int sda, int id)
{
    Testbench *_this = (Testbench *)__this;

    _this->i2cs[id].sync(scl, sda);
}


void Testbench::uart_start_tx_sampling(int baudrate)
{
    this->trace.msg(vp::trace::LEVEL_TRACE, "Start TX sampling (baudrate: %d)\n", this->uart_baudrate);

    // We set the frequency to twice the baudrate to be able sampling in the
    // middle of the cycle
    this->clock_cfg.set_frequency(this->uart_baudrate*2);

    this->uart_sampling_tx = 1;

    this->event_reenqueue(this->uart_sampling_event, 3);
}


void Testbench::uart_stop_tx_sampling(void)
{
    this->uart_sampling_tx = 0;
    
    if (this->uart_sampling_event->is_enqueued())
    {
        this->event_cancel(this->uart_sampling_event);
    }
}


void Testbench::handle_received_byte(uint8_t byte)
{
    if (this->state == STATE_WAITING_CMD)
    {
        this->cmd = byte;

        switch (byte) {
            case PI_TESTBENCH_CMD_GPIO_LOOPBACK:
                this->state = STATE_WAITING_REQUEST;
                this->req_size = sizeof(pi_testbench_req_t);
                this->current_req_size = 0;
                break;
        }
    }
    else if (this->state == STATE_WAITING_REQUEST)
    {
        this->req[this->current_req_size++] = byte;
        if (this->current_req_size == this->req_size)
        {
            this->state = STATE_WAITING_CMD;

            switch (this->cmd) {
                case PI_TESTBENCH_CMD_GPIO_LOOPBACK:
                    this->handle_gpio_loopback();
                    break;
            }

        }
    }
}


void Testbench::handle_gpio_loopback()
{
    pi_testbench_req_t *req = (pi_testbench_req_t *)this->req;

    this->trace.msg(vp::trace::LEVEL_INFO, "Handling GPIO loopback (enabled: %d, output: %d, intput: %d)\n", req->enabled, req->output, req->input);

    if (req->enabled)
    {
        this->gpios[req->output].loopback = req->input;
        this->gpios[req->input].itf.sync(this->gpios[req->output].value);
    }
    else
    {
        this->gpios[req->output].loopback = -1;
    }
}


extern "C" vp::component *vp_constructor(js::config *config)
{
    return new Testbench(config);
}
