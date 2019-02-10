//Copyright 2015 <>< Charles Lohr Under the MIT/x11 License, NewBSD License or
// ColorChord License.  You Choose.

#include "mem.h"
#include "c_types.h"
#include "user_interface.h"
#include "ets_sys.h"
#include "uart.h"
#include "osapi.h"
#include "espconn.h"
#include "esp82xxutil.h"
#include "ws2812_i2s.h"
#include "hpatimer.h"
#include "DFT32.h"
#include "ccconfig.h"
#include <embeddednf.h>
#include <embeddedout.h>
#include <commonservices.h>
#include "ets_sys.h"
#include "gpio.h"
//#define PROFILE
//#define DISABLE_SERVICE_UDP

#define PORT 7777
#define SERVER_TIMEOUT 1500
#define MAX_CONNS 5
#define MAX_FRAME 2000

#define procTaskPrio        0
#define procTaskQueueLen    1

struct CCSettings CCS;
static volatile os_timer_t some_timer;
static volatile os_timer_t udp_timer;
static struct espconn *pUdpServer;

#ifndef DISABLE_SERVICE_UDP
LOCAL struct espconn pUdpBroadcaster;
LOCAL esp_udp udp;
#endif

void EnterCritical();
void ExitCritical();

extern volatile uint8_t sounddata[HPABUFFSIZE];
extern volatile uint16_t soundhead;
uint16_t soundtail;

static uint8_t hpa_running = 0;
static uint8_t hpa_is_paused_for_wifi;
void ICACHE_FLASH_ATTR CustomStart( );

void ICACHE_FLASH_ATTR user_rf_pre_init()
{
}


//Call this once we've stacked together one full colorchord frame.
static void NewFrame()
{
	if( !COLORCHORD_ACTIVE ) return;

	//uint8_t led_outs[NUM_LIN_LEDS*3];
	int i;

	switch( COLORCHORD_OUTPUT_DRIVER )
	{
	case 0:
	case 6:
		HandleFrameInfo();
		UpdateLinearLEDs();
		break;
	case 1:
		HandleFrameInfo();
		UpdateAllSameLEDs();
		break;
	case 2: 
		UpdateRainbow();
		break;
	case 3: 
		UpdateSolid();
		break;
	case 4: 
		UpdateOff();
		break;
	case 5: 
		UpdateTwinkle();
		break;
	};

	//SendSPI2812( ledOut, NUM_LIN_LEDS );
	ws2812_push( ledOut, USE_NUM_LIN_LEDS * 3 );
}

os_event_t    procTaskQueue[procTaskQueueLen];
uint32_t samp_iir = 0;
int wf = 0;

//Tasks that happen all the time.

static void procTask(os_event_t *events)
{
	system_os_post(procTaskPrio, 0, 0 );

	if( COLORCHORD_ACTIVE && !hpa_running )
	{
		ExitCritical();
		hpa_running = 1;
	}

	if( !COLORCHORD_ACTIVE && hpa_running )
	{
		EnterCritical();
		hpa_running = 0;
	}
	
	//For profiling so we can see how much CPU is spent in this loop.
#ifdef PROFILE
	WRITE_PERI_REG( PERIPHS_GPIO_BASEADDR + GPIO_ID_PIN(0), 1 );
#endif
	while( soundtail != soundhead )
	{
		int32_t samp = sounddata[soundtail];
		samp_iir = samp_iir - (samp_iir>>10) + samp;
		samp = (samp - (samp_iir>>10))*16;
		samp = (samp * CCS.gINITIAL_AMP) >> 4;
		PushSample32( samp );
		soundtail = (soundtail+1)&(HPABUFFSIZE-1);

		wf++;
		if( wf == 128 )
		{
			NewFrame();
			wf = 0; 
		}
	}
#ifdef PROFILE
	WRITE_PERI_REG( PERIPHS_GPIO_BASEADDR + GPIO_ID_PIN(0), 0 );
#endif

	if( events->sig == 0 && events->par == 0 )
	{
		CSTick( 0 );
	}

}

//Timer event.
static void ICACHE_FLASH_ATTR myTimer(void *arg)
{
printf( "myTimer hpa_is_paused_for_wifi:%d printed_ip:%d\n", hpa_is_paused_for_wifi,printed_ip);

	CSTick( 1 );

	if( hpa_is_paused_for_wifi && printed_ip )
	{
		StartHPATimer(); //Init the high speed  ADC timer.
		hpa_running = 1;
		hpa_is_paused_for_wifi = 0; // only need to do once prevents unstable ADC
	}
//	uart0_sendStr(".");
//	printf( "%d/%d\n",soundtail,soundhead );
//	printf( "%d/%d\n",soundtail,soundhead );
//	uint8_t ledout[] = { 0x00, 0xff, 0xaa, 0x00, 0xff, 0xaa, };
//	ws2812_push( ledout, 6 );
}

#ifndef DISABLE_SERVICE_UDP
//UDP broadcast Timer event.
static void ICACHE_FLASH_ATTR udpTimer(void *arg)
{
	if (CCS.gCOLORCHORD_ACTIVE == 1) {
		uint8_t udpOut[USE_NUM_LIN_LEDS*3+1];
		os_memcpy(&udpOut[1], ledOut, USE_NUM_LIN_LEDS*3);
		udpOut[0] = 255;//udpOut[1] = 72;udpOut[2] = 73;udpOut[3] = 10;udpOut[4] = 13;

		espconn_send(&pUdpBroadcaster, udpOut, USE_NUM_LIN_LEDS*3+1);
		//err = espconn_delete(&pUdpBroadcaster);
	}
}
#endif

//Called when new packet comes in.
static void ICACHE_FLASH_ATTR udpserver_recv(void *arg, char *pusrdata, unsigned short len)
{
	struct espconn *pespconn = (struct espconn *)arg;
	uint8_t buffer[MAX_FRAME];
//	uint8_t ledout[] = { 0x00, 0xff, 0xaa, 0x00, 0xff, 0xaa, };
//	uart0_sendStr("X");
	ws2812_push( pusrdata+3, len );
}

void ICACHE_FLASH_ATTR charrx( uint8_t c )
{
	//Called from UART.
}


void ICACHE_FLASH_ATTR user_init(void)
{
	uart_init(BIT_RATE_115200, BIT_RATE_115200);
	int wifiMode = wifi_get_opmode();

	uart0_sendStr("\r\nColorChord\r\n");

//Uncomment this to force a system restore.
//	system_restore();

	CustomStart();

#ifdef PROFILE
	GPIO_OUTPUT_SET(GPIO_ID_PIN(0), 0);
#endif

	CSPreInit();

	SetServiceName( "colorchord" );
	AddMDNSName(    "esp82xx" );
	AddMDNSName(    "colorchord" );
	AddMDNSName(    "espcom" );
	AddMDNSService( "_colorchord._tcp",    "A Colorchord Webserver", WEB_PORT );
	AddMDNSService( "_espcom._udp",  "ESP82XX Comunication", COM_PORT );
	AddMDNSService( "_esp82xx._udp", "ESP82XX Backend",      BACKEND_PORT );

#ifndef DISABLE_SERVICE_UDP
	pUdpBroadcaster.type = ESPCONN_UDP;
	pUdpBroadcaster.state = ESPCONN_NONE;
	pUdpBroadcaster.proto.udp = &udp;
	IP4_ADDR((ip_addr_t *)pUdpBroadcaster.proto.udp->remote_ip, 255, 255, 255, 255);
	pUdpBroadcaster.proto.udp->remote_port = BROADCAST_PORT; // Remote port
	uint8_t err = espconn_create(&pUdpBroadcaster);

	pUdpServer = (struct espconn *)os_zalloc(sizeof(struct espconn));
	ets_memset( pUdpServer, 0, sizeof( struct espconn ) );
	espconn_create( pUdpServer );
	pUdpServer->type = ESPCONN_UDP;
	pUdpServer->proto.udp = (esp_udp *)os_zalloc(sizeof(esp_udp));
	pUdpServer->proto.udp->local_port = COM_PORT;
	espconn_regist_recvcb(pUdpServer, udpserver_recv);

	if( err || espconn_create( pUdpServer ) )
	{
		while(1) { uart0_sendStr( "\r\nFAULT\r\n" ); }
	}
#endif

	CSInit();

	//Add a process
	system_os_task(procTask, procTaskPrio, procTaskQueue, procTaskQueueLen);

	//Timer example
	os_timer_disarm(&some_timer);
	os_timer_setfn(&some_timer, (os_timer_func_t *)myTimer, NULL);
	os_timer_arm(&some_timer, 100, 1);

#ifndef DISABLE_SERVICE_UDP
	//UDP broadcast Timer
	os_timer_disarm(&udp_timer);
	os_timer_setfn(&udp_timer, (os_timer_func_t *)udpTimer, NULL);
	os_timer_arm(&udp_timer, 21, 1);
#endif

	//Set GPIO16 for Input
    WRITE_PERI_REG(PAD_XPD_DCDC_CONF,
                   (READ_PERI_REG(PAD_XPD_DCDC_CONF) & 0xffffffbc) | (uint32)0x1); 	// mux configuration for XPD_DCDC and rtc_gpio0 connection

    WRITE_PERI_REG(RTC_GPIO_CONF,
                   (READ_PERI_REG(RTC_GPIO_CONF) & (uint32)0xfffffffe) | (uint32)0x0);	//mux configuration for out enable

    WRITE_PERI_REG(RTC_GPIO_ENABLE,
                   READ_PERI_REG(RTC_GPIO_ENABLE) & (uint32)0xfffffffe);	//out disable

	InitColorChord(); //Init colorchord

	//Tricky: If we are in station mode, wait for that to get resolved before enabling the high speed timer.
	if( wifi_get_opmode() == 1 )
	{
		hpa_is_paused_for_wifi = 1;
	}
	else
	{
		StartHPATimer(); //Init the high speed  ADC timer.
		hpa_running = 1;
	}

	ws2812_init();

	// Attempt to make ADC more stable
	// https://github.com/esp8266/Arduino/issues/2070
	// see peripherals https://espressif.com/en/support/explore/faq
	//wifi_set_sleep_type(NONE_SLEEP_T); // on its own stopped wifi working
	//wifi_fpm_set_sleep_type(NONE_SLEEP_T); // with this seemed no difference

	system_os_post(procTaskPrio, 0, 0 );
}

void EnterCritical()
{
	PauseHPATimer();
	//ets_intr_lock();
}

void ExitCritical()
{
	//ets_intr_unlock();
	ContinueHPATimer();
}



