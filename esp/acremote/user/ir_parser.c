#ifndef _TEST__MODE____

#include "ets_sys.h"
#include "gpio.h"
#include "os_type.h"
#include "osapi.h"
#include "mem.h"
#include "user_config.h"
#include "user_interface.h"

#else

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define os_zalloc malloc
#define os_printf printf
#define os_free free
#define uint8_t unsigned char
#define uint32_t long int

#endif

#include "utils.h"
#include "ir_driver.h"

typedef struct _ACState {
    uint8_t power;
    uint8_t mode;
    uint8_t temperature;
    uint8_t fan;
    uint8_t swing;
    uint8_t energy;
    uint8_t ion;
    uint8_t display;
} ACState;

typedef struct {
    char     *key;
    uint8_t  *value_dest;
    char     *value_name;
    uint8_t   value_data;
} mode_parser_table;

char *chomp(const char *str, uint32_t len)
{
    char *tmp = (char *) os_zalloc(len+1 * sizeof(char));
    char *out;

    char *i, *o;

    /* strip invalid characters and null terminates input string */
    for (i = (char *) str, o = tmp; (i - str) < len; i++) {
        if ((isalnum(*i)) || (*i == '=') || (*i == ',')) {
            *o = *i; o++;
        }
    }

    *o = '\0';

    out = (char *) os_zalloc(strlen(tmp) * sizeof(char));
    strcpy(out, tmp);

    os_free(tmp);

    return out;
}

static char *get_pair(char *key_pos, char *key, char *value)
{
    uint8_t key_sz, value_sz;
    char *value_pos;
    char *iter;

    value_pos = key_pos;

    for (iter = key_pos; (*iter != ',') && (*iter != '\0'); iter++) {
        if (*iter == '=') {
            key_sz = iter - key_pos;
            value_pos = iter + 1;
            memcpy(key, key_pos, key_sz * sizeof(char));
            key[key_sz] = '\0';
        }
    }
    value_sz = iter - value_pos;
    memcpy(value, value_pos, value_sz * sizeof(char));
    value[value_sz] = '\0';

    return *iter == '\0' ? NULL : iter + 1;
}

static void parse_pair(char *key, char *value, ACState *state)
{
    uint8_t i;
    mode_parser_table table[] = {
        /***************************************************************/
        {   "power", &(state->power),      "on", IR_POWER_MODE_ON },
        {   "power", &(state->power),     "off", IR_POWER_MODE_OFF },
        /***************************************************************/
        {    "mode", &(state->mode),     "auto", IR_AC_MODE_AUTO },
        {    "mode", &(state->mode),     "cool", IR_AC_MODE_COOL },
        {    "mode", &(state->mode),      "dry", IR_AC_MODE_DRY },
        {    "mode", &(state->mode),      "fan", IR_AC_MODE_FAN },
        {    "mode", &(state->mode),     "warm", IR_AC_MODE_WARM },
        /***************************************************************/
        {     "fan", &(state->fan),      "auto", IR_FAN_MODE_AUTO },
        {     "fan", &(state->fan),       "low", IR_FAN_MODE_LOW },
        {     "fan", &(state->fan),       "mid", IR_FAN_MODE_MID },
        {     "fan", &(state->fan),      "high", IR_FAN_MODE_HIGH },
        {     "fan", &(state->fan),   "natural", IR_FAN_MODE_NATURAL },
        /***************************************************************/
        {  "energy", &(state->energy), "normal", IR_ENERGY_MODE_NORMAL },
        {  "energy", &(state->energy),  "sleep", IR_ENERGY_MODE_SLEEP },
        {  "energy", &(state->energy),  "turbo", IR_ENERGY_MODE_TURBO },
        {  "energy", &(state->energy),   "save", IR_ENERGY_MODE_SAVE },
        /***************************************************************/
        { "display", &(state->display),    "on", IR_DISPLAY_MODE_ON },
        { "display", &(state->display),   "off", IR_DISPLAY_MODE_OFF },
        /***************************************************************/
        {   "swing", &(state->swing),      "on", IR_SWING_MODE_ON },
        {   "swing", &(state->swing),     "off", IR_SWING_MODE_OFF },
        /***************************************************************/
        {     "ion", &(state->ion),        "on", IR_ION_MODE_ON },
        {     "ion", &(state->ion),       "off", IR_ION_MODE_OFF },
        /***************************************************************/
    };

    for (i=0; i<N_ELEMENTS(table); i++) {
        if (strcmp(key, "temperature") == 0) {
            state->temperature = atoi(value);
        } else if (strcmp(key, table[i].key) == 0) {
            if (strcmp(value, table[i].value_name) == 0)
                *table[i].value_dest = table[i].value_data;
            continue;
        }
    }

    /* printf("%02X %d°C %02X %02X %02X %02X %02X\n",
           state->mode, state->temperature, state->fan,
           state->swing, state->energy, state->ion, state->display); */
}

static uint8_t checksum (uint8_t *buf) {
    /* 34 - ones sum */
    uint8_t acc,i,j;
    acc = 34;
    for (i=0; i<7;i++)
        for (j=0; j<8; j++) {
            acc -= (buf[i] & 1<<j)>>j;
        }
    return acc;
}

void parse(char *msg, uint8_t *cmd)
{
    char key[20], value[20];
    char *next = msg;
    uint8_t i;
    ACState state;
    memset(&state, 0x00, sizeof(ACState));

    do {
        memset(key, 0, N_ELEMENTS(key));
        memset(value, 0, N_ELEMENTS(value));

        next = get_pair(next, key, value);

        os_printf("%s=>%s; ", key, value);
        parse_pair(key, value, &state);
    } while (next != NULL);
    os_printf("\n");

    uint8_t template[7] = { 0x01, 0x02, 0x0E, 0x00, 0x03, 0x00, 0x00 };
    memcpy(cmd, template, 7);

    /* FIXME: properly check allowed and invalid states */
    if (state.mode == IR_AC_MODE_FAN) state.temperature = 24;
    else if (state.mode == IR_AC_MODE_AUTO) state.temperature = 25;
    else if (state.mode == IR_AC_MODE_COOL)
        state.temperature = CLAMP(state.temperature, 18, 30);
    else state.temperature = CLAMP(state.temperature, 16, 30);

    state.temperature = (state.temperature - 16) << 4;

    if (state.mode != IR_AC_MODE_COOL && state.energy == IR_ENERGY_MODE_SAVE)
        state.energy = IR_ENERGY_MODE_NORMAL;
    if (state.energy == IR_ENERGY_MODE_SAVE) {
        state.temperature = 26;
        state.swing = IR_SWING_MODE_ON;
    } else if (state.energy == IR_ENERGY_MODE_TURBO) {
        state.fan = IR_FAN_MODE_AUTO;
    }


    cmd[2] |= state.swing;
    cmd[3] |= state.display | state.energy;
    cmd[4] |= state.temperature;
    cmd[5] |= state.mode | state.fan;
    cmd[6] |= state.power | state.ion;
    cmd[1] |= checksum(cmd) << 4;

    os_printf("Command: ");
    for (i=0; i<7; i++) {
        os_printf("%02X ", cmd[i]);
    }
    os_printf("\n");

}

#ifdef _TEST__MODE____

void main (void)
{
    char *test;
    uint8_t i;

    char *payloads[] = {
        "power=off, mode=warm,temperature=27,fan=auto,swing=off,energy=normal,ion=off,display=on",
        "power=on, mode=warm,temperature=27,fan=auto,swing=off,energy=normal,ion=off,display=on",
        "power=on, mode=cool,temperature=10,fan=high,swing=on,energy=turbo,ion=on,display=off",
    };

    uint8_t command[7];
    for (i=0; i<N_ELEMENTS(payloads); i++) {
        test = chomp(payloads[i], strlen(payloads[i]));
        parse(test, command);
    }
}

#endif
