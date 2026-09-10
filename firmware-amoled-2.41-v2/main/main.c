#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "driver/i2c_master.h"
#include "driver/usb_serial_jtag.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "display_bsp.h"
#include "touch_bsp.h"
#include "main_config.h"
#include "panel.h"

static EventGroupHandle_t network_events;
static char ssid[33], password[65], endpoint[192], token[128];
static bool ready;
static const char *TAG="amoled_v2";

static bool field(cJSON *json,const char *name,char *out,size_t capacity,size_t minimum) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(json,name);
    if(!cJSON_IsString(v) || !v->valuestring) return false;
    size_t len=strlen(v->valuestring);
    if(len<minimum || len>=capacity || strchr(v->valuestring,'\r') || strchr(v->valuestring,'\n')) return false;
    memcpy(out,v->valuestring,len+1); return true;
}
static bool valid_endpoint(const char *url) {
    unsigned a,b,c,d,port; int end=0;
    if(sscanf(url,"http://%u.%u.%u.%u:%u/snapshot%n",&a,&b,&c,&d,&port,&end)!=5 || !end || url[end]) return false;
    return a<256 && b<256 && c<256 && d<256 && port>0 && port<65536 &&
        (a==10 || (a==172 && b>=16 && b<=31) || (a==192 && b==168));
}
static bool save_config(const char *line) {
    char s[33],p[65],u[192],t[128];
    cJSON *json=cJSON_Parse(line);
    bool ok=json && field(json,"ssid",s,sizeof(s),1) && field(json,"password",p,sizeof(p),8) &&
        field(json,"url",u,sizeof(u),1) && field(json,"token",t,sizeof(t),24) && valid_endpoint(u);
    cJSON_Delete(json);
    if(!ok) return false;
    // Store one blob atomically so interrupted provisioning cannot mix two configurations.
    nvs_handle_t nvs;
    if(nvs_open("desk_v2",NVS_READWRITE,&nvs)!=ESP_OK) return false;
    ok=nvs_set_str(nvs,"config",line)==ESP_OK && nvs_commit(nvs)==ESP_OK;
    nvs_close(nvs); memset(p,0,sizeof(p)); return ok;
}
static bool load_config(void) {
    nvs_handle_t nvs; size_t length=0;
    if(nvs_open("desk_v2",NVS_READONLY,&nvs)!=ESP_OK) return false;
    esp_err_t error=nvs_get_str(nvs,"config",NULL,&length);
    char *text=error==ESP_OK && length<=2048 ? malloc(length) : NULL;
    if(!text) { nvs_close(nvs); return false; }
    error=nvs_get_str(nvs,"config",text,&length); nvs_close(nvs);
    cJSON *json=error==ESP_OK?cJSON_Parse(text):NULL;
    bool ok=json && field(json,"ssid",ssid,sizeof(ssid),1) && field(json,"password",password,sizeof(password),8) &&
        field(json,"url",endpoint,sizeof(endpoint),1) && field(json,"token",token,sizeof(token),24) && valid_endpoint(endpoint);
    cJSON_Delete(json); memset(text,0,length); free(text); return ok;
}
static bool write_usb(const char *data,size_t length) {
    while(length) {
        // A ring-buffer item must fit the 2 KB TX buffer, including its metadata.
        int sent=usb_serial_jtag_write_bytes(data,length>512?512:length,pdMS_TO_TICKS(1500));
        if(sent<=0) return false;
        data+=sent; length-=sent;
    }
    return true;
}
static void screenshot(void) {
#if CONFIG_LV_USE_SNAPSHOT
    lv_draw_buf_t *image=NULL;
    if(esp_lv_adapter_lock(-1)==ESP_OK) {
        image=lv_snapshot_take(lv_screen_active(),LV_COLOR_FORMAT_RGB565);
        esp_lv_adapter_unlock();
    }
    if(!image) { printf("SCREEN_FAILED\n"); return; }
    char *row=malloc(image->header.w*4+32);
    if(row) {
        static const char hex[]="0123456789abcdef";
        esp_log_level_set("*",ESP_LOG_NONE);
        int header_len=sprintf(row,"SCREEN_BEGIN %lu %lu\n",(unsigned long)image->header.w,(unsigned long)image->header.h);
        bool sending=write_usb(row,header_len);
        for(unsigned y=0;sending && y<image->header.h;y++) {
            int offset=sprintf(row,"ROW %u ",y);
            const uint8_t *pixels=image->data+y*image->header.stride;
            for(unsigned x=0;x<image->header.w*2;x++) {
                row[offset++]=hex[pixels[x]>>4]; row[offset++]=hex[pixels[x]&15];
            }
            row[offset++]='\n'; sending=write_usb(row,offset);
        }
        if(sending) write_usb("SCREEN_END\n",11);
        usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(1500));
        esp_log_level_set("*",ESP_LOG_INFO);
        free(row);
    }
    if(esp_lv_adapter_lock(-1)==ESP_OK) { lv_draw_buf_destroy(image); esp_lv_adapter_unlock(); }
#else
    printf("SCREEN_DISABLED\n");
#endif
}
static void serial_task(void *arg) {
    (void)arg;
    char *line=calloc(1,2048); size_t used=0; bool overflow=false;
    if(!line) vTaskDelete(NULL);
    while(1) {
        char ch;
        if(usb_serial_jtag_read_bytes(&ch,1,pdMS_TO_TICKS(100))<=0) continue;
        if(ch=='\r') continue;
        if(ch!='\n') {
            if(used<2047 && !overflow) line[used++]=ch; else overflow=true;
            continue;
        }
        line[used]=0;
        if(!overflow && !strcmp(line,"IDENTIFY")) printf("AGENT_DESK_AMOLED_V2\n");
        else if(!overflow && !strcmp(line,"SCREEN")) screenshot();
        else if(!overflow && !strncmp(line,"PAGE ",5)) {
            if(esp_lv_adapter_lock(-1)==ESP_OK) { panel_debug_page(atoi(line+5)); esp_lv_adapter_unlock(); }
            printf("PAGE_READY\n");
        }
        else if(!overflow && !strncmp(line,"CONFIG ",7)) {
            bool ok=save_config(line+7); memset(line,0,2048);
            printf("%s\n",ok?"CONFIG_SAVED":"CONFIG_REJECTED"); fflush(stdout);
            if(ok) { vTaskDelay(pdMS_TO_TICKS(500)); esp_restart(); }
        }
        used=0; overflow=false; memset(line,0,2048);
    }
}
static void wifi_event(void *arg,esp_event_base_t base,int32_t id,void *data) {
    (void)arg;
    if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event=data;
        xEventGroupSetBits(network_events,BIT0);
        ESP_LOGI(TAG,"LAN_READY " IPSTR,IP2STR(&event->ip_info.ip));
    } else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(network_events,BIT0);
        ESP_LOGW(TAG,"Wi-Fi disconnected, reason=%d",((wifi_event_sta_disconnected_t *)data)->reason);
    }
}
static void report(bool wifi,bool http) {
    if(esp_lv_adapter_lock(-1)==ESP_OK) { panel_network(wifi,http,ready); esp_lv_adapter_unlock(); }
}
static void network_task(void *arg) {
    (void)arg;
    char *body=malloc(65537), auth[160];
    if(!body) { ESP_LOGE(TAG,"Cannot allocate network buffer"); vTaskDelete(NULL); }
    snprintf(auth,sizeof(auth),"Bearer %s",token);
    esp_http_client_config_t config={.url=endpoint,.timeout_ms=3500,.buffer_size=2048,.disable_auto_redirect=true};
    esp_http_client_handle_t client=esp_http_client_init(&config);
    if(!client) { free(body); vTaskDelete(NULL); }
    esp_http_client_set_header(client,"Authorization",auth);
    while(1) {
        if(!(xEventGroupGetBits(network_events)&BIT0)) {
            report(false,false); esp_wifi_connect();
            xEventGroupWaitBits(network_events,BIT0,pdFALSE,pdFALSE,pdMS_TO_TICKS(5000));
            continue;
        }
        bool valid=false;
        esp_err_t error=esp_http_client_open(client,0);
        if(error==ESP_OK) {
            int64_t length=esp_http_client_fetch_headers(client);
            if(esp_http_client_get_status_code(client)==200 && length>0 && length<=65536) {
                int used=0;
                while(used<length) {
                    int got=esp_http_client_read(client,body+used,(int)length-used);
                    if(got<=0) break;
                    used+=got;
                }
                body[used]=0;
                if(used==length && esp_lv_adapter_lock(-1)==ESP_OK) {
                    valid=panel_accept(body); esp_lv_adapter_unlock();
                }
                if(valid) ESP_LOGI(TAG,"SNAPSHOT_OK bytes=%d free_heap=%lu",used,(unsigned long)esp_get_free_heap_size());
            } else ESP_LOGW(TAG,"Snapshot HTTP %d",esp_http_client_get_status_code(client));
        }
        esp_http_client_close(client);
        report((xEventGroupGetBits(network_events)&BIT0)!=0,valid);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
void app_main(void) {
    esp_err_t error=nvs_flash_init();
    // Never erase arbitrary existing NVS automatically; retain a recoverable configuration.
    ESP_ERROR_CHECK(error);
    usb_serial_jtag_driver_config_t usb={.tx_buffer_size=2048,.rx_buffer_size=2048};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));
    ready=load_config();
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t i2c={.i2c_port=BSP_I2C_NUM,.sda_io_num=I2C_SDA_PIN,.scl_io_num=I2C_SCL_PIN,
        .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=true};
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c,&bus));
    bsp_display_init(bus,LCD_WIDTH*LCD_HEIGHT*2);
    bsp_display_lcd_init();
    ESP_ERROR_CHECK(bsp_display_touch_reset());
    bsp_display_indev_init(bsp_touch_init(bus,LCD_WIDTH,LCD_HEIGHT));
    ESP_ERROR_CHECK(esp_lv_adapter_start());
    ESP_ERROR_CHECK(bsp_display_brightness_set(65));
    if(esp_lv_adapter_lock(-1)==ESP_OK) { panel_init(); panel_network(false,false,ready); esp_lv_adapter_unlock(); }
    ESP_LOGI(TAG,"AGENT_DESK_AMOLED_V2 UI_READY 600x450 config=%d",ready);
    xTaskCreate(serial_task,"provision",6144,NULL,3,NULL);
    if(!ready) return;
    network_events=xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init()); ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t wifi=WIFI_INIT_CONFIG_DEFAULT(); ESP_ERROR_CHECK(esp_wifi_init(&wifi));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,wifi_event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,wifi_event,NULL));
    wifi_config_t station={0};
    memcpy(station.sta.ssid,ssid,strlen(ssid)); memcpy(station.sta.password,password,strlen(password));
    station.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&station));
    memset(password,0,sizeof(password)); memset(&station,0,sizeof(station));
    ESP_ERROR_CHECK(esp_wifi_start());
    xTaskCreate(network_task,"snapshots",8192,NULL,3,NULL);
}
