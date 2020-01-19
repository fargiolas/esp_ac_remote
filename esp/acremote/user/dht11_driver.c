/*
 *  DHT11 Driver
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

#define _gpio_pin  13
#define _gpio_func FUNC_GPIO13
#define _gpio_mux  PERIPHS_IO_MUX_MTCK_U

#define DHT11_INPUT() GPIO_DIS_OUTPUT(_gpio_pin)
#define DHT11_LOW() GPIO_OUTPUT_SET(_gpio_pin, 0)
#define DHT11_HIGH() GPIO_OUTPUT_SET(_gpio_pin, 1)
#define DHT11_READ() GPIO_INPUT_GET(_gpio_pin)

#define MAXTIMINGS 10000
#define DHT_MAXCOUNT 32000
#define BREAKTIME 30

#define DHT_PIN 13

void ICACHE_FLASH_ATTR dht11_init() {
    PIN_FUNC_SELECT(_gpio_mux, _gpio_func);
    PIN_PULLUP_EN(_gpio_mux);
    GPIO_DIS_OUTPUT(_gpio_pin);
}

static inline void delay_ms(int sleep) { 
    os_delay_us(1000 * sleep); 
}

int8_t ICACHE_FLASH_ATTR dht11_read() {
    int counter = 0;
    int laststate = 1;
    int i = 0;
    int bits_in = 0;

    int8_t data[100];

    int timings[100];

    data[0] = data[1] = data[2] = data[3] = data[4] = 0;

    // Wake up device, 250ms of high
    DHT11_HIGH();
    os_delay_us(20000);

    // Hold low for 20ms
    DHT11_LOW();
    os_delay_us(20000);

    //disable interrupts, start of critical section
    ETS_INTR_LOCK();

    // High for 40ms
    DHT11_HIGH();
    os_delay_us(40);
    DHT11_INPUT();

    // wait for pin to drop?
    while (DHT11_READ() == 1 && i < DHT_MAXCOUNT) {
        if (i >= DHT_MAXCOUNT) {
            goto fail;
        }
        i++;
    }

    for (i = 0; i < MAXTIMINGS; i++) {
        // Count high time (in approx us)
        counter = 0;
        while (DHT11_READ() == laststate) {
            counter++;
            os_delay_us(1);
            if (counter > 500)
                break;
        }
        laststate = DHT11_READ();

        if (counter > 500)
            break;

        timings[i] = counter;

        // store data after 3 reads
        if ((i > 3) && (i % 2 == 0)) {
            // shove each bit into the storage bytes
            data[bits_in / 8] <<= 1;
            if (counter > BREAKTIME) {
                //os_printf("1");
                data[bits_in / 8] |= 1;
            } else {
                //os_printf("0");
            }
            bits_in++;
        }
    }

    //Re-enable interrupts, end of critical section
    ETS_INTR_UNLOCK();

    if (bits_in < 40) {
        os_printf("Got too few bits: %d should be at least 40", bits_in);
        goto fail;
    }


    int checksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;

    os_printf("DHT: %02x %02x %02x %02x [%02x] CS: %02x\n", data[0], data[1], data[2], data[3], data[4], checksum);

    if (data[4] != checksum) {
        os_printf("Checksum was incorrect after %d bits. Expected %d but got %d",
                  bits_in, data[4], checksum);
        goto fail;
    }

    os_printf("temperature: %d\n", data[2]);
    os_printf("humidity: %d\n", data[0]);

    return data[0];
fail:

    os_printf("Failed to get reading, dying\n");

    return -1;
}

static void ICACHE_FLASH_ATTR temp2string(uint16_t temp, char *buf) {
    int8_t integ;
    uint16_t frac;
    integ = temp >> 4;
    frac = ((temp & 0xf)>>3) * 5; /* assuming 9bit res */
    os_sprintf(buf, "%d.%u", integ, frac);
}
