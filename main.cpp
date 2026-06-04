#include <array>
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/mqtt.h"
#include "lwip/ip_addr.h"
#include "ws2812.pio.h"

// I2C defines
#define I2C_PORT i2c0
#define I2C_SDA 8
#define I2C_SCL 9

// WS2812 defines
#define WS2812_PIN 0
#define WS2812_IS_RGBW false
#define WS2812_LEN 30

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
        ws2812_put_pixel(ws2812_rgb_scaled(255, green, blue, brightness));
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
}


int main()
{
    stdio_init_all();

    // Initialise WS2812
    uint offset = pio_add_program(ws2812_pio, &ws2812_program);
    ws2812_program_init(ws2812_pio, ws2812_sm, offset, WS2812_PIN, 800000, WS2812_IS_RGBW);
    /*
    ws2812_put_pixel(ws2812_rgb_scaled(255, 0, 0, 1.0));
    ws2812_put_pixel(ws2812_rgb(255, 255, 0));
    //for (int i = 0; i < 27; ++i) ws2812_put_pixel(ws2812_rgb(0, 0, 0));
    ws2812_put_pixel(ws2812_rgb(0, 0, 255));
    */

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

    //set_leds();
    set_leds_to_one_color();

    while (true) {
        const char *payload = "hello from pico";
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        mqtt_publish(mqtt_client, "pico/test", payload, strlen(payload),
                     0,    // QoS 0
                     0,    // not retained
                     mqtt_pub_request_cb, NULL);

        printf("%s\n",payload);
        sleep_ms(50);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        //pattern_random(ws2812_pio, ws2812_sm, WS2812_LEN, 8);
        sleep_ms(9950);
    }
}
