/*
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

/* parse mqtt payloads into a samsung ac state */

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
    uint64_t power;
    uint64_t mode;
    uint64_t temperature;
    uint64_t fan;
    uint64_t swing;
    uint64_t energy;
    // uint8_t ion;
    // uint8_t display;
} ACState;

typedef struct {
    char     *key;
    uint64_t  *value_dest;
    char     *value_name;
    uint64_t   value_data;
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
        /* {    "mode", &(state->mode),      "fan", IR_AC_MODE_FAN }, */
        {    "mode", &(state->mode),     "warm", IR_AC_MODE_WARM },
        /***************************************************************/
        {     "fan", &(state->fan),      "auto", IR_FAN_MODE_AUTO },
        {     "fan", &(state->fan),       "low", IR_FAN_MODE_LOW },
        {     "fan", &(state->fan),       "mid", IR_FAN_MODE_MID },
        {     "fan", &(state->fan),      "high", IR_FAN_MODE_HIGH },
        /* {     "fan", &(state->fan),   "natural", IR_FAN_MODE_NATURAL }, */
        /***************************************************************/
        {  "energy", &(state->energy), "normal", IR_ENERGY_MODE_NORMAL },
        /* {  "energy", &(state->energy),  "sleep", IR_ENERGY_MODE_SLEEP }, */
        {  "energy", &(state->energy),  "turbo", IR_ENERGY_MODE_TURBO },
        // {  "energy", &(state->energy),   "save", IR_ENERGY_MODE_SAVE },
        /***************************************************************/
        /* { "display", &(state->display),    "on", IR_DISPLAY_MODE_ON }, */
        /* { "display", &(state->display),   "off", IR_DISPLAY_MODE_OFF }, */
        /***************************************************************/
        {   "swing", &(state->swing),      "on", IR_SWING_MODE_ON },
        {   "swing", &(state->swing),     "off", IR_SWING_MODE_OFF },
        {   "swing", &(state->swing),    "blow", IR_SWING_MODE_BLOW },
        /***************************************************************/
        /* {     "ion", &(state->ion),        "on", IR_ION_MODE_ON }, */
        /* {     "ion", &(state->ion),       "off", IR_ION_MODE_OFF }, */
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
    for (i=0; i<cmd_len;i++)
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

    os_printf("PARSER: ");
    do {
        memset(key, 0, N_ELEMENTS(key));
        memset(value, 0, N_ELEMENTS(value));

        next = get_pair(next, key, value);

        os_printf("%s=>%s; ", key, value);
        parse_pair(key, value, &state);
    } while (next != NULL);
    os_printf("\n");

    uint8_t template[cmd_len] = {0x52,0xAE,0xC3,0x26,0xD9,0xFF,0x00,0x0f,0x00,0x08,0x00};
    memcpy(cmd, template, cmd_len);

    /* defaults */
    if (state.power == 0) state.power = IR_POWER_MODE_OFF;
    if (state.mode == 0) state.mode = IR_AC_MODE_AUTO;
    if (state.fan == 0) state.fan = IR_FAN_MODE_AUTO;
    if (state.energy == 0) state.energy = IR_ENERGY_MODE_NORMAL;
    /* if (state.display == 0) state.display = IR_DISPLAY_MODE_OFF; */
    if (state.swing == 0) state.swing = IR_SWING_MODE_OFF;
    /* if (state.ion == 0) state.ion = IR_ION_MODE_OFF; */

    if (state.energy == IR_ENERGY_MODE_TURBO) {
        state.fan = IR_FAN_MODE_HIGH;
    }

    /* FIXME: properly check allowed and invalid states */
    /* if (state.mode == IR_AC_MODE_FAN) state.temperature = 24; */
    /* else if (state.mode == IR_AC_MODE_AUTO) state.temperature = 25; */
    /* else if (state.mode == IR_AC_MODE_COOL) */
    /*     state.temperature = CLAMP(state.temperature, 18, 30); */
    /* else state.temperature = CLAMP(state.temperature, 16, 30); */

    uint64_t tail = (cmd[cmd_len-4] << 24) | (cmd[cmd_len-3] << 16) | (cmd[cmd_len-2] << 8) | cmd[cmd_len-1];

    state.temperature = CLAMP(state.temperature, 18, 30);

    tail |= state.mode;
    tail |= state.fan;
    tail |= (state.temperature - 18 + 1) << 4;
    tail |= (30 - state.temperature + 2) << 12;

    tail ^= state.swing;
    tail ^= state.energy;
    tail ^= state.power;

    /* if (state.mode != IR_AC_MODE_COOL && state.energy == IR_ENERGY_MODE_SAVE) */
    /*     state.energy = IR_ENERGY_MODE_NORMAL; */
    /* if (state.energy == IR_ENERGY_MODE_SAVE) { */
    /*     state.temperature = 26; */
    /*     state.swing = IR_SWING_MODE_ON; */
    cmd[cmd_len-4] = (tail >> 24) & 0xFF;
    cmd[cmd_len-3] = (tail >> 16) & 0xFF;
    cmd[cmd_len-2] = (tail >> 8) & 0xFF;
    cmd[cmd_len-1] = tail & 0xFF;
}

#ifdef _TEST__MODE____

void main (void)
{
    char *test;
    uint8_t i;

    char *payloads[] = {
        "",
        "default",
        "power=off, mode=warm,temperature=27,fan=auto,swing=off,energy=normal,ion=off,display=on",
        "power=on, mode=warm,temperature=27,fan=auto,swing=off,energy=normal,ion=off,display=on",
        "power=on, mode=cool,temperature=10,fan=high,swing=on,energy=turbo,ion=on,display=off",
        "power=off",
        "power=on",
    };

    uint8_t command[cmd_len];
    for (i=0; i<N_ELEMENTS(payloads); i++) {
        test = chomp(payloads[i], strlen(payloads[i]));
        parse(test, command);
    }
}

#endif
