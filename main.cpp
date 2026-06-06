#include <array>
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

// I2C defines
#define I2C_PORT i2c0
#define I2C_SDA 20
#define I2C_SCL 21

// BME280
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

// WS2812 constants
constexpr bool WS2812_IS_RGBW = false;
constexpr uint WS2812_PIN = 0;
constexpr uint WS2812_LEN = 30;

static LED led;

static PIO ws2812_pio = pio0;
static uint ws2812_sm = 0;

static inline void ws2812_put_pixel(uint32_t grb) {
    pio_sm_put_blocking(ws2812_pio, ws2812_sm, grb << 8u);
}

// Pack RGB into GRB word
static inline uint32_t ws2812_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
}

static inline uint32_t ws2812_rgb_scaled(uint8_t r, uint8_t g, uint8_t b, float brightness = 1.0f) {
    r = (uint8_t)(r * brightness);
    g = (uint8_t)(g * brightness);
    b = (uint8_t)(b * brightness);
    return ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
}

void pattern_random(PIO pio, uint sm, uint len, uint t) {
    if (t % 8)
        return;
    for (uint i = 0; i < len; ++i)
        ws2812_put_pixel(static_cast<uint32_t>(rand()*0.1));
    printf("pattern call\n");
}

void set_leds()
{
    float brightness = .25f;
    short blue = 50;
    short green = 180;
    short red = 255;
    struct RGB { short r, g, b; };
    RGB leds[30];

    for (int i = 0; i < WS2812_LEN; ++i)
    {
        //printf("%.2f\n", brightness);
        //printf("%i: %i\n", i+1, blue);
        ws2812_put_pixel(ws2812_rgb_scaled(red, green, blue, brightness));
        leds[i] = {255,green,blue};
        blue -= 2;
        green -= 1;
        if (blue < 0) blue = 0;
    }

    for (int i = 0; i < WS2812_LEN; ++i)
    {
        printf("%i: (%i, %i, %i)\n", i+1, leds[i].r, leds[i].g, leds[i].b);
    }
}

void set_leds_to_one_color()
{
    float brightness = .25f;
    for (int i = 0; i < WS2812_LEN; ++i)
    {
        ws2812_put_pixel(ws2812_rgb_scaled(255, 160, 10, brightness));
    }
    sleep_us(100);  // latch
}

void set_leds_form_MQTT()
{
    for (int i = 0; i < WS2812_LEN; ++i)
    {
        ws2812_put_pixel(led.ws2812_rgb_scaled());
    }
    sleep_us(100);  // latch
}

// MQTT
static mqtt_client_t *mqtt_client;

static void mqtt_connection_cb(mqtt_client_t *client, void *arg, mqtt_connection_status_t status) {
    if (status == MQTT_CONNECT_ACCEPTED) {
        printf("MQTT connected\n");
    } else {
        printf("MQTT connection failed: %d\n", status);
    }
}

static void mqtt_pub_request_cb(void *arg, err_t result) {
    if (result == ERR_OK) {
        printf("Publish successful\n");
    }
}

static volatile bool leds_dirty = false;
static u8_t* rcv_data;
static u16_t rcv_len;

//static uint8_t led_r = 0, led_g = 0, led_b = 0;

// MQTT incoming data callback
static void mqtt_incoming_data_cb_print(void *arg, const u8_t *data, u16_t len, u8_t flags) {
    // e.g. payload "255,0,128"
    //sscanf((const char *)data, "%hhu,%hhu,%hhu", &led_r, &led_g, &led_b);
    printf("%.*s\n", len, reinterpret_cast<const char*>(data));
    rcv_data = const_cast<u8_t*>(data);
    rcv_len = len;
    leds_dirty = true;
}
// parse JSON to LED
static void mqtt_incoming_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags) {
    char buf[256];
    memcpy(buf, data, len);
    buf[len] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (json) {
        led.setRed(cJSON_GetObjectItem(json, "red")->valueint);
        led.setGreen(cJSON_GetObjectItem(json, "green")->valueint);
        led.setBlue(cJSON_GetObjectItem(json, "blue")->valueint);
        led.setBrightness(cJSON_GetObjectItem(json, "brightness")->valueint / 255.0f);
        // parse "mode" similarly
        cJSON_Delete(json);
        rcv_data = const_cast<u8_t*>(data);
        rcv_len = len;
        leds_dirty = true;
    }
}

int main()
{
    stdio_init_all();

    // Initialise WS2812
    uint offset = pio_add_program(ws2812_pio, &ws2812_program);
    ws2812_program_init(ws2812_pio, ws2812_sm, offset, WS2812_PIN, 800000, WS2812_IS_RGBW);

    // Initialise the Wi-Fi chip
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    // I2C Initialisation. Using it at 400Khz.
    i2c_init(I2C_PORT, 400*1000);
    
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    // For more examples of I2C use see https://github.com/raspberrypi/pico-examples/tree/master/i2c

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
        break;
    }
    if (retrys >= 4 ) return 1;

    // BME 280 init
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // BME280 I2C address: 0x76 (SDO→GND) or 0x77 (SDO→VDD)
    uint8_t dev_addr = BME280_I2C_ADDR_PRIM; // 0x76

    struct bme280_dev dev = {
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

    struct bme280_settings settings = {
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

    // MQTT connection to broker
    ip_addr_t broker_ip;
    // Replace with your broker's IP
    IP4_ADDR(&broker_ip, 192, 168, 178, 100);
    mqtt_client = mqtt_client_new();
    struct mqtt_connect_client_info_t ci = {};
    ci.client_id = "pico_w";
    // ci.client_user = "user";   // if auth required
    // ci.client_pass = "pass";
    mqtt_client_connect(mqtt_client, &broker_ip, 1883, mqtt_connection_cb, NULL, &ci);
    // Wait a moment for connection (in a real app, do this in the callback)
    sleep_ms(2000);
    // Subscribe
    mqtt_subscribe(mqtt_client, "pico/leds", 0, NULL, NULL);
    mqtt_set_inpub_callback(mqtt_client, NULL, mqtt_incoming_data_cb, NULL);

    //set_leds();
    set_leds_to_one_color();

    const char *payload = "hello from pico";
    uint32_t last_publish = 0;
    uint32_t last_read_bme = 0;
    constexpr  uint32_t interval_publish = 10'000; // time between publishes in milliseconds
    constexpr  uint32_t interval_read_bme = 2'000;
    while (true)
    {
        uint32_t now = to_ms_since_boot(get_absolute_time());

        if (now - last_publish > interval_publish)
        {
            last_publish = now;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            mqtt_publish(mqtt_client, "pico/test", payload, strlen(payload),
                         0,    // QoS 0
                         0,    // not retained
                         mqtt_pub_request_cb, NULL);
            printf("%s\n",payload);
            sleep_ms(50);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        }

        if (now - last_read_bme > interval_read_bme)
        {
            last_read_bme = now;
            bme280_set_sensor_mode(BME280_POWERMODE_FORCED, &dev);
            dev.delay_us(meas_delay_us, dev.intf_ptr);

            struct bme280_data data;
            rslt = bme280_get_sensor_data(BME280_ALL, &data, &dev);
            if (rslt == BME280_OK) {
                printf("Temp: %.2f °C  Humidity: %.2f %%  Pressure: %.2f hPa\n",
                       data.temperature, data.humidity, data.pressure / 100.0);
            } else {
                printf("Read error: %d\n", rslt);
            }
        }

        if (leds_dirty)
        {
            mqtt_publish(mqtt_client, "pico/mirror",led.toString() , 128,
                          0,    // QoS 0
                          0,    // not retained
                          mqtt_pub_request_cb, NULL);
            set_leds_form_MQTT();
            leds_dirty = false;
        }

    }
}
