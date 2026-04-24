#include <stdio.h>
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

#include "esp_wifi.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_netif.h"

#define WIFI_SSID ""
#define WIFI_PASS ""

#define Samples 240
#define R_DIV_S 330000
#define R_DIV_I 10000
#define Fac_rel ((R_DIV_S + R_DIV_I) / (float)R_DIV_I)
#define ADC_TO_VOLT (3.3f / 4095.0f)

int16_t adc_samples[Samples];
float vol_samples[Samples];

static esp_mqtt_client_handle_t mqtt_client = NULL;

void adc_init()
{
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
}

void sample_ADC(int16_t *buffer)
{
    const TickType_t delay = pdMS_TO_TICKS(4);

    for (int i = 0; i < Samples; i++)
    {
        int raw = adc1_get_raw(ADC1_CHANNEL_6);
        buffer[i] = raw - 2048;
        vTaskDelay(delay);
    }
}

void Sample_to_Volt(const int16_t *adc_buffer, float *volt_buffer)
{
    for (int i = 0; i < Samples; i++)
    {
        volt_buffer[i] = adc_buffer[i] * ADC_TO_VOLT * Fac_rel;
    }
}

float Cal_RMS(const float *buffer)
{
    float suma = 0.0f;

    for (int i = 0; i < Samples; i++)
    {
        suma += buffer[i] * buffer[i];
    }

    return sqrtf(suma / Samples);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event->event_id)
    {
        case MQTT_EVENT_CONNECTED:
            printf("MQTT conectado\n");
            break;

        case MQTT_EVENT_DISCONNECTED:
            printf("MQTT desconectado\n");
            break;

        case MQTT_EVENT_PUBLISHED:
            printf("Mensaje publicado\n");
            break;

        default:
            break;
    }
}

void mqtt_app_start(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://mqtt.eclipseprojects.io",
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

void mqtt_send_voltage(float voltage)
{
    if (mqtt_client == NULL) return;

    char msg[32];
    snprintf(msg, sizeof(msg), "%.2f", voltage);

    esp_mqtt_client_publish(
        mqtt_client,
        "yadiel/voltimetro/vrms",
        msg,
        0,
        1,
        0
    );
}

static const char *WIFI_TAG = "wifi";

void wifi_init(void)
{
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(WIFI_TAG, "Conectando a WiFi...");
}

void app_main(void)
{
    adc_init();

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init();
    mqtt_app_start();

    while (1)
    {
        sample_ADC(adc_samples);
        Sample_to_Volt(adc_samples, vol_samples);

        float Vrms = Cal_RMS(vol_samples);

        printf("Vrms: %.2f V\n", Vrms);

        mqtt_send_voltage(Vrms);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}