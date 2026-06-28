#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/mqtt.h"
#include "lwip/ip_addr.h"
#include "ws2812.pio.h"
#include "exlibs/cJSON.h"
#include "LED.h"
#include "pico/time.h"
#include "exlibs/bme280.h"
#include "lwip/apps/sntp.h"
#include "MQTT.h"
#include "exlibs/bme280.h"

// I2C defines
#define I2C_PORT i2c0
#define I2C_SDA 20
#define I2C_SCL 21

//MQTT
static MQTT *mqtt = nullptr; // create variable for global accessibility

// WS2812 constants
constexpr bool WS2812_IS_RGBW = false;
constexpr uint WS2812_PIN = 0;
constexpr uint WS2812_LEN = 30;
// sea green
constexpr uint initial_red = 59;
constexpr uint initial_green = 122;
constexpr uint initial_blue = 87;

static LED led(initial_red, initial_green, initial_blue, 0.2);

static PIO ws2812_pio = pio0;
static uint ws2812_sm = 0;

static void ws2812_put_pixel(uint32_t grb)
{
    pio_sm_put_blocking(ws2812_pio, ws2812_sm, grb << 8u);
}

//BME280
// BME280 I2C address: 0x76 (SDO→GND) or 0x77 (SDO→VDD)
static uint8_t dev_addr = BME280_I2C_ADDR_PRIM; // 0x76
static bme280_dev dev;
uint32_t meas_delay_us;

static BME280_INTF_RET_TYPE bme280_i2c_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    uint8_t dev_addr = *(uint8_t *)intf_ptr;
    if (i2c_write_blocking(I2C_PORT, dev_addr, &reg_addr, 1, true) < 0)
        return BME280_E_COMM_FAIL;
    if (i2c_read_blocking(I2C_PORT, dev_addr, reg_data, len, false) < 0)
        return BME280_E_COMM_FAIL;
    return BME280_OK;
}

static BME280_INTF_RET_TYPE bme280_i2c_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    uint8_t dev_addr = *(uint8_t *)intf_ptr;
    uint8_t buf[len + 1];
    buf[0] = reg_addr;
    for (uint32_t i = 0; i < len; i++) buf[i + 1] = reg_data[i];
    if (i2c_write_blocking(I2C_PORT, dev_addr, buf, len + 1, false) < 0)
        return BME280_E_COMM_FAIL;
    return BME280_OK;
}

static void bme280_delay_us(uint32_t period, void *intf_ptr)
{
    (void)intf_ptr;
    sleep_us(period);
}

int sensor_init()
{
    dev = {
        .intf     = BME280_I2C_INTF,
        .intf_ptr = &dev_addr,
        .read     = bme280_i2c_read,
        .write    = bme280_i2c_write,
        .delay_us = bme280_delay_us,
    };

    int8_t rslt = bme280_init(&dev);
    if (rslt != BME280_OK) {
        printf("BME280 init failed: %d\n", rslt);
        return -1;
    }

    bme280_settings settings = {
        .osr_p        = BME280_OVERSAMPLING_1X,
        .osr_t        = BME280_OVERSAMPLING_1X,
        .osr_h        = BME280_OVERSAMPLING_1X,
        .filter       = BME280_FILTER_COEFF_OFF,
        .standby_time = BME280_STANDBY_TIME_0_5_MS,
    };
    bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS, &settings, &dev);

    uint32_t meas_delay_us;
    bme280_cal_meas_delay(&meas_delay_us, &settings);

    printf("BME280 ready\n");
    return 0;
}

// WIFI
static int connect_to_WIFI()
{
    // Initialise the Wi-Fi chip
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    // Enable wifi station
    cyw43_arch_enable_sta_mode();
    sleep_ms(1000);
    printf("%s\n",WIFI_SSID);
    printf("Connecting to Wi-Fi...\n");

    short retrys = 0;
    while (retrys < 4)
    {
        retrys++;
        if (cyw43_arch_wifi_connect_timeout_ms(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000))
        {
            printf("failed to connect.\n");
            continue;
        }

        printf("Connected.\n");
        // Read the ip address in a human readable way
        uint8_t *ip_address = (uint8_t*)&(cyw43_state.netif[0].ip_addr.addr);
        printf("IP address %d.%d.%d.%d\n", ip_address[0], ip_address[1], ip_address[2], ip_address[3]);
        return 0;
    }
    return -1;
}

int main()
{
    stdio_init_all();

    // init i2c and gpios
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Initialise WS2812
    uint offset = pio_add_program(ws2812_pio, &ws2812_program);
    ws2812_program_init(ws2812_pio, ws2812_sm, offset, WS2812_PIN, 800000, WS2812_IS_RGBW);

    // BME 280 init
    bool sensor_initialized = not(sensor_init());

    //set_leds();
    led.set_leds_form_MQTT(WS2812_LEN, ws2812_put_pixel);

    // WIFI connection afterwards MQTT and timesync
    bool WIFI_is_connected = not(connect_to_WIFI());
    if (WIFI_is_connected)
    {
        // create mqtt-instance and link to global pointer
        static MQTT mqtt_instance;
        mqtt = &mqtt_instance;
        if (mqtt->is_connected())
        {
            // send initial state to home assistant
            mqtt->set_new_color(initial_red, initial_green, initial_blue);
            mqtt->sub_to_led_topic(mqtt);
        }
    }

    // super loop
    uint32_t last_publish = 0;
    constexpr  uint32_t interval_publish = 15'000; // time between publishes in milliseconds
    static char payload_bme280[60];

    while (true)
    {
        const uint32_t now = to_ms_since_boot(get_absolute_time());

        if (now - last_publish > interval_publish && sensor_initialized && WIFI_is_connected)
        {
            last_publish = now;

            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

            bme280_set_sensor_mode(BME280_POWERMODE_FORCED, &dev);
            dev.delay_us(meas_delay_us, dev.intf_ptr);
            bme280_data data;
            int8_t rslt = bme280_get_sensor_data(BME280_ALL, &data, &dev);
            if (rslt == BME280_OK) {
                snprintf(payload_bme280, sizeof(payload_bme280),
                        "{\"temp\":%.2f,\"pressure\":%.2f,\"humidity\":%.2f}",
                        data.temperature, data.pressure / 100.0, data.humidity);

                if (mqtt != nullptr && mqtt->is_connected())
                {
                    mqtt->publish_retain("climate/living/1", payload_bme280, strlen(payload_bme280));
                }

                printf("%s\n",payload_bme280);
            } else {
                printf("Read error: %d\n", rslt);
            }

            sleep_ms(50);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        }

        if (mqtt != nullptr && mqtt-> new_data())
        {
            auto [r,g,b,brightness] = mqtt->get_led_values();
            led.set_led_values(r, g, b, brightness);
            led.set_leds_form_MQTT(WS2812_LEN, ws2812_put_pixel);
            mqtt->publish_retain("led/living/1/state",led.to_mqtt_json() , strlen(led.to_mqtt_json()));
            mqtt->clear_flag_new_data();
        }

    }
}
