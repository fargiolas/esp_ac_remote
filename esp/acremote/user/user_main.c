#include "ets_sys.h"
#include "gpio.h"
#include "os_type.h"
#define USE_US_TIMER
#include "osapi.h"
#include "mem.h"
#include "user_config.h"
#include "user_interface.h"

#include "ir_driver.h"

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

    ir_send_cmd(cmd);
    ir_send_cmd(test);

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

            break;
        }

        default:
        {
            break;
        }
    }
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

    ir_init();

    //Disarm timer
    os_timer_disarm(&some_timer);

    //Setup timer
    os_timer_setfn(&some_timer, (os_timer_func_t *)some_timerfunc, NULL);

    //Arm the timer
    //&some_timer is the pointer
    //1000 is the fire time in ms
    //0 for once and 1 for repeating
    os_timer_arm(&some_timer, 2000, 0);
}
