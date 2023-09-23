#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include <esp_http_server.h>
#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "connect_wifi.h"

int days = 0;
int hours = 0;
int minutes = 0;
int seconds = 0;

bool read_nvs_time = true;

int total_seconds = 0;

httpd_handle_t server = NULL;
struct async_resp_arg {
    httpd_handle_t hd;
    int fd;
};

static const char *TAG = "WebSocket Server"; // TAG for debug

#define INDEX_HTML_PATH "/spiffs/index.html"
char index_html[4096];
char response_data[4096];

static void initi_web_page_buffer(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = NULL,
        .max_files = 5,
        .format_if_mount_failed = true};

    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));

    memset((void *)index_html, 0, sizeof(index_html));
    struct stat st;
    if (stat(INDEX_HTML_PATH, &st))
    {
        ESP_LOGE(TAG, "index.html not found");
        return;
    }

    FILE *fp = fopen(INDEX_HTML_PATH, "r");
    if (fread(index_html, st.st_size, 1, fp) == 0)
    {
        ESP_LOGE(TAG, "fread failed");
    }
    fclose(fp);
}

esp_err_t get_req_handler(httpd_req_t *req)
{
    int response;
    sprintf(response_data, index_html, "Hello");
    response = httpd_resp_send(req, response_data, HTTPD_RESP_USE_STRLEN);
    return response;
}

static void ws_async_send(void *arg)
{
    httpd_ws_frame_t ws_pkt;
    struct async_resp_arg *resp_arg = arg;
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
    
    char buff[10];
    memset(buff, 0, sizeof(buff));
    sprintf(buff, "%d",total_seconds);
    
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t *)buff;
    ws_pkt.len = strlen(buff);
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    
    static size_t max_clients = CONFIG_LWIP_MAX_LISTENING_TCP;
    size_t fds = max_clients;
    int client_fds[max_clients];

    esp_err_t ret = httpd_get_client_list(server, &fds, client_fds);

    if (ret != ESP_OK) {
        return;
    }
    for (int i = 0; i < fds; i++) {
        int client_info = httpd_ws_get_fd_info(server, client_fds[i]);
        if (client_info == HTTPD_WS_CLIENT_WEBSOCKET) {
            httpd_ws_send_frame_async(hd, client_fds[i], &ws_pkt);
        }
    }
    free(resp_arg);
}

static void read_timer_from_nvs(void){
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open("storage", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK) {
        err = nvs_get_i32(nvs_handle, "seconds", &seconds);
    }
    if (err == ESP_OK) {
        err = nvs_get_i32(nvs_handle, "minutes", &minutes);
    }
    if (err == ESP_OK) {
        err = nvs_get_i32(nvs_handle, "hours", &hours);
    }
    if (err == ESP_OK) {
        err = nvs_get_i32(nvs_handle, "days", &days);
    }
    nvs_close(nvs_handle);
    total_seconds = seconds + minutes * 60 + hours * 60 *60 + days * 24 * 60 *60;
}

static esp_err_t trigger_async_send(httpd_handle_t handle, httpd_req_t *req)
{
    struct async_resp_arg *resp_arg = malloc(sizeof(struct async_resp_arg));
    resp_arg->hd = req->handle;
    resp_arg->fd = httpd_req_to_sockfd(req);
    return httpd_queue_work(handle, ws_async_send, resp_arg);
}

static esp_err_t handle_ws_req(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        read_nvs_time = true;
        read_timer_from_nvs();
        trigger_async_send(req->handle, req);
        ESP_LOGI(TAG, "Handshake done, the new connection was opened");
        return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }
    if (ws_pkt.len)
    {
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL)
        {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        if (strcmp((const char*)ws_pkt.payload, "Reset") == 0)
        {
            // Handle the "Reset" message
            // Set the timer values to 0
            seconds = 0;
            minutes = 0;
            hours = 0;
            days = 0;
            total_seconds = 0;
        }
        else
        {
            if(ws_pkt.len < 12){
                minutes = 0;
                hours = 0;
                days = 0;
                sscanf((const char*)ws_pkt.payload,"%d seconds",&seconds);
            }else if(ws_pkt.len > 12 && ws_pkt.len < 22){
                hours = 0;
                days = 0;
                sscanf((const char*)ws_pkt.payload,"%d minutes %d seconds",&minutes,&seconds);
            }else if(ws_pkt.len > 22 && ws_pkt.len < 34){
                days = 0;
                sscanf((const char*)ws_pkt.payload,"%d hours %d minutes %d seconds",&hours,&minutes,&seconds);
            }else if(ws_pkt.len > 34){
                sscanf((const char*)ws_pkt.payload,"%d days %d hours %d minutes %d seconds",&days,&hours,&minutes,&seconds);
            }
            ESP_LOGI(TAG, "Got packet with message: %s", ws_pkt.payload);
            ESP_LOGI(TAG, "%d:%d:%d:%d", days,hours,minutes,seconds);
            nvs_handle_t nvs_handle;
            esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
            if (err == ESP_OK) {
                err = nvs_set_i32(nvs_handle, "seconds", seconds);
                if (err == ESP_OK) {
                    err = nvs_set_i32(nvs_handle, "minutes", minutes);
                    if (err == ESP_OK) {
                        err = nvs_set_i32(nvs_handle, "hours", hours);
                        if (err == ESP_OK) {
                            err = nvs_set_i32(nvs_handle, "days", days);
                            if (err == ESP_OK) {
                                err = nvs_commit(nvs_handle);
                            }
                        }
                    }
                }
                nvs_close(nvs_handle);
            }
        }
    }
    ESP_LOGI(TAG, "frame len is %d", ws_pkt.len);
    ESP_LOGI(TAG, "stored time %d", total_seconds);
    
    return ESP_OK;
}


httpd_handle_t setup_websocket_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    httpd_uri_t uri_get = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = get_req_handler,
        .user_ctx = NULL};
    
    httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = handle_ws_req,
        .user_ctx = NULL,
        .is_websocket = true};

    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &ws);
    }

    return server;
}

void app_main()
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    read_timer_from_nvs();

    connect_wifi();

    if (wifi_connect_status)
    {
        ESP_LOGI(TAG, "ESP32 ESP-IDF WebSocket Web Server is running ... ...\n");
        initi_web_page_buffer();
        setup_websocket_server();
    }
}