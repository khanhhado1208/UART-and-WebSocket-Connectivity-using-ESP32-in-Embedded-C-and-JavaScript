/* UART asynchronous example, that uses separate RX and TX tasks
   This example code is in the Public Domain (or CC0 licensed, at your option.)
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "string.h"
#include "driver/gpio.h"

static const int RX_BUF_SIZE = 1024;

#define TXD_PIN (GPIO_NUM_4)
#define RXD_PIN (GPIO_NUM_5)

#define POWER_PIN 19
#define RESET_PIN 20

// To make both press and release button change power status
bool POWER = false;
bool POWER_BUTTON = false;
bool RESET_BUTTON = false;

void init(void) {
    const uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    // We won't use a buffer for sending data.
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    gpio_reset_pin(POWER_PIN);
    gpio_set_direction(POWER_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(POWER_PIN);
    gpio_pulldown_dis(POWER_PIN);

    gpio_reset_pin(RESET_PIN);
    gpio_set_direction(RESET_PIN, GPIO_MODE_INPUT);
    gpio_pullup_en(RESET_PIN);
    gpio_pulldown_dis(RESET_PIN);
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
        }
    }
    free(data);
}

static void button_task(void *arg) {
    static const char *BUTTON_TASK_TAG = "BUTTON_CHECK";
    esp_log_level_set(BUTTON_TASK_TAG, ESP_LOG_INFO);

    while (1) {
        if (gpio_get_level(POWER_PIN) == 0) {
            // Button is pressed
            POWER_BUTTON = true;
        } else {
            if (POWER_BUTTON) {
                ESP_LOGI(BUTTON_TASK_TAG, "Button pressed");
                // Change power status
                POWER = !POWER;
                // Set back button status
                POWER_BUTTON = false;
                // Send message to slave board based on power status
                if (POWER) {
                    sendData(BUTTON_TASK_TAG, "Power on - start counting");
                } else {
                    sendData(BUTTON_TASK_TAG, "Power off - stop counting time");
                }
            }
        }
        if (gpio_get_level(RESET_PIN) == 0) {
            // Button is pressed
            RESET_BUTTON = true;
        } else {
            if (RESET_BUTTON) {
                ESP_LOGI(BUTTON_TASK_TAG, "Button pressed");
                // Change power status
                // Set back button status
                RESET_BUTTON = false;
                // Send message to slave board
                sendData(BUTTON_TASK_TAG, "RESET");
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS); // Adjust delay as needed
    }
}

void app_main(void) {
    init();
    xTaskCreate(rx_task, "uart_rx_task", 1024 * 4, NULL, configMAX_PRIORITIES, NULL);
    xTaskCreate(button_task, "button_check", 1024 * 2, NULL, configMAX_PRIORITIES, NULL);
}
