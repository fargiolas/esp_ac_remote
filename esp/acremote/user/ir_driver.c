#include "ets_sys.h"
#include "gpio.h"
#include "os_type.h"
#include "osapi.h"
#include "mem.h"

#include "ir_driver.h"
#include "utils.h"
#include "queue.h"

#define HEADER_MARK_US  3000
#define HEADER_SPACE_US 9000

#define BIT_SPACE_0_US  500 /* 450-470 */
#define BIT_SPACE_1_US 1500 /* 1450 */
#define BIT_MARK_US       500 /* 570 */

#define TICK 13 /* 1000/38/2 */

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
static uint8_t current_bit = 0;
static uint8_t *current_cmd;


/*
  Bit bang GPIO pin for <len> us.
  TICK is expected to be half a period of the carrier (38kHz) in
  microseconds (1000/38/2).

  Not perfect but the receiver seems tolerant enough. Also, timing
  from the original remote wasn't perfect at all.

  A bit worried about those delays, each command can get pretty
  lengthy, around 100ms. Watchdog seems happy, but still...
 */

static void
mark (uint16_t len) {
    uint16_t i;
    for(i=0; i<len/TICK/2; i++) {
        GPIO_OUTPUT_SET(_gpio_pin, 0);
        os_delay_us(TICK);
        GPIO_OUTPUT_SET(_gpio_pin, 1);
        os_delay_us(TICK);
    }
}


static void
space (uint16_t len) {
    GPIO_OUTPUT_SET(_gpio_pin, 0);
    os_delay_us(len);
}

void
ir_send_cmd_simple (uint8_t *buf) {
    uint8_t i,j;

    mark(HEADER_MARK_US);
    space(HEADER_SPACE_US);

    for(i=0; i<7; i++) {
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

static ICACHE_FLASH_ATTR
uint8_t checksum (uint8_t *buf) {
    /* 34 - ones sum */
    uint8_t acc,i,j;
    acc = 34;
    for (i=0; i<7;i++)
        for (j=0; j<8; j++) {
            acc -= (buf[i] & 1<<j)>>j;
        }
    return acc;
}

void machine_func (void);

const char *state_to_str(BurstState state) {
    switch (state) {
    case STATE_IDLE:      return "STATE_IDLE";
    case STATE_INIT:      return "STATE_INIT";
    case STATE_HDR_MARK:  return "STATE_HDR_MARK";
    case STATE_HDR_SPACE: return "STATE_HDR_SPACE";
    case STATE_CMD_MARK:  return "STATE_CMD_MARK";
    case STATE_CMD_SPACE: return "STATE_CMD_SPACE";
    case STATE_DONE:      return "STATE_DONE";
    }
}


void
change_state_delayed (BurstState state, uint16_t delay) {
//    os_printf("[%d] %s -> %s (%d)\n", system_get_time(),
//              state_to_str(data->state), state_to_str(state), delay);

    ir_state = state;

    if (delay > 0) {
        hw_timer_set_func(machine_func);
        hw_timer_arm(delay);
    } else {
        machine_func(); /* super ugly, locking, FIXME */
    }
}

void
change_state (BurstState state) {
    return change_state_delayed(state, 0);
}

void
machine_func (void) {
    uint8_t i;
    uint8 pos, seek;

    switch (ir_state) {
    case STATE_IDLE:
        break;
    case STATE_INIT:
        current_cmd = (uint8_t *) cmd_queue->pop(cmd_queue);

        os_printf("Sending command: ");
        for (i=0; i<7; i++) {
            os_printf("%02X ", current_cmd[i]);
        }
        os_printf("\n");

        current_bit = 0;

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
        if (current_bit <= (8*7-1))
            change_state(STATE_CMD_SPACE);
        else
            change_state(STATE_DONE);
        break;
    case STATE_CMD_SPACE:
        GPIO_OUTPUT_SET(_gpio_pin, 0);
        pos = current_bit / 8;
        seek = current_bit % 8;
        current_bit++;
        if (current_cmd[pos] & 1<<seek) {
            change_state_delayed(STATE_CMD_MARK, BIT_SPACE_1_US);
        }
        else {
            change_state_delayed(STATE_CMD_MARK, BIT_SPACE_0_US);
        }
        break;
    case STATE_DONE:
        GPIO_OUTPUT_SET(_gpio_pin, 0);
        os_free(current_cmd);
        if (cmd_queue->len == 0) {
            os_printf("Queue done. See you in another life brother.\n");
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

void
ir_send_cmd (uint8_t *command) {
    uint8_t i;
    uint8_t *cmd = (uint8_t *) os_zalloc(7*sizeof(uint8_t));
    os_memcpy(cmd, command, 7*sizeof(uint8_t));

    if (cmd_queue == NULL) {
        os_printf("Queue init\n");
        cmd_queue = queue_new();
    }

    os_printf("Pushing command: ");
    for (i=0; i<7; i++) {
        os_printf("%02X ", cmd[i]);
    }
    os_printf("\n");

    cmd_queue->push(cmd_queue, cmd);

    if ((cmd_queue->len == 1) && (ir_state == STATE_IDLE))
        change_state(STATE_INIT);
}

/* move everything to a proper object with checks and all */
void ICACHE_FLASH_ATTR
build_command(uint8_t *cmd,
              uint8_t mode, uint8_t temperature,
              uint8_t fan_speed, uint8_t swing,
              uint8_t energy_mode, uint8_t ions,
              uint8_t display)
{
    /* #: template (constant bits)
       C: checksum 34 - SUM
       S: swing       [A,F]       => [on,off]
       D: display     [E,F]       => [on,off]
       P: powersave   [1,3,7,F]   => [none,moon,turbo,powersave(T=26, swing=on)]
       T: temperature [0,E]       => [16°C,30°C]
       M: mode        [0,1,2,3,4] => [auto,cool,dry,fan,warm]
       F: fan speed   [1,5,9,B,D] => [auto,low,mid,high,natural(only in auto mode)]
       I: ions        [8,0]       => [on,off]    */

    /*                        ##    C#    S#    DP    T#    MF    #I */
    uint8_t template[7] = { 0x01, 0x02, 0x0E, 0x00, 0x03, 0x00, 0xF0 };
    uint8_t i;
    memcpy(cmd, template, 7);

    /* FIXME: list all invalid modes and rules */
    if (mode == IR_AC_MODE_FAN) temperature = 24;
    else if (mode == IR_AC_MODE_AUTO) temperature = 25;
    else if (mode == IR_AC_MODE_COOL) temperature = CLAMP(temperature, 18, 30);
    else temperature = CLAMP(temperature, 16, 30);

    temperature = (temperature - 16) << 4;

    if (mode != IR_AC_MODE_COOL && energy_mode == IR_POWER_MODE_SAVE)
        energy_mode = IR_POWER_MODE_NORMAL;
    if (energy_mode == IR_POWER_MODE_SAVE) {
        temperature = 26;
        swing = IR_SWING_MODE_ON;
    }

    cmd[2] |= swing;
    cmd[3] |= display | energy_mode;
    cmd[4] |= temperature;
    cmd[5] |= mode | fan_speed;
    cmd[6] |= ions;
    cmd[1] |= checksum(cmd) << 4;

    os_printf("Command: ");
    for (i=0; i<7; i++) {
        os_printf("%02X ", cmd[i]);
    }
    os_printf("\n");
}
