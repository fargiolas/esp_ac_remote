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

MQTT_Client mqttClient;

static os_timer_t temperature_timer;
static os_timer_t humidity_timer;

#ifdef DEMO_MODE

static os_timer_t demo_timer;
static uint8_t demo_counter = 0;
static uint64_t demo_interval = 5000;

void ICACHE_FLASH_ATTR demo_cb (void *userdata) {
    const char *commands[] = {
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
    MQTT_Publish(client, "/samsungac/humidity", buf, os_strlen(buf), 0, 0);
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
    //MQTT_InitConnection(&mqttClient, "192.168.11.122", 1880, 0);

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


#ifdef DEMO_MODE
    os_timer_disarm(&demo_timer);
    os_timer_setfn(&demo_timer, (os_timer_func_t *)demo_cb, NULL);
    os_timer_arm(&demo_timer, demo_interval, 1);
#endif /* DEMO_MODE */
}
