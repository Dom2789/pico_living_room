#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/cyw43_arch.h"
#include "lwip/apps/mqtt.h"
#include "lwip/ip_addr.h"

// I2C defines
// This example will use I2C0 on GPIO8 (SDA) and GPIO9 (SCL) running at 400KHz.
// Pins can be changed, see the GPIO function select table in the datasheet for information on GPIO assignments
#define I2C_PORT i2c0
#define I2C_SDA 8
#define I2C_SCL 9

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

int main()
{
    stdio_init_all();

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

    while (true) {
        const char *payload = "hello from pico";
        mqtt_publish(mqtt_client, "pico/test", payload, strlen(payload),
                     0,    // QoS 0
                     0,    // not retained
                     mqtt_pub_request_cb, NULL);

        printf("%s\n",payload);
        sleep_ms(10000);
    }
}
