
#ifndef MQTTBME280_MQTT_CPP_H
#define MQTTBME280_MQTT_CPP_H
#include <cstring>
#include "exlibs/cJSON.h"
#include "lwip/apps/mqtt.h"
#include "pico/time.h"

class MQTT
{
public:
    struct Color { int r, g, b; float brightness; };
    MQTT()
    {
        IP4_ADDR(&_broker_ip, 192, 168, 178, 100);
        _client = mqtt_client_new();
        _ci.client_id = "pico_w";
        mqtt_client_connect(_client, &_broker_ip, _port, mqtt_connection_cb, NULL, &_ci);
        sleep_ms(2000);
    };

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

    void parse_JSON_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags)
    {
        char buf[256];
        memcpy(buf, data, len);
        buf[len] = '\0';

        cJSON *json = cJSON_Parse(buf);
        if (json) {
            _received_color.r = cJSON_GetObjectItem(json, "red")->valueint;
            _received_color.g  = cJSON_GetObjectItem(json, "green")->valueint;
            _received_color.b = cJSON_GetObjectItem(json, "blue")->valueint;
            _received_color.brightness  = cJSON_GetObjectItem(json, "brightness")->valueint / 255.0f;
            // parse "mode" similarly
            cJSON_Delete(json);
            _received_new_color = true;
            _rcv_color_raw = const_cast<u8_t*>(data);
            _rcv_color_raw_len = len;
        }
    }

private:
    short unsigned int _port = 1883;
    ip_addr_t _broker_ip;
    mqtt_client_t* _client;
    mqtt_connect_client_info_t _ci = {};
    bool _received_new_color = false;
    Color _received_color;
    u8_t* _rcv_color_raw;
    u16_t _rcv_color_raw_len;


};
#endif //MQTTBME280_MQTT_CPP_H