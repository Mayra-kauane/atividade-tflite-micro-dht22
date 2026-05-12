#include <stdio.h>
#include <stdarg.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DHT_GPIO GPIO_NUM_4
#define DHT_TIMEOUT_US 120
#define MONITOR_UART UART_NUM_1
#define MONITOR_TX_GPIO GPIO_NUM_17
#define MONITOR_RX_GPIO GPIO_NUM_18
#define MONITOR_BAUD_RATE 115200

typedef struct {
    float temperature_c;
    float humidity_percent;
} dht_reading_t;

static void monitor_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = MONITOR_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_ERROR_CHECK(uart_driver_install(MONITOR_UART, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(MONITOR_UART, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(MONITOR_UART,
                                 MONITOR_TX_GPIO,
                                 MONITOR_RX_GPIO,
                                 UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE));
}

static void monitor_printf(const char *format, ...)
{
    char message[160];
    va_list args;

    va_start(args, format);
    int length = vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    if (length <= 0) {
        return;
    }

    if (length >= (int)sizeof(message)) {
        length = sizeof(message) - 1;
    }

    uart_write_bytes(MONITOR_UART, message, length);
    printf("%s", message);
    fflush(stdout);
}

static esp_err_t dht_wait_while(gpio_num_t pin, int level, uint32_t timeout_us, uint32_t *duration_us)
{
    int64_t start = esp_timer_get_time();

    while (gpio_get_level(pin) == level) {
        int64_t elapsed = esp_timer_get_time() - start;
        if (elapsed > timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
    }

    if (duration_us != NULL) {
        *duration_us = (uint32_t)(esp_timer_get_time() - start);
    }

    return ESP_OK;
}

static esp_err_t dht_read(gpio_num_t pin, dht_reading_t *reading)
{
    uint8_t data[5] = {0};

    gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
    gpio_set_level(pin, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    gpio_set_level(pin, 1);
    esp_rom_delay_us(40);
    gpio_set_direction(pin, GPIO_MODE_INPUT);

    ESP_RETURN_ON_ERROR(dht_wait_while(pin, 1, DHT_TIMEOUT_US, NULL), "DHT22", "sem resposta do sensor");
    ESP_RETURN_ON_ERROR(dht_wait_while(pin, 0, DHT_TIMEOUT_US, NULL), "DHT22", "pulso inicial baixo invalido");
    ESP_RETURN_ON_ERROR(dht_wait_while(pin, 1, DHT_TIMEOUT_US, NULL), "DHT22", "pulso inicial alto invalido");

    for (int bit = 0; bit < 40; bit++) {
        uint32_t high_time_us = 0;

        ESP_RETURN_ON_ERROR(dht_wait_while(pin, 0, DHT_TIMEOUT_US, NULL), "DHT22", "inicio de bit invalido");
        ESP_RETURN_ON_ERROR(dht_wait_while(pin, 1, DHT_TIMEOUT_US, &high_time_us), "DHT22", "tempo alto de bit invalido");

        data[bit / 8] <<= 1;
        if (high_time_us > 50) {
            data[bit / 8] |= 1;
        }
    }

    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t humidity_raw = ((uint16_t)data[0] << 8) | data[1];
    uint16_t temperature_raw = (((uint16_t)data[2] & 0x7F) << 8) | data[3];

    reading->humidity_percent = humidity_raw / 10.0f;
    reading->temperature_c = temperature_raw / 10.0f;

    if ((data[2] & 0x80) != 0) {
        reading->temperature_c *= -1.0f;
    }

    return ESP_OK;
}

void app_main(void)
{
    monitor_init();

    monitor_printf("\r\nLeitura de sensor DHT22 no ESP32-S3\r\n");
    monitor_printf("Pino de dados do sensor: GPIO%d\r\n", DHT_GPIO);
    monitor_printf("Monitor serial: UART1 TX=GPIO%d RX=GPIO%d @ %d baud\r\n\r\n",
                   MONITOR_TX_GPIO,
                   MONITOR_RX_GPIO,
                   MONITOR_BAUD_RATE);

    while (true) {
        dht_reading_t reading;
        esp_err_t err = dht_read(DHT_GPIO, &reading);

        if (err == ESP_OK) {
            monitor_printf("Temperatura: %.1f C | Umidade: %.1f %%\r\n",
                           reading.temperature_c,
                           reading.humidity_percent);
        } else {
            monitor_printf("Falha ao ler DHT22: %s\r\n", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
