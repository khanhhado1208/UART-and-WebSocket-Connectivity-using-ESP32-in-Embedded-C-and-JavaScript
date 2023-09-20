#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/timers.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_http_server.h"

#include "lwip/err.h"
#include "lwip/sys.h"

static const int RX_BUF_SIZE = 1024;
static TimerHandle_t timer; // Global timer handle variable
static int seconds = 0;
static int minutes = 0;
static int hours = 0;
static int days = 0;

#define TXD_PIN (GPIO_NUM_4)
#define RXD_PIN (GPIO_NUM_5)
#define START_COMMAND "Power on - start counting" // Command to start the timer
#define STOP_COMMAND "Power off - stop counting time" // Command to stop the timer
#define RESET_COMMAND "RESET"

#define EXAMPLE_ESP_WIFI_SSID      "HD" //add your SSID wifi
#define EXAMPLE_ESP_WIFI_PASS      "helloHado" //add your password wifi
#define EXAMPLE_ESP_MAXIMUM_RETRY 10

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi station";

const char *html_page = "<html><body><h1>Hello HA DO</h1></body></html>";
httpd_handle_t server = NULL;

static int s_retry_num = 0;

static esp_err_t root_handler(httpd_req_t *req) {
    char message[50];
    snprintf(message, sizeof(message), "Timer: %d days %d hours %d minutes %d seconds", days, hours, minutes, seconds);
    // Set the HTTP response content type to plain text
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, message, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void start_http_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t test_page_uri = {
            .uri = "/test",
            .method = HTTP_GET,
            .handler = root_handler,
            .user_ctx = NULL
        };
        httpd_register_uri_handler(server, &test_page_uri);
    }
}

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG, "connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        start_http_server();
    }
}

void wifi_init_sta(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip));
    ESP_ERROR_CHECK(esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id));
    vEventGroupDelete(s_wifi_event_group);
}

void init(void) {
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

int sendData(const char *logName, const char *data) {
    const int len = strlen(data);
    const int txBytes = uart_write_bytes(UART_NUM_1, data, len);
    ESP_LOGI(logName, "Wrote %d bytes", txBytes);
    return txBytes;
}

static void rx_task(void *arg) {
    static const char *RX_TASK_TAG = "RX_TASK";
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);
    uint8_t *data = (uint8_t *)malloc(RX_BUF_SIZE + 1);
    while (1) {
        const int rxBytes = uart_read_bytes(UART_NUM_1, data, RX_BUF_SIZE, 1000 / portTICK_PERIOD_MS);
        if (rxBytes > 0) {
            data[rxBytes] = 0;
            ESP_LOGI(RX_TASK_TAG, "Read %d bytes: '%s'", rxBytes, data);
            ESP_LOG_BUFFER_HEXDUMP(RX_TASK_TAG, data, rxBytes, ESP_LOG_INFO);

            if (strcmp((char *)data, START_COMMAND) == 0) {
                ESP_LOGI(RX_TASK_TAG, "Start command received");
                xTimerStart(timer, 0);
            } else if (strcmp((char *)data, STOP_COMMAND) == 0) {
                ESP_LOGI(RX_TASK_TAG, "Stop command received");
                xTimerStop(timer, 0);
            } else if (strcmp((char *)data, RESET_COMMAND) == 0) {
                ESP_LOGI(RX_TASK_TAG, "Reset command received");
                seconds = 0;
                minutes = 0;
                hours = 0;
                days = 0;
                nvs_handle_t nvs_handle;
                esp_err_t err = nvs_open("storage", NVS_READWRITE, &nvs_handle);
                if (err == ESP_OK) {
                    err = nvs_set_i32(nvs_handle, "seconds", 0);
                    if (err == ESP_OK) {
                        err = nvs_set_i32(nvs_handle, "minutes", 0);
                        if (err == ESP_OK) {
                            err = nvs_set_i32(nvs_handle, "hours", 0);
                            if (err == ESP_OK) {
                                err = nvs_set_i32(nvs_handle, "days", 0);
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
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    free(data);
}

void timer_callback(TimerHandle_t xTimer) {
    seconds++;

    if (seconds >= 60) {
        minutes++;
        seconds -= 60;
    }
    if (minutes >= 60) {
        hours++;
        minutes -= 60;
    }
    if (hours >= 24) {
        days++;
        hours -= 24;
    }

    // Store counting time in NVS
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

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS storage error");
    }

}

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
    init();

    // Create a timer with a 1-second period
    timer = xTimerCreate("Timer", pdMS_TO_TICKS(1000), pdTRUE, (void *)0, timer_callback);
    if (timer == NULL) {
        ESP_LOGE(TAG, "Timer creation failed");
    }

    // Load counting time from NVS
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

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS storage error");
    }

    // Create the UART receive task
    xTaskCreate(rx_task, "uart_rx_task", 1024 * 4, NULL, configMAX_PRIORITIES, NULL);
}
