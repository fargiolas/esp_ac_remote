#include "ets_sys.h"
#include "gpio.h"
#include "os_type.h"
#define USE_US_TIMER
#include "osapi.h"
#include "mem.h"
#include "user_config.h"
#include "user_interface.h"

#include "user_config.local.h"

#include "ir_driver.h"
#include "mqtt.h"

MQTT_Client mqttClient;


static volatile os_timer_t some_timer;

void some_timerfunc(void *arg)
{
    uint8_t i;

    uint8_t sator[7] = { 0x01, 0x72, 0x0F, 0x00, 0x70, 0x11, 0x01 };
    uint8_t arepo[7] = { 0x01, 0xC2, 0xFE, 0xF1, 0x23, 0x15, 0xC0 };

    uint8_t test[7] = { 0x01, 0x82, 0xFE, 0xF1, 0xE3, 0x45, 0xF0 };
    // uint8_t test[7] = { 0x01, 0x92, 0xFE, 0xF7, 0x23, 0x11, 0xF0 };

    os_printf("sending burst\n");


    uint8_t cmd[7];
    os_printf("MODE: warm, FAN: auto, temperature: %d\n", 27);
    build_command(cmd, IR_AC_MODE_WARM, 27, IR_FAN_MODE_AUTO,
                  IR_SWING_MODE_OFF, IR_POWER_MODE_NORMAL,
                  IR_ION_MODE_OFF, IR_DISPLAY_MODE_ON);

//    ir_send_cmd(cmd);
//    ir_send_cmd(test);
//    ir_send_cmd(cmd);
    ir_send_cmd(sator);
    ir_send_cmd(arepo);


    // send_cmd(test);
    // send_cmd_sm(test);
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
  MQTT_Subscribe(client, "/mqtt/topic/0", 0);
  MQTT_Subscribe(client, "/mqtt/topic/1", 1);
  MQTT_Subscribe(client, "/mqtt/topic/2", 2);

  MQTT_Publish(client, "/mqtt/topic/0", "hello0", 6, 0, 0);
  MQTT_Publish(client, "/mqtt/topic/1", "hello1", 6, 1, 0);
  MQTT_Publish(client, "/mqtt/topic/2", "hello2", 6, 2, 0);

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

static void ICACHE_FLASH_ATTR mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len)
{
  char *topicBuf = (char*)os_zalloc(topic_len + 1),
        *dataBuf = (char*)os_zalloc(data_len + 1);

  MQTT_Client* client = (MQTT_Client*)args;
  os_memcpy(topicBuf, topic, topic_len);
  topicBuf[topic_len] = 0;
  os_memcpy(dataBuf, data, data_len);
  dataBuf[data_len] = 0;
  INFO("Receive topic: %s, data: %s \r\n", topicBuf, dataBuf);
  os_free(topicBuf);
  os_free(dataBuf);
}


//Init function
void ICACHE_FLASH_ATTR
user_init()
{
    static struct station_config config;
    system_timer_reinit();

    wifi_station_set_hostname("esp-ac-remote");
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
    //MQTT_InitClient(&mqttClient, "client_id", "user", "pass", 120, 1);
    MQTT_InitLWT(&mqttClient, "/lwt", "offline", 0, 0);
    MQTT_OnConnected(&mqttClient, mqttConnectedCb);
    MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
    MQTT_OnPublished(&mqttClient, mqttPublishedCb);
    MQTT_OnData(&mqttClient, mqttDataCb);

    ir_init();

    //Disarm timer
    os_timer_disarm(&some_timer);

    //Setup timer
    os_timer_setfn(&some_timer, (os_timer_func_t *)some_timerfunc, NULL);

    //Arm the timer
    //&some_timer is the pointer
    //1000 is the fire time in ms
    //0 for once and 1 for repeating
    os_timer_arm(&some_timer, 5000, 1);
}
