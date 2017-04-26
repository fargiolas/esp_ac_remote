#include "ets_sys.h"
#include "gpio.h"
#include "os_type.h"
#define USE_US_TIMER
#include "osapi.h"
#include "mem.h"

#include "ir_driver.h"
#include "utils.h"

#define HEADER_MARK_US  3000
#define HEADER_SPACE_US 9000

#define BIT_SPACE_0_US  500 /* 450-470 */
#define BIT_SPACE_1_US 1500 /* 1450 */
#define BIT_MARK_US       500 /* 570 */

#define TICK 13 /* 1000/38/2 */

#define _gpio_pin  4
#define _gpio_func FUNC_GPIO4
#define _gpio_mux  PERIPHS_IO_MUX_GPIO4_U

static volatile os_timer_t machine_timer;
static volatile bool ir_lock = FALSE;

typedef enum {
    STATE_IDLE,
    STATE_HDR_MARK,
    STATE_HDR_SPACE,
    STATE_CMD_MARK,
    STATE_CMD_SPACE,
    STATE_DONE,
} BurstState;

typedef struct _MachineData {
    uint8_t command[7];
    uint8_t bit;
    BurstState state;
} MachineData;


/*
  Bit bang GPIO pin for <len> us.
  TICK is expected to be half a period of the carrier (38kHz) in
  microseconds (1000/38/2).

  Not perfect but the receiver seems tolerant enough. Also, timing
  from the original remote wasn't perfect at all.

  A bit worried about those delays, each command can get pretty
  lengthy, around 100ms. Watchdog seems happy, but still...
 */

static void mark (uint16_t len) {
    uint16_t i;
    for(i=0; i<len/TICK/2; i++) {
        GPIO_OUTPUT_SET(_gpio_pin, 0);
        os_delay_us(TICK);
        GPIO_OUTPUT_SET(_gpio_pin, 1);
        os_delay_us(TICK);
    }
}


static void space (uint16_t len) {
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

void
machine_func (void *userdata);

const char *state_to_str(BurstState state) {
    switch (state) {
    case STATE_IDLE:      return "STATE_IDLE";
    case STATE_HDR_MARK:  return "STATE_HDR_MARK";
    case STATE_HDR_SPACE: return "STATE_HDR_SPACE";
    case STATE_CMD_MARK:  return "STATE_CMD_MARK";
    case STATE_CMD_SPACE: return "STATE_CMD_SPACE";
    case STATE_DONE:      return "STATE_DONE";
    }
}


void
change_state_delayed (BurstState state, MachineData *data, uint16_t delay) {
//    os_printf("[%d] %s -> %s (%d)\n", system_get_time(),
//              state_to_str(data->state), state_to_str(state), delay);

    data->state = state;

    if (delay > 0) {
        os_timer_setfn(&machine_timer, (os_timer_func_t *)machine_func, data);
        os_timer_arm_us(&machine_timer, delay, 0);
    } else {
        machine_func(data); /* super ugly, locking, FIXME */
    }
}

void
change_state (BurstState state, MachineData *data) {
    return change_state_delayed(state, data, 0);
}

void
machine_func (void *userdata) {
    MachineData *data = (MachineData *) userdata;
    uint8 pos, seek;

    os_timer_disarm(&machine_timer); /* safe if not armed? */

    switch (data->state) {
    case STATE_IDLE:
        GPIO_OUTPUT_SET(_gpio_pin, 0);
        change_state(STATE_HDR_MARK, data);
        break;
    case STATE_HDR_MARK:
        mark(HEADER_MARK_US);
        change_state(STATE_HDR_SPACE, data);
        break;
    case STATE_HDR_SPACE:
        GPIO_OUTPUT_SET(_gpio_pin, 0);
        change_state_delayed(STATE_CMD_MARK, data, HEADER_SPACE_US);
        break;
    case STATE_CMD_MARK:
        mark(BIT_MARK_US);
         if (data->bit <= (8*7-1))
            change_state(STATE_CMD_SPACE, data);
        else
            change_state(STATE_DONE, data);
        break;
    case STATE_CMD_SPACE:
        GPIO_OUTPUT_SET(_gpio_pin, 0);
        pos = data->bit / 8;
        seek = data->bit % 8;
        data->bit++;
        if (data->command[pos] & 1<<seek) {
            change_state_delayed(STATE_CMD_MARK, data, BIT_SPACE_1_US);
        }
        else {
            change_state_delayed(STATE_CMD_MARK, data, BIT_SPACE_0_US);
        }
        break;
    case STATE_DONE:
        GPIO_OUTPUT_SET(_gpio_pin, 0);
        os_free(data);
        ir_lock = FALSE;
        break;
    }
}

void ICACHE_FLASH_ATTR
ir_init(void) {
    PIN_FUNC_SELECT(_gpio_mux, _gpio_func);
}

void ICACHE_FLASH_ATTR
ir_send_cmd (uint8_t *command) {
    uint8_t i;
    MachineData *data;

    /* FIXME: implement async properly, wait for the lock to be
     * released and send new command */
    if (ir_lock) {
        os_printf("IR driver busy, ignoring command: ");
        for (i=0; i<7; i++) {
            os_printf("%02X ", command[i]);
        }
        os_printf("\n");
        return;
    }

    ir_lock = TRUE;

    data = (MachineData *) os_zalloc(sizeof(MachineData));
    os_memcpy(data->command, command);
    data->state = STATE_IDLE;
    data->bit = 0;

    os_printf("Sending command: ");
    for (i=0; i<7; i++) {
        os_printf("%02X ", data->command[i]);
    }
    os_printf("\n");

    change_state(STATE_IDLE, data);
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
