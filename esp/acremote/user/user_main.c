#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "user_config.h"
#include "user_interface.h"

#define HEADER_MARK 3000
#define HEADER_SPACE 9000
#define SPACE_BIT_THRESHOLD 1000

#define ZERO_SPACE 500 /* 450-470 */
#define ONE_SPACE 1500 /* 1450 */
#define BIT_MARK  500 /* 570 */

#define TICK 13

#define user_procTaskPrio        0
#define user_procTaskQueueLen    1
os_event_t    user_procTaskQueue[user_procTaskQueueLen];
static void user_procTask(os_event_t *events);

static volatile os_timer_t some_timer;

#define DEFAULT_TOLERANCE 250
#define _APPROX(x, a, tol) (((x) <= ((a)+(tol))) && ((x) >= ((a)-(tol))))
#define APPROX(x, a) _APPROX((x), (a), (DEFAULT_TOLERANCE))


void mark(uint16_t len) {
    uint16_t i;
    for(i=0; i<len/TICK/2; i++) {
        gpio_output_set(BIT4, 0, BIT4, 0);
        os_delay_us(TICK);
        gpio_output_set(0, BIT4, BIT4, 0);
        os_delay_us(TICK);
    }
}

void space(uint16_t len) {
    gpio_output_set(0, BIT4, BIT4, 0);
    os_delay_us(len);
}

void send_cmd(uint8_t *buf) {
    uint8_t i,j;

    mark(HEADER_MARK);
    space(HEADER_SPACE);

    for(i=0; i<7; i++) {
        for (j=0; j<8; j++) {
            mark(BIT_MARK);
            if ((buf[i] & 1<<j)>>j) {
                space(ONE_SPACE);
            }
            else {
                space(ZERO_SPACE);
            }
        }
    }

    mark(BIT_MARK);
    space(ONE_SPACE);
}


void some_timerfunc(void *arg)
{
    uint8_t i;
    // uint8_t sator[7] = { 0x01, 0x72, 0x0F, 0x00, 0x70, 0x11, 0x01 };
    // uint8_t arepo[7] = { 0x01, 0xC2, 0xFE, 0xF1, 0x23, 0x15, 0xC0 };

    uint8_t test[7] = { 0x01, 0x82, 0xFE, 0xF1, 0xD3, 0x15, 0xF0 };


    os_printf("sending burst\n");

    send_cmd(test);

    return;
    //Do blinky stuff
    if (GPIO_REG_READ(GPIO_OUT_ADDRESS) & BIT4)
    {
        //Set GPIO2 to LOW
        gpio_output_set(0, BIT4, BIT4, 0);
        for (i=0; i<100; i++) {
            os_delay_us(1000);
        }
        os_printf("now I'm here\n");
    }
    else
    {
        //Set GPIO2 to HIGH
        gpio_output_set(BIT4, 0, BIT4, 0);
        for (i=0; i<100; i++) {
            os_delay_us(1000);
        }
        os_printf("now I'm there\n");
    }
}

//Do nothing function
static void ICACHE_FLASH_ATTR
user_procTask(os_event_t *events)
{
    os_delay_us(10);
}



void wifi_callback( System_Event_t *evt )
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
    os_printf( "%s\n", __FUNCTION__ );

    wifi_station_set_hostname("esp-ac-remote");
    wifi_set_opmode_current(STATION_MODE);

    // Initialize the GPIO subsystem.
    gpio_init();

    config.bssid_set = 0;
    os_memcpy(&config.ssid, "omissis", 32);
    os_memcpy(&config.password, "youwouldliketoknow", 64);
    wifi_station_set_config(&config);

    wifi_set_event_handler_cb(wifi_callback);

    //Set GPIO2 to output mode
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO4_U, FUNC_GPIO4);

    //Set GPIO2 low
    space(100);

    //Disarm timer
    os_timer_disarm(&some_timer);

    //Setup timer
    os_timer_setfn(&some_timer, (os_timer_func_t *)some_timerfunc, NULL);

    //Arm the timer
    //&some_timer is the pointer
    //1000 is the fire time in ms
    //0 for once and 1 for repeating
    os_timer_arm(&some_timer, 2000, 1);

    //Start os task
    system_os_task(user_procTask, user_procTaskPrio,user_procTaskQueue, user_procTaskQueueLen);
}
