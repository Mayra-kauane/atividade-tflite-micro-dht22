#include <math.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "generated/comfort_model.h"
#include "model.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

#define DHT_GPIO GPIO_NUM_4
#define DHT_TIMEOUT_US 120

namespace {

constexpr char kTag[] = "atividade_4";
constexpr float kTwoPi = 6.28318530718f;
constexpr int kInferencesPerCycle = 20;
constexpr int kTensorArenaSize = 3 * 1024;

struct DhtReading {
    float temperature_c;
    float humidity_percent;
};

const tflite::Model *model = nullptr;
tflite::MicroInterpreter *interpreter = nullptr;
TfLiteTensor *input = nullptr;
TfLiteTensor *output = nullptr;
int inference_count = 0;
alignas(16) uint8_t tensor_arena[kTensorArenaSize];

esp_err_t dht_wait_while(gpio_num_t pin, int level, uint32_t timeout_us, uint32_t *duration_us)
{
    const int64_t start = esp_timer_get_time();

    while (gpio_get_level(pin) == level) {
        const int64_t elapsed = esp_timer_get_time() - start;
        if (elapsed > timeout_us) {
            return ESP_ERR_TIMEOUT;
        }
    }

    if (duration_us != nullptr) {
        *duration_us = static_cast<uint32_t>(esp_timer_get_time() - start);
    }

    return ESP_OK;
}

esp_err_t dht_read(gpio_num_t pin, DhtReading *reading)
{
    uint8_t data[5] = {0};

    gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
    gpio_set_level(pin, 0);
    vTaskDelay(pdMS_TO_TICKS(20));

    gpio_set_level(pin, 1);
    esp_rom_delay_us(40);
    gpio_set_direction(pin, GPIO_MODE_INPUT);

    ESP_RETURN_ON_ERROR(dht_wait_while(pin, 1, DHT_TIMEOUT_US, nullptr), "DHT22", "sensor sem resposta");
    ESP_RETURN_ON_ERROR(dht_wait_while(pin, 0, DHT_TIMEOUT_US, nullptr), "DHT22", "pulso inicial baixo invalido");
    ESP_RETURN_ON_ERROR(dht_wait_while(pin, 1, DHT_TIMEOUT_US, nullptr), "DHT22", "pulso inicial alto invalido");

    for (int bit = 0; bit < 40; bit++) {
        uint32_t high_time_us = 0;

        ESP_RETURN_ON_ERROR(dht_wait_while(pin, 0, DHT_TIMEOUT_US, nullptr), "DHT22", "inicio de bit invalido");
        ESP_RETURN_ON_ERROR(dht_wait_while(pin, 1, DHT_TIMEOUT_US, &high_time_us), "DHT22", "tempo alto de bit invalido");

        data[bit / 8] <<= 1;
        if (high_time_us > 50) {
            data[bit / 8] |= 1;
        }
    }

    const uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        return ESP_ERR_INVALID_CRC;
    }

    const uint16_t humidity_raw = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    const uint16_t temperature_raw = ((static_cast<uint16_t>(data[2]) & 0x7F) << 8) | data[3];

    reading->humidity_percent = humidity_raw / 10.0f;
    reading->temperature_c = temperature_raw / 10.0f;
    if ((data[2] & 0x80) != 0) {
        reading->temperature_c *= -1.0f;
    }

    return ESP_OK;
}

int8_t quantize_to_int8(float value, float scale, int zero_point)
{
    int quantized = static_cast<int>(roundf(value / scale)) + zero_point;
    if (quantized > 127) {
        quantized = 127;
    }
    if (quantized < -128) {
        quantized = -128;
    }
    return static_cast<int8_t>(quantized);
}

bool run_hello_world(float x, float *predicted_y)
{
    if (input == nullptr || output == nullptr || input->type != kTfLiteInt8 || output->type != kTfLiteInt8) {
        ESP_LOGE(kTag, "Tensores inesperados no modelo Hello World");
        return false;
    }

    input->data.int8[0] = quantize_to_int8(x, input->params.scale, input->params.zero_point);

    if (interpreter->Invoke() != kTfLiteOk) {
        ESP_LOGE(kTag, "Falha ao executar inferencia para x=%.3f", x);
        return false;
    }

    const int8_t output_quantized = output->data.int8[0];
    *predicted_y = (static_cast<float>(output_quantized) - output->params.zero_point) * output->params.scale;
    return true;
}

const char *classify_comfort(float temperature_c, float humidity_percent)
{
    const ComfortCentroid *best = &kComfortCentroids[0];
    float best_distance = INFINITY;

    for (const ComfortCentroid &centroid : kComfortCentroids) {
        const float temp_delta = temperature_c - centroid.temperature_c;
        const float humidity_delta = (humidity_percent - centroid.humidity_percent) / 5.0f;
        const float distance = temp_delta * temp_delta + humidity_delta * humidity_delta;
        if (distance < best_distance) {
            best_distance = distance;
            best = &centroid;
        }
    }

    return best->label;
}

bool setup_tflite()
{
    tflite::InitializeTarget();

    model = tflite::GetModel(g_model);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(kTag, "Schema do modelo=%d, schema suportado=%d", model->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }

    static tflite::MicroMutableOpResolver<1> resolver;
    if (resolver.AddFullyConnected() != kTfLiteOk) {
        ESP_LOGE(kTag, "Nao foi possivel registrar FullyConnected");
        return false;
    }

    static tflite::MicroInterpreter static_interpreter(model, resolver, tensor_arena, kTensorArenaSize);
    interpreter = &static_interpreter;

    if (interpreter->AllocateTensors() != kTfLiteOk) {
        ESP_LOGE(kTag, "AllocateTensors falhou");
        return false;
    }

    input = interpreter->input(0);
    output = interpreter->output(0);
    ESP_LOGI(kTag, "TensorFlow Lite Micro pronto. Modelo Hello World carregado.");
    return true;
}

}  // namespace

extern "C" void app_main(void)
{
    if (!setup_tflite()) {
        return;
    }

    ESP_LOGI(kTag, "Atividade 4/6 - Hello World + Extra DHT22");
    ESP_LOGI(kTag, "DHT22 no GPIO%d; modelo extra gerado com %d amostras e %d classes",
             DHT_GPIO,
             kComfortDatasetRows,
             kComfortClassCount);

    while (true) {
        const float position = static_cast<float>(inference_count) / static_cast<float>(kInferencesPerCycle);
        const float x = position * kTwoPi;
        float predicted_y = 0.0f;

        if (run_hello_world(x, &predicted_y)) {
            ESP_LOGI(kTag, "Hello World | x=%.3f | predicted_sin=%.3f | expected_sin=%.3f",
                     x, predicted_y, sinf(x));
        }

        inference_count = (inference_count + 1) % kInferencesPerCycle;

        DhtReading reading = {};
        const esp_err_t err = dht_read(DHT_GPIO, &reading);
        if (err == ESP_OK) {
            ESP_LOGI(kTag, "Extra DHT22 | temp=%.1f C | umidade=%.1f %% | conforto=%s",
                     reading.temperature_c,
                     reading.humidity_percent,
                     classify_comfort(reading.temperature_c, reading.humidity_percent));
        } else {
            ESP_LOGW(kTag, "Falha ao ler DHT22: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
