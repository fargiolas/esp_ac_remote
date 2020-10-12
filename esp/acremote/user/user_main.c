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

#include "ets_sys.h"
#include "gpio.h"
#include "os_type.h"
#include "osapi.h"
#include "mem.h"
#include "user_config.h"
#include "user_interface.h"

#define DEBUG_ON
#include "user_config.local.h"

#include "utils.h"
#include "mqtt.h"
#include "ir_driver.h"
#include "ds18b20_driver.h"
#include "dht11_driver.h"
#include "i2c_master.h"
#include "bme280.h"
#include "printf.h"

MQTT_Client mqttClient;

static os_timer_t temperature_timer;
static os_timer_t humidity_timer;
static os_timer_t bme_timer;

static struct bme280_dev bme;
static uint8_t bme280_i2c_addr;

#ifdef DEMO_MODE

static os_timer_t demo_timer;
static uint8_t demo_counter = 0;
static uint64_t demo_interval = 5000;


void ICACHE_FLASH_ATTR demo_cb (void *userdata) {
    char *commands[] = {
        /* "power=off, mode=cool, temperature=30", */
        /* "power=off, mode=cool, temperature=26", */
        /* "power=off, mode=cool, temperature=18", */
        "power=off, mode=warm, temperature=30",
        /* "power=off, mode=dry", */
        /* "power=off, mode=auto, temperature=28, swing=off", */
        /* "power=off, mode=auto, temperature=28, swing=blow", */
        /* "power=off", */
    };

    uint8_t cmd[cmd_len];

    os_printf("DEMO: sending command %s\n", commands[demo_counter]);
    parse(commands[demo_counter], cmd);
    ir_send_cmd(cmd);
    demo_counter++;
    demo_counter %= N_ELEMENTS(commands);
}

#endif /* DEMO_MODE */

void user_delay_us(uint32_t period, void *intf_ptr)
{
    os_delay_us(period);
}

int8_t user_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    uint8_t dev_id = *((uint8_t *) intf_ptr);

    i2c_master_start();
    i2c_master_writeByte(dev_id << 1);
    if (i2c_master_getAck()) {
        i2c_master_stop();
        return -1;
    }
    i2c_master_writeByte(reg_addr);
    if (i2c_master_getAck()) {
        i2c_master_stop();
        return -1;
    }
    i2c_master_stop();
    i2c_master_start();
    i2c_master_writeByte((dev_id << 1) | 1);
    if (i2c_master_getAck()) {
        i2c_master_stop();
        return -1;
    }

    while (len--) {
        *reg_data++ = i2c_master_readByte();
        i2c_master_setAck(len == 0);
    }

    i2c_master_stop();
    return 0;
}

int8_t user_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    uint8_t dev_id = *((uint8_t *) intf_ptr);

    i2c_master_start();
    i2c_master_writeByte(dev_id << 1);
    if (i2c_master_getAck()) {
        i2c_master_stop();
        return -1;
    }
    i2c_master_writeByte(reg_addr);
    if (i2c_master_getAck()) {
        i2c_master_stop();
        return -1;
    }

    while (len--) {
        i2c_master_writeByte(*reg_data++);
        if (i2c_master_getAck()) {
            i2c_master_stop();
            return -1;
        }
    }

    i2c_master_stop();
    return 0;
}

void ICACHE_FLASH_ATTR
wifi_callback( System_Event_t *evt )
{
    os_printf("%s: %d\n", __FUNCTION__, evt->event);

    switch ( evt->event )
    {
        case EVENT_STAMODE_CONNECTED:
        {
            os_printf("connect to ssid %s, channel %d\n",
                        evt->event_info.connected.ssid,
                        evt->event_info.connected.channel);
            break;
        }

        case EVENT_STAMODE_DISCONNECTED:
        {
            os_printf("disconnect from ssid %s, reason %d\n",
                        evt->event_info.disconnected.ssid,
                        evt->event_info.disconnected.reason);

            deep_sleep_set_option( 0 );
            system_deep_sleep( 60 * 1000 * 1000 );  // 60 seconds
            break;
        }

        case EVENT_STAMODE_GOT_IP:
        {
            os_printf("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
                        IP2STR(&evt->event_info.got_ip.ip),
                        IP2STR(&evt->event_info.got_ip.mask),
                        IP2STR(&evt->event_info.got_ip.gw));
            os_printf("\n");

            MQTT_Connect(&mqttClient);

            break;
        }

        default:
        {
            break;
        }
    }
}

static void ICACHE_FLASH_ATTR mqttConnectedCb(uint32_t *args)
{
  MQTT_Client* client = (MQTT_Client*)args;
  INFO("MQTT: Connected\r\n");
  MQTT_Subscribe(client, "/samsungac/remote", 2);
  MQTT_Publish(client, "/samsungac/info", "hey!", 4, 0, 0);
}

static void ICACHE_FLASH_ATTR mqttDisconnectedCb(uint32_t *args)
{
  MQTT_Client* client = (MQTT_Client*)args;
  INFO("MQTT: Disconnected\r\n");
}

static void ICACHE_FLASH_ATTR mqttPublishedCb(uint32_t *args)
{
  MQTT_Client* client = (MQTT_Client*)args;
  INFO("MQTT: Published\r\n");
}


#define IR_TOPIC "/samsungac/remote"

char *chomp(const char *str, int len);
static void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len)
{
    MQTT_Client* client = (MQTT_Client*)args;

    uint8_t cmd[cmd_len];
    char *payload;

    if (os_strncmp(topic, IR_TOPIC, topic_len) == 0) {
        payload = chomp(data, data_len);
        os_printf("MQTT payload: %s\n", payload);
        parse(payload, cmd);
    }

    ir_send_cmd(cmd);

    os_free(payload);
}

void ICACHE_FLASH_ATTR temperature_cb (void *userdata) {
    uint8_t integ;
    uint16_t frac;
    char buf[10];
    char payload[256];

    MQTT_Client* client = (MQTT_Client*)userdata;

    ds18b20_get_temp(buf);
    os_printf("TEMPERATURE: %s °C\n", buf);


    MQTT_Publish(client, "/samsungac/temperature", buf, os_strlen(buf), 0, 0);
}

void ICACHE_FLASH_ATTR humidity_cb (void *userdata) {
    int8_t humidity = dht11_read();
    char buf[10];

    MQTT_Client* client = (MQTT_Client*)userdata;

    os_sprintf(buf, "%d", humidity);
    os_printf("HUMIDITY: %s%%\n", buf);
    MQTT_Publish(client, "/samsungac/humidity", buf, os_strlen(buf), 0, 0);
}

void ICACHE_FLASH_ATTR bme_cb (void *userdata) {
    int j;
    char T[64];
    char RH[64];
    char P[64];

    uint32_t req_delay;
    int8_t rslt = BME280_OK;

    rslt = bme280_set_sensor_mode(BME280_FORCED_MODE, &bme);
    if (rslt != BME280_OK){
        os_printf("bme: set forced mode failed, (code %d).\n", rslt);
        return;
    }

    req_delay = bme280_cal_meas_delay(&bme.settings);
    bme.delay_us(req_delay, bme.intf_ptr);

    struct bme280_data comp_data;

    rslt = bme280_get_sensor_data(BME280_ALL, &comp_data, &bme);
    if (rslt != BME280_OK) {
        os_printf("Failed to get sensor data (code %d).\n", rslt);
        return;
    }

    sprintf(T, "%.2f", comp_data.temperature);
    sprintf(RH, "%.2f", comp_data.humidity);
    sprintf(P, "%.2f", comp_data.pressure);

    os_printf("%s C, %s %% , %s Pa\n", T, RH, P);


    MQTT_Client* client = (MQTT_Client*)userdata;

    MQTT_Publish(client, "/samsungac/bme/temperature", T, os_strlen(T), 0, 0);
    MQTT_Publish(client, "/samsungac/bme/humidity", RH, os_strlen(RH), 0, 0);
    MQTT_Publish(client, "/samsungac/bme/pressure", P, os_strlen(P), 0, 0);
}

void ICACHE_FLASH_ATTR bme280_setup(void)
{
    int8_t rslt = BME280_OK;

    bme280_i2c_addr = BME280_I2C_ADDR_PRIM;
    bme.intf = BME280_I2C_INTF;
    bme.intf_ptr = &bme280_i2c_addr;
    bme.read = user_i2c_read;
    bme.write = user_i2c_write;
    bme.delay_us = user_delay_us;

    if (bme280_init(&bme) != BME280_OK) {
        os_printf("bme: absent\n");
        return;
    }

    bme.settings.osr_h = BME280_OVERSAMPLING_1X;
    bme.settings.osr_p = BME280_OVERSAMPLING_1X;
    bme.settings.osr_t = BME280_OVERSAMPLING_1X;
    bme.settings.filter = BME280_FILTER_COEFF_OFF;

    int settings_sel = BME280_OSR_PRESS_SEL | BME280_OSR_TEMP_SEL |
        BME280_OSR_HUM_SEL | BME280_FILTER_SEL;

    rslt = bme280_set_sensor_settings(settings_sel, &bme);
    if (rslt != BME280_OK) {
        os_printf("bme: set sensor settings failed, (code %d).\n", rslt);
        return;
    }

    rslt = bme280_set_sensor_mode(BME280_FORCED_MODE, &bme);
    if (rslt != BME280_OK){
        os_printf("bme: set forced mode failed, (code %d).\n", rslt);
        return;
    }


    os_timer_disarm(&bme_timer);
    os_timer_setfn(&bme_timer, (os_timer_func_t *) bme_cb, &mqttClient);
    os_timer_arm(&bme_timer, 5000, 1);
}

//Init function
void ICACHE_FLASH_ATTR
user_init()
{
    static struct station_config config;

    char client_id[24];
    os_sprintf(client_id, "esp_ac_remote_%d", system_get_chip_id());

    wifi_station_set_hostname("esp_ac_remote");
    wifi_set_opmode_current(STATION_MODE);

    // Initialize the GPIO subsystem.
    gpio_init();

    config.bssid_set = 0;
    os_memcpy(&config.ssid, STA_SSID, 32);
    os_memcpy(&config.password, STA_PASS, 64);
    wifi_station_set_config(&config);

    wifi_set_event_handler_cb(wifi_callback);

    MQTT_InitConnection(&mqttClient, MQTT_HOST, MQTT_PORT, DEFAULT_SECURITY);

    if ( !MQTT_InitClient(&mqttClient, client_id, NULL, NULL, MQTT_KEEPALIVE, MQTT_CLEAN_SESSION) )
    {
        INFO("Failed to initialize properly. Check MQTT version.\r\n");
        return;
    }
    MQTT_InitLWT(&mqttClient, "/samsungac/info", "bye!", 0, 0);
    MQTT_OnConnected(&mqttClient, mqttConnectedCb);
    MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
    MQTT_OnPublished(&mqttClient, mqttPublishedCb);
    MQTT_OnData(&mqttClient, mqttDataCb);

    ir_init();
    ds18b20_init();
    dht11_init();

    os_timer_disarm(&temperature_timer);
    os_timer_setfn(&temperature_timer, (os_timer_func_t *)temperature_cb, &mqttClient);
    os_timer_arm(&temperature_timer, 5000, 1);

    os_timer_disarm(&humidity_timer);
    os_timer_setfn(&humidity_timer, (os_timer_func_t *)humidity_cb, &mqttClient);
    os_timer_arm(&humidity_timer, 5000, 1);

    i2c_master_gpio_init();

    bme280_setup();

#ifdef DEMO_MODE
    os_timer_disarm(&demo_timer);
    os_timer_setfn(&demo_timer, (os_timer_func_t *) demo_cb, NULL);
    os_timer_arm(&demo_timer, demo_interval, 1);
#endif /* DEMO_MODE */
}
