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

MQTT_Client mqttClient;

static volatile os_timer_t some_timer;


void done (void* data) {
    uint8_t *cmd = (uint8_t *) data;
    uint8_t i;

    os_printf("Free heap: %d\n", system_get_free_heap_size());
}


void ICACHE_FLASH_ATTR
wifi_callback( System_Event_t *evt )
{
    os_printf( "%s: %d\n", __FUNCTION__, evt->event );

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
  MQTT_Publish(client, "/samsungac/info", "hey!", 6, 0, 0);
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

    uint8_t cmd[7];
    char *payload;

    if (os_strncmp(topic, IR_TOPIC, topic_len) == 0) {
        payload = chomp(data, data_len);
        os_printf("payload: %s\n", payload);
        parse(payload, cmd);
    }

    ir_send_cmd(cmd);

    os_free(payload);
}


//Init function
void ICACHE_FLASH_ATTR
user_init()
{
    static struct station_config config;

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

    if ( !MQTT_InitClient(&mqttClient, MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, MQTT_KEEPALIVE, MQTT_CLEAN_SESSION) )
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
}
