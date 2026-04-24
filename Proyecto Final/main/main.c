#include <stdio.h>
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
#include "esp_log.h"

// ── ADC ───────────────────────────────────────────────────────────
#define ADC_CHANNEL     ADC1_CHANNEL_6
#define ADC_ATTEN       ADC_ATTEN_DB_12
#define ADC_WIDTH       ADC_WIDTH_BIT_12
#define SAMPLES         1000
#define V_REF_MV        1100

// ── MQTT / WiFi ───────────────────────────────────────────────────
#define WIFI_SSID       "Campus_ITLA"
#define MQTT_BROKER     "mqtt://broker.hivemq.com"
#define MQTT_PORT       1883
#define MQTT_TOPIC      "itla/mecatronica/Jeran"

// ── Bit de evento: WiFi conectado ─────────────────────────────────
#define WIFI_CONNECTED_BIT  BIT0
static EventGroupHandle_t wifi_events;

static esp_mqtt_client_handle_t client;
static esp_adc_cal_characteristics_t adc_chars;
static const char *TAG = "VOLTIMETRO";

// ══════════════════════════════════════════════════════════════════
//  Handler de eventos WiFi — setea el bit cuando hay IP
// ══════════════════════════════════════════════════════════════════
static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *data)
{
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi desconectado, reintentando...");
        esp_wifi_connect();
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "IP obtenida: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_events, WIFI_CONNECTED_BIT);  // ← WiFi listo
    }
}

// ══════════════════════════════════════════════════════════════════
//  Red: WiFi + MQTT (espera IP antes de iniciar MQTT)
// ══════════════════════════════════════════════════════════════════
static void network_init(void)
{
    wifi_events = xEventGroupCreate();

    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    // Registrar handlers de eventos
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,  &wifi_event_handler, NULL);
    esp_event_handler_register(IP_EVENT,   IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL);

    wifi_init_config_t wifi_init = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wifi_init);

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid = WIFI_SSID,
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_wifi_start();
    esp_wifi_connect();

    // ── Esperar hasta tener IP antes de continuar ─────────────────
    ESP_LOGI(TAG, "Esperando conexión WiFi...");
    xEventGroupWaitBits(wifi_events, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi conectado, iniciando MQTT...");

    // ── Ahora sí iniciar MQTT ─────────────────────────────────────
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri  = MQTT_BROKER,
        .broker.address.port = MQTT_PORT,
    };
    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);

    // Pequeña espera para que MQTT establezca la conexión
    vTaskDelay(pdMS_TO_TICKS(2000));
}

// ══════════════════════════════════════════════════════════════════
//  ADC: calibración
// ══════════════════════════════════════════════════════════════════
static void adc_init(void)
{
    adc1_config_width(ADC_WIDTH);
    adc1_config_channel_atten(ADC_CHANNEL, ADC_ATTEN);
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN, ADC_WIDTH, V_REF_MV, &adc_chars);
}

// ══════════════════════════════════════════════════════════════════
//  Muestreo: calcula offset DC y Vrms
// ══════════════════════════════════════════════════════════════════
static float medir_vrms(float *out_offset)
{
    float offset = 0.0f;

    for (int i = 0; i < SAMPLES; i++) {
        uint32_t raw = adc1_get_raw(ADC_CHANNEL);
        offset += esp_adc_cal_raw_to_voltage(raw, &adc_chars) / 1000.0f;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    offset /= SAMPLES;

    float sum = 0.0f;
    for (int i = 0; i < SAMPLES; i++) {
        uint32_t raw = adc1_get_raw(ADC_CHANNEL);
        float v = (esp_adc_cal_raw_to_voltage(raw, &adc_chars) / 1000.0f) - offset;
        sum += v * v;
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (out_offset) *out_offset = offset;
    return sqrtf(sum / SAMPLES);
}

// ══════════════════════════════════════════════════════════════════
//  MQTT: publica en topic de Yadiel
// ══════════════════════════════════════════════════════════════════
static void publicar_vrms(float vrms)
{
    if (client == NULL) return;

    char payload[64];
    snprintf(payload, sizeof(payload), "{\"vrms\": %.3f, \"status\": \"Yuppiiiii\"}", vrms);

    esp_mqtt_client_publish(client, MQTT_TOPIC, payload, 0, 1, 0);
    ESP_LOGI(TAG, "Publicado en [%s] → %s", MQTT_TOPIC, payload);
}

// ══════════════════════════════════════════════════════════════════
//  app_main
// ══════════════════════════════════════════════════════════════════
void app_main(void)
{
    network_init();
    adc_init();

    ESP_LOGI(TAG, "Iniciando medición RMS...");

    while (1)
    {
        float offset, vrms;
        vrms = medir_vrms(&offset);

        ESP_LOGI(TAG, "Offset: %.3f V | Vrms: %.3f V", offset, vrms);
        publicar_vrms(vrms);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}