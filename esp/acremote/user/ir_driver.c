/*
 *  IR led driver that replicates Samsung ARH-901 remote functionality
 *
 *  Copyright (C) 2017 Filippo Argiolas <filippo.argiolas@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

#include "ets_sys.h"
#include "gpio.h"
#include "os_type.h"
#include "osapi.h"
#include "mem.h"

#include "ir_driver.h"
#include "utils.h"
#include "queue.h"

/* TIMINGS */

/* 38 kHz modulation, ~26us periods, 13us half periods */
/* Mark: signal oscillates up and down with 26us period for a given
 * amout of time */
/* Space: signal stays low for a given amount of time */
#define TICK 13 /* 1000/38/2 */


/*
  Header (mitsub): 3ms mark + 1.5ms space
  Data: each bit starts with a 500us mark and its value is encoded in
  the subsequent space length, long space is one short space is 0.

  Transmission ends with a bit mark and line goes down for at least one 1-bit space
  This is definitely true with samsung, not sure about mitsubishi, still checking
 */
#define HEADER_MARK_US  3000
#define HEADER_SPACE_US 1500

#define BIT_SPACE_0_US  400
#define BIT_SPACE_1_US 1200
#define BIT_MARK_US     400

#define _gpio_pin  4
#define _gpio_func FUNC_GPIO4
#define _gpio_mux  PERIPHS_IO_MUX_GPIO4_U

static queue *cmd_queue = NULL;

typedef enum {
    STATE_IDLE,
    STATE_INIT,
    STATE_HDR_MARK,
    STATE_HDR_SPACE,
    STATE_CMD_MARK,
    STATE_CMD_SPACE,
    STATE_DONE,
} BurstState;

static BurstState ir_state = STATE_IDLE;

typedef struct _CommandData {
    uint8_t cmd[cmd_len];
    uint8_t bit;

    cmd_done_func_t done_cb;
    void           *done_data;
} CommandData;

static CommandData *cur_data = NULL;

static void mark (uint16_t len)
{
    uint16_t i;
    for(i=0; i<len/TICK/2; i++) {
        GPIO_OUTPUT_SET(_gpio_pin, 0);
        os_delay_us(TICK);
        GPIO_OUTPUT_SET(_gpio_pin, 1);
        os_delay_us(TICK);
    }
}


static void space (uint16_t len)
{
    GPIO_OUTPUT_SET(_gpio_pin, 0);
    os_delay_us(len);
}


/* Simple bit banging, it apparently works. Watchdog doesn't bark, but
 * given a single command is usually of the order of 100ms, it should
 * and it probably will. */
void ir_send_cmd_simple (uint8_t *buf)
{
    uint8_t i,j;

    mark(HEADER_MARK_US);
    space(HEADER_SPACE_US);

    for(i=0; i<cmd_len; i++) {
        for (j=0; j<8; j++) {
            mark(BIT_MARK_US);
            if (buf[i] & 1<<j) {
                space(BIT_SPACE_1_US);
            }
            else {
                space(BIT_SPACE_0_US);
            }
        }
    }

    mark(BIT_MARK_US);
}

void ir_send_cmd_short (uint8_t *buf,
                        uint16_t header_mark_us, uint16_t header_space_us,
                        uint16_t bit_mark_us, uint16_t bit_space_0_us, uint16_t bit_space_1_us)
{
    uint8_t i,j;

    mark(header_mark_us);
    space(header_space_us);

    for(i=0; i<4; i++) {
        for (j=0; j<8; j++) {
            mark(bit_mark_us);
            if (buf[i] & 1<<j) {
                space(bit_space_1_us);
            }
            else {
                space(bit_space_0_us);
            }
        }
    }

    mark(bit_mark_us);
}




/* Async command state machine */
/*
  Locks only while sending marks, usually around 500us, 3ms for
  headers, I should probably use hardware timers for these too.

  Spaces are proper hardware delays.

  Commands are put in a queue and executed asynchronously
*/

void machine_func (void);

void change_state_delayed (BurstState state, uint16_t delay) {
    ir_state = state;

    if (delay > 0) {
        hw_timer_set_func(machine_func);
        hw_timer_arm(delay);
    } else {
        machine_func(); /* super ugly, locking, FIXME */
    }
}

void change_state (BurstState state) {
    return change_state_delayed(state, 0);
}

void machine_func (void) {
    uint8_t i;
    uint8 pos, seek;

    switch (ir_state) {
    case STATE_IDLE:
        break;
    case STATE_INIT:
        cur_data = cmd_queue->pop(cmd_queue);

        os_printf("IR: sending command ");
        for (i=0; i<cmd_len; i++) {
            os_printf("%02X ", cur_data->cmd[i]);
        }
        os_printf("\n");

        GPIO_OUTPUT_SET(_gpio_pin, 0);
        change_state(STATE_HDR_MARK);
        break;
    case STATE_HDR_MARK:
        mark(HEADER_MARK_US);
        change_state(STATE_HDR_SPACE);
        break;
    case STATE_HDR_SPACE:
        GPIO_OUTPUT_SET(_gpio_pin, 0);
        change_state_delayed(STATE_CMD_MARK, HEADER_SPACE_US);
        break;
    case STATE_CMD_MARK:
        mark(BIT_MARK_US);
        if (cur_data->bit <= (8*cmd_len-1))
            change_state(STATE_CMD_SPACE);
        else
            change_state(STATE_DONE);
        break;
    case STATE_CMD_SPACE:
        GPIO_OUTPUT_SET(_gpio_pin, 0);
        /* bit is the position in the whole burst sequence */
        /* pos is the current byte in the command array */
        /* seek it the bit we want from current byte */
        pos = cur_data->bit / 8;
        seek = cur_data->bit % 8;
        cur_data->bit++;
        if (cur_data->cmd[pos] & 1<<seek) {
            change_state_delayed(STATE_CMD_MARK, BIT_SPACE_1_US);
        }
        else {
            change_state_delayed(STATE_CMD_MARK, BIT_SPACE_0_US);
        }
        break;
    case STATE_DONE:
        GPIO_OUTPUT_SET(_gpio_pin, 0);
        if (cur_data->done_cb != NULL) cur_data->done_cb(cur_data->done_data);
        os_free(cur_data);

        if (cmd_queue->len == 0) {
            os_printf("IR: queue done\n");
            queue_free(cmd_queue);
            cmd_queue = NULL;
            change_state_delayed(STATE_IDLE, BIT_SPACE_1_US);
        } else {
            change_state_delayed(STATE_INIT, BIT_SPACE_1_US);
        }

        break;
    }
}

void ICACHE_FLASH_ATTR
ir_init(void) {
    PIN_FUNC_SELECT(_gpio_mux, _gpio_func);
    hw_timer_init(0, 0);
}

static CommandData *command_data_new(uint8_t *command,
                                     cmd_done_func_t done_cb, void *done_data)
{
    CommandData *data = (CommandData *) os_zalloc(sizeof(CommandData));

    os_memcpy(data->cmd, command, cmd_len*sizeof(uint8_t));
    data->bit = 0;
    data->done_cb = done_cb;
    data->done_data = done_data;

    return data;
}

void ir_send_cmd_full (uint8_t *command,
                       cmd_done_func_t done_cb, void *done_data)
{
    uint8_t i;
    CommandData *data = command_data_new(command, done_cb, done_data);

    if (cmd_queue == NULL) {
        os_printf("IR: queue init\n");
        cmd_queue = queue_new();
    }

    os_printf("IR: pushing command ");
    for (i=0; i<cmd_len; i++) {
        os_printf("%02X ", data->cmd[i]);
    }
    os_printf("\n");

    cmd_queue->push(cmd_queue, data);

    if ((cmd_queue->len == 1) && (ir_state == STATE_IDLE))
        change_state(STATE_INIT);
}

void ir_send_cmd (uint8_t *command) {
    ir_send_cmd_full (command, NULL, NULL);
}



/* move everything to a proper object with checks and all */

static ICACHE_FLASH_ATTR
uint8_t checksum (uint8_t *buf) {
    /* 34 - ones sum */
    uint8_t acc,i,j;
    acc = 34;
    for (i=0; i<cmd_len;i++)
        for (j=0; j<8; j++) {
            acc -= (buf[i] & 1<<j)>>j;
        }
    return acc;
}
