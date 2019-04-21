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

#ifndef _IR_DRIVER_H
#define _IR_DRIVER_H

/* 0 here means "default template" */
#define IR_POWER_MODE_OFF 0
/* "on" toggle bits, assuming template is "off" by default */
#define IR_POWER_MODE_ON  ((1<<3) | (1<<11))

#define IR_SWING_MODE_OFF  0
#define IR_SWING_MODE_ON   ((1<<20) | (1<<28))
#define IR_SWING_MODE_BLOW ((1<<19) | (1<<27))

#define IR_ENERGY_MODE_NORMAL 0
#define IR_ENERGY_MODE_TURBO ((1<<30) | (1<<22))

#define IR_AC_MODE_AUTO 0x700
#define IR_AC_MODE_COOL 0x601
#define IR_AC_MODE_WARM 0x304
#define IR_AC_MODE_DRY  0x502

#define IR_FAN_MODE_AUTO 0xF0000000
#define IR_FAN_MODE_LOW  0xB0400000
#define IR_FAN_MODE_MID  0x90600000
#define IR_FAN_MODE_HIGH 0x70800000

#define cmd_len 11

typedef void (* cmd_done_func_t)();

void ir_init (void);
void ir_send_cmd (uint8_t *command);
void ir_send_cmd_full (uint8_t *command, cmd_done_func_t done_cb, void *done_data);
void ir_send_cmd_simple (uint8_t *command);

void build_command (uint8_t *cmd, uint8_t mode, uint8_t temperature,
                    uint8_t fan_speed, uint8_t swing,
                    uint8_t energy_mode, uint8_t ions,
                    uint8_t display);

#endif /* _IR_DRIVER_H */
