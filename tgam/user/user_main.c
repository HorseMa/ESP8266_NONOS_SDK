/* main.c -- MQTT client example
*
* Copyright (c) 2014-2015, Tuan PM <tuanpm at live dot com>
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of Redis nor the names of its contributors may be used
* to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/

#include "ets_sys.h"
#include "driver/uart.h"
#include "osapi.h"
#include "mqtt.h"
#include "wifi.h"
#include "config.h"
#include "debug.h"
#include "gpio.h"
#include "user_interface.h"
#include "mem.h"
#include "sntp.h"

#define UART_BUFF_EN  0   //use uart buffer  , FOR UART0
#define uart_recvTaskPrio        0
#define uart_recvTaskQueueLen    10
os_event_t    uart_recvTaskQueue[uart_recvTaskQueueLen];
MQTT_Client mqttClient;
typedef unsigned long u32_t;
static ETSTimer sntp_timer;

typedef enum{
    enParseStateSync,
    enParseStateLength,
    enParseStatePayload,
    enParseStateCheckSum
}en_ParseState,*pen_ParseState;

void hexdump(const unsigned char *buf, const int num)
{
    int i;
    for(i = 0; i < num; i++)
    {
        INFO("%02X ", buf[i]);
        /*if ((i+1)%8 == 0)
            printf("\n");*/
    }
    INFO("\r\n");
    return;
}

void TGAM_powerenable(void)
{
    gpio_output_set(BIT4, 0, BIT4, 0);
}

void TGAM_powerdisable(void)
{
    gpio_output_set(0, BIT4, BIT4, 0);
}

void buzzer_enable(void)
{
    gpio_output_set(BIT5, 0, BIT5, 0);
}

void buzzer_disable(void)
{
    gpio_output_set(0, BIT5, BIT5, 0);
}

void tgam_led_on(void)
{
    gpio_output_set(BIT14, 0, BIT14, 0);
}

void tgam_led_off(void)
{
    gpio_output_set(0, BIT14, BIT14, 0);
}

void sntpfn()
{
    u32_t ts = 0;
    ts = sntp_get_current_timestamp();
    os_printf("current time : %s\n", sntp_get_real_time(ts));
    if (ts == 0) {
        //os_printf("did not get a valid time from sntp server\n");
    } else {
            os_timer_disarm(&sntp_timer);
            MQTT_Connect(&mqttClient);
    }
}

void wifiConnectCb(uint8_t status)
{
    if(status == STATION_GOT_IP){
        sntp_setservername(0, "pool.ntp.org");        // set sntp server after got ip address
        sntp_init();
        os_timer_disarm(&sntp_timer);
        os_timer_setfn(&sntp_timer, (os_timer_func_t *)sntpfn, NULL);
        os_timer_arm(&sntp_timer, 1000, 1);//1s
    } else {
          MQTT_Disconnect(&mqttClient);
    }
}

void mqttConnectedCb(uint32_t *args)
{
    MQTT_Client* client = (MQTT_Client*)args;
    INFO("MQTT: Connected\r\n");
    MQTT_Subscribe(client, "/mqtt/topic/0", 0);
    MQTT_Subscribe(client, "/mqtt/topic/1", 1);
    MQTT_Subscribe(client, "/mqtt/topic/2", 2);

    MQTT_Publish(client, "/mqtt/topic/0", "hello0", 6, 0, 0);
    MQTT_Publish(client, "/mqtt/topic/1", "hello1", 6, 1, 0);
    MQTT_Publish(client, "/mqtt/topic/2", "hello2", 6, 2, 0);
    TGAM_powerenable();

}

void mqttDisconnectedCb(uint32_t *args)
{
    MQTT_Client* client = (MQTT_Client*)args;
    INFO("MQTT: Disconnected\r\n");
}

void mqttPublishedCb(uint32_t *args)
{
    MQTT_Client* client = (MQTT_Client*)args;
    INFO("MQTT: Published\r\n");
}

void mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len)
{
    char *topicBuf = (char*)os_zalloc(topic_len+1),
            *dataBuf = (char*)os_zalloc(data_len+1);

    MQTT_Client* client = (MQTT_Client*)args;

    os_memcpy(topicBuf, topic, topic_len);
    topicBuf[topic_len] = 0;

    os_memcpy(dataBuf, data, data_len);
    dataBuf[data_len] = 0;

    INFO("Receive topic: %s, data: %s \r\n", topicBuf, dataBuf);
    os_free(topicBuf);
    os_free(dataBuf);
}


/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABCCC
 *                A : rf cal
 *                B : rf init data
 *                C : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
 *******************************************************************************/
uint32 ICACHE_FLASH_ATTR
user_rf_cal_sector_set(void)
{
    enum flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;

    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 5;
            break;

        case FLASH_SIZE_8M_MAP_512_512:
            rf_cal_sec = 256 - 5;
            break;

        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:
            rf_cal_sec = 512 - 5;
            break;

        case FLASH_SIZE_32M_MAP_512_512:
        case FLASH_SIZE_32M_MAP_1024_1024:
            rf_cal_sec = 1024 - 5;
            break;

        case FLASH_SIZE_64M_MAP_1024_1024:
            rf_cal_sec = 2048 - 5;
            break;
        case FLASH_SIZE_128M_MAP_1024_1024:
            rf_cal_sec = 4096 - 5;
            break;
        default:
            rf_cal_sec = 0;
            break;
    }

    return rf_cal_sec;
}

LOCAL void ICACHE_FLASH_ATTR ///////
uart_recvTask(os_event_t *events)
{
    static en_ParseState enParseState = enParseStateSync;
    static u16 syncword = 0;
    static u16 length = 0,index = 0;
    static u8 payload[256] = {0};
    static u32 checksum = 0,checksumtmp = 0;
    //static cJSON *root = NULL,*rawarray = NULL;
    static rawcnt = 0;
    static u8 *text;

    if(events->sig == 0){
    #if  UART_BUFF_EN
        Uart_rx_buff_enq();
    #else
        uint8 fifo_len = (READ_PERI_REG(UART_STATUS(UART0))>>UART_RXFIFO_CNT_S)&UART_RXFIFO_CNT;
        uint8 d_tmp = 0;
        uint8 idx=0;
        for(idx=0;idx<fifo_len;idx++) {
            d_tmp = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
            switch(enParseState)
            {
                case enParseStateSync:
                    syncword = syncword << 8;
                    syncword = syncword | d_tmp;
                    if(syncword == 0xAAAA)
                    {
                        enParseState = enParseStateLength;
                    }
                    break;
                case enParseStateLength:
                    length = d_tmp;
                    index = 0;
                    enParseState = enParseStatePayload;
                    break;
                case enParseStatePayload:
                    payload[index ++] = d_tmp;
                    if(index >= length)
                    {
                        enParseState = enParseStateCheckSum;
                    }
                    break;
                case enParseStateCheckSum:
                    checksum = d_tmp;
                    checksumtmp = 0;
                    for(index = 0;index < length;index ++)
                    {
                        checksumtmp += payload[index];
                    }
                    checksumtmp = (checksumtmp ^ 0xFFFFFFFF) & 0xFF;
                    if(checksum == checksumtmp)
                    {/*
                        if (!root)
                        {
                            root =  cJSON_CreateObject();
                            cJSON_AddNumberToObject(root, "Chip_ID", system_get_chip_id());
                            cJSON_AddItemToObject(root, "Raw Data", rawarray = cJSON_CreateArray());
                            printf("free heap size :%d\r\n", system_get_free_heap_size());
                        }
                        if(length == 0x20)
                        {
                            if(payload[0] == 0x02)
                            {
                                cJSON_AddNumberToObject(root, "Poor_Signal", (payload[1] & 0xFF));
                            }
                            if((payload[2] == 0x83) && (payload[3] == 0x18))
                            {
                                cJSON_AddNumberToObject(root, "Delta", ((u32)payload[4] << 16) | ((u32)payload[5] << 8) | ((u32)payload[6] << 0));
                                cJSON_AddNumberToObject(root, "Theta", ((u32)payload[7] << 16) | ((u32)payload[8] << 8) | ((u32)payload[9] << 0));
                                cJSON_AddNumberToObject(root, "LowAlpha", ((u32)payload[10] << 16) | ((u32)payload[11] << 8) | ((u32)payload[12] << 0));
                                cJSON_AddNumberToObject(root, "HighAlpha", ((u32)payload[13] << 16) | ((u32)payload[14] << 8) | ((u32)payload[15] << 0));
                                cJSON_AddNumberToObject(root, "LowBeta", ((u32)payload[16] << 16) | ((u32)payload[17] << 8) | ((u32)payload[18] << 0));
                                cJSON_AddNumberToObject(root, "HighBeta", ((u32)payload[19] << 16) | ((u32)payload[20] << 8) | ((u32)payload[21] << 0));
                                cJSON_AddNumberToObject(root, "LowGamma", ((u32)payload[22] << 16) | ((u32)payload[23] << 8) | ((u32)payload[24] << 0));
                                cJSON_AddNumberToObject(root, "MiddleGamma", ((u32)payload[25] << 16) | ((u32)payload[26] << 8) | ((u32)payload[27] << 0));
                            }
                            if(payload[28] == 0x04)
                            {
                                cJSON_AddNumberToObject(root, "Attention", (payload[29] & 0xFF));
                            }
                            if(payload[30] == 0x05)
                            {
                                cJSON_AddNumberToObject(root, "Meditation", (payload[31] & 0xFF));
                            }

                            xSemaphoreTake( MQTTpubSemaphore, portMAX_DELAY );
                            message.qos = QOS0;
                            message.retained = 0;
                            memset(mqtt_payload,0,1024 * 2);
                            message.payload = mqtt_payload;
                            strcpy(mqtt_payload, text = cJSON_Print(root));
                            free(text);
                            cJSON_Delete(root);
                            message.payloadlen = strlen(mqtt_payload);
                            printf("%s, %d\r\n",mqtt_payload,rawcnt);
                            xSemaphoreGive( MQTTpubSemaphore );
                            vTaskResume(mqttc_client_handle);
                            root = NULL;
                            rawcnt = 0;
                            rawarray = NULL;
                        }
                        else if(length == 0x04)
                        {
                            if((payload[0] == 0x80) && (payload[1] == 0x02))
                            {
                                if(rawcnt % 2)
                                {
                                    cJSON_AddItemToArray(rawarray,cJSON_CreateNumber(((short) (( payload[2] << 8 ) | payload[3]))));
                                }
                                rawcnt ++;
                            }
                            else
                            {
                                printf("format error\r\n");
                            }

                        }
                        else
                        {
                            printf("format error\r\n");
                        }*/
                        hexdump(payload,length);
                    }
                    else
                    {
                        INFO("Checksum error\r\n");
                    }
                    enParseState = enParseStateSync;
                    syncword = 0;
                    break;
                default:
                    enParseState = enParseStateSync;
                    syncword = 0;
                    break;
            }
            //uart_tx_one_char(UART0, d_tmp);
            //INFO("%02x\r\n",d_tmp);
        }
        WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR|UART_RXFIFO_TOUT_INT_CLR);
        uart_rx_intr_enable(UART0);
    #endif
    }else if(events->sig == 1){
    #if UART_BUFF_EN
     //already move uart buffer output to uart empty interrupt
        //tx_start_uart_buffer(UART0);
    #else

    #endif
    }
}

void user_init(void)
{
    uart_init(BIT_RATE_57600, BIT_RATE_115200);
    system_os_task(uart_recvTask, uart_recvTaskPrio, uart_recvTaskQueue, uart_recvTaskQueueLen);
    UART_SetPrintPort(1);
    os_delay_us(60000);

    CFG_Load();

    MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);
    //MQTT_InitConnection(&mqttClient, "192.168.11.122", 1880, 0);

    MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass, sysCfg.mqtt_keepalive, 1);
    //MQTT_InitClient(&mqttClient, "client_id", "user", "pass", 120, 1);

    MQTT_InitLWT(&mqttClient, "/lwt", "offline", 0, 0);
    MQTT_OnConnected(&mqttClient, mqttConnectedCb);
    MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
    MQTT_OnPublished(&mqttClient, mqttPublishedCb);
    MQTT_OnData(&mqttClient, mqttDataCb);

    WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, wifiConnectCb);

    INFO("\r\nSystem started ...\r\n");
}
