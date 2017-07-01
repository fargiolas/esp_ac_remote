/*
 *  DS18B20 Driver
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

#include "utils.h"

#define _gpio_pin  5
#define _gpio_func FUNC_GPIO5
#define _gpio_mux  PERIPHS_IO_MUX_GPIO5_U

/* port from my AVR driver */
/* changing these defines and time apis should be enough */
/*
  #define DS18B20_INPUT() DS18B20_DDR &= ~(1 << DS18B20_DDR_BIT)
  #define DS18B20_OUTPUT() DS18B20_DDR |= (1 << DS18B20_DDR_BIT)
  #define DS18B20_LOW() DS18B20_PORT &= ~(1 << DS18B20_DQ)
  #define DS18B20_HIGH() DS18B20_PORT |= (1 << DS18B20_DQ)
*/

#define DS18B20_INPUT() GPIO_DIS_OUTPUT(_gpio_pin)
#define DS18B20_LOW() GPIO_OUTPUT_SET(_gpio_pin, 0)
#define DS18B20_HIGH() GPIO_OUTPUT_SET(_gpio_pin, 1)
#define DS18B20_READ() GPIO_INPUT_GET(_gpio_pin)


#define DS18B20_CMD_READROM      0x33
#define DS18B20_CMD_SEARCHROM    0xF0
#define DS18B20_CMD_MATCHROM     0x55
#define DS18B20_CMD_ALARMSEARCH  0xEC
#define DS18B20_CMD_SKIPROM      0xCC
#define DS18B20_CMD_CONVERTT     0x44
#define DS18B20_CMD_RSCRATCHPAD  0xBE
#define DS18B20_CMD_WSCRATCHPAD  0x4E
#define DS18B20_CMD_CSCRATCHPAD  0x48
#define DS18B20_RES_9BIT         0x1F
#define DS18B20_RES_10BIT        0x3F
#define DS18B20_RES_11BIT        0x5F
#define DS18B20_RES_12BIT        0x7F

#define CMD(a) DS18B20_CMD_##a

#define TEMPRES 625
#define BUFSZ 16


void ICACHE_FLASH_ATTR ds18b20_init()
{
    PIN_FUNC_SELECT(_gpio_mux, _gpio_func);
    PIN_PULLUP_EN(_gpio_mux);
    GPIO_DIS_OUTPUT(_gpio_pin);
}

/* Reset and detect presence */
static uint8_t ICACHE_FLASH_ATTR ds18b20_reset() {
    uint8_t presence;

    /* pull down bus */
    DS18B20_LOW();
    os_delay_us(480);

    /* release and wait */
    DS18B20_INPUT();
    os_delay_us(60);

    /* read: 0=found something */
    presence = DS18B20_READ();
    os_delay_us(240);

    return presence;
}

static void ICACHE_FLASH_ATTR ds18b20_write_slot(uint8_t bit)
{
    /* pull down bus and recover for 1us to initiate time slot */
    DS18B20_LOW();
    os_delay_us(1);

    /* the device will sample the bus for the next 15-60us */
    /* to write 1 release the bus */
    if (bit) DS18B20_INPUT();

    /* to write 0 keep it low for 60us before releasing */
    os_delay_us(60);
    DS18B20_INPUT();
}

static uint8_t ds18b20_read_slot(void)
{
    uint8_t bit = 0;

    /* pull down bus and recover for 1us to initiate time slot */
    DS18B20_LOW();
    os_delay_us(1);

    /* release and wait */
    DS18B20_INPUT();
    os_delay_us(15);

    /* read */
    if (DS18B20_READ()) bit = 1;
    os_delay_us(45);

    return bit;
}

static void ICACHE_FLASH_ATTR ds18b20_write(uint8_t byte) {
    uint8_t i = 8;

    while(i--) {
        ds18b20_write_slot(byte & 0x01);
        byte >>= 1;
    }
}

static uint8_t ICACHE_FLASH_ATTR ds18b20_read(void) {
    uint8_t i = 8;
    uint8_t byte = 0;

    while(i--) {
        byte |= (ds18b20_read_slot() << (7-i));
    }

    return byte;
}

static uint16_t ICACHE_FLASH_ATTR ds18b20_read_temp() {
    uint8_t temp_low, temp_high;
    uint16_t ret;

    ds18b20_reset();
    /* address all devices */
    ds18b20_write(CMD(SKIPROM));
    /* issue a single temperature conversion */
    ds18b20_write(CMD(CONVERTT));

    /* wait */
    /* 0=busy 1=conversion complete */
    while(!ds18b20_read_slot());

    ds18b20_reset();
    ds18b20_write(CMD(SKIPROM));
    /* read scratchpad */
    /* FIXME: this only works with a single slave */
    ds18b20_write(CMD(RSCRATCHPAD));

    temp_low = ds18b20_read();
    temp_high = ds18b20_read();

    ret = (temp_high << 8) + temp_low;

    return ret;
}

void ICACHE_FLASH_ATTR ds18b20_set_resolution(uint8_t bits) {
    uint8_t resolution;
    switch (bits) {
    case 9:  resolution = DS18B20_RES_9BIT; break;
    case 10: resolution = DS18B20_RES_10BIT; break;
    case 11: resolution = DS18B20_RES_11BIT; break;
    default: resolution = DS18B20_RES_12BIT;
    }

    ds18b20_reset();
    ds18b20_write(CMD(SKIPROM));
    ds18b20_write(CMD(WSCRATCHPAD));
    ds18b20_write(0);
    ds18b20_write(0);
    ds18b20_write(resolution);
    ds18b20_reset();

    ds18b20_write(CMD(SKIPROM));
    ds18b20_write(CMD(CSCRATCHPAD));
    os_delay_us(15000); /* uh uh */
    ds18b20_reset();
}


static void ICACHE_FLASH_ATTR temp2string(uint16_t temp, char *buf) {
    int8_t integ;
    uint16_t frac;
    integ = temp >> 4;
    frac = ((temp & 0xf)>>3) * 5; /* assuming 9bit res */
    os_sprintf(buf, "%d.%u", integ, frac);
}

char ICACHE_FLASH_ATTR *ds18b20_get_temp(char *buf) {
    temp2string(ds18b20_read_temp(), buf);
    return buf;
}

