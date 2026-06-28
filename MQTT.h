
#ifndef MQTTBME280_MQTT_CPP_H
#define MQTTBME280_MQTT_CPP_H
#include <tuple>

#include "exlibs/cJSON.h"
#include "lwip/apps/mqtt.h"
#include "pico/time.h"

class MQTT
{
public:
    struct Color { int r, g, b; float brightness; };
    // constructor handles connection to broker
    MQTT()
    {
        IP4_ADDR(&_broker_ip, 192, 168, 178, 100);
        _client = mqtt_client_new();
        _ci.client_id = "pico_w";
        err_t err =  mqtt_client_connect(_client, &_broker_ip, _port, mqtt_connection_cb, NULL, &_ci);
        sleep_ms(2000);
        _connected = err == (ERR_OK);
    };

    [[nodiscard]] bool is_connected() const { return _connected; }

    [[nodiscard]] bool new_data() const {return _received_new_color;}

    void clear_flag_new_data() {_received_new_color = false;}

    void set_new_color(const int red, const int green, const int blue)
    {
        _received_new_color = true;
        _received_color.r = red;
        _received_color.g = green;
        _received_color.b = blue;
    }

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

    static void parse_JSON_data_cb(void *arg, const u8_t *data, u16_t len, uint8_t flags)
    {
        char buf[256];
        memcpy(buf, data, len);
        buf[len] = '\0';
        // can't use 'this' here — but you can pass the instance via arg:
        MQTT *self = static_cast<MQTT*>(arg);
        cJSON *json = cJSON_Parse(buf);
        if (json) {
            self->_received_color.r = cJSON_GetObjectItem(json, "red")->valueint;
            self->_received_color.g  = cJSON_GetObjectItem(json, "green")->valueint;
            self->_received_color.b = cJSON_GetObjectItem(json, "blue")->valueint;
            self->_received_color.brightness  = cJSON_GetObjectItem(json, "brightness")->valueint / 255.0f;
            // parse "mode" similarly
            cJSON_Delete(json);
            self->_received_new_color = true;
            self->_rcv_color_raw = const_cast<u8_t*>(data);
            self->_rcv_color_raw_len = len;
        }
    }

    static void parse_light_cmd(void *arg, const u8_t *data, u16_t len,  uint8_t flags)
    {
        char buf[256];
        memcpy(buf, data, len);
        buf[len] = '\0';
        // can't use 'this' here — but you can pass the instance via arg:
        MQTT *self = static_cast<MQTT*>(arg);

        cJSON *root = cJSON_Parse(buf);
        if (!root) {
            // parse error — cJSON_GetErrorPtr() shows where
            return;
        }

        // "state": "ON" or "OFF"
        cJSON *state = cJSON_GetObjectItemCaseSensitive(root, "state");
        if (cJSON_IsString(state) && state->valuestring) {
            bool off = (strcmp(state->valuestring, "OFF") == 0);
            if (off)
            {
                self->_received_color.r =0;
                self->_received_color.g =0;
                self->_received_color.b =0;
                cJSON_Delete(root);
                self->_received_new_color = true;
                return;
            }
        }

        cJSON *brightness = cJSON_GetObjectItemCaseSensitive(root, "brightness");
        if (cJSON_IsNumber(brightness)) {
            printf("%f\n",static_cast<float>(brightness->valueint));
            self->_received_color.brightness = static_cast<float>(brightness->valueint)/255.0f;       // 0..255
        }

        // "color": { "r":..,"g":..,"b":.. }
        cJSON *color = cJSON_GetObjectItemCaseSensitive(root, "color");
        if (cJSON_IsObject(color)) {
            cJSON *r = cJSON_GetObjectItemCaseSensitive(color, "r");
            cJSON *g = cJSON_GetObjectItemCaseSensitive(color, "g");
            cJSON *b = cJSON_GetObjectItemCaseSensitive(color, "b");
            if (cJSON_IsNumber(r) && cJSON_IsNumber(g) && cJSON_IsNumber(b)) {
                self->_received_color.r = (uint8_t)r->valueint;
                self->_received_color.g = (uint8_t)g->valueint;
                self->_received_color.b = (uint8_t)b->valueint;
            }
        }

        cJSON_Delete(root);
        self->_received_new_color = true;
    }

    void sub_to_led_topic(void* arg = nullptr)
    {
        mqtt_subscribe(_client, "led/living/1/set", 0, NULL, NULL);
        mqtt_set_inpub_callback(_client, NULL, parse_light_cmd, arg);
        printf("subscribed to topic 'led/living/1/set'\n");
    }

    [[nodiscard]] std::tuple<int, int, int, float> get_led_values() const
    {
        return std::make_tuple(_received_color.r, _received_color.g, _received_color.b, _received_color.brightness);
    }

    void publish(const char* topic, const char* payload, short unsigned int payload_len)
    {
        mqtt_publish(_client, topic, payload, payload_len,
              0,    // QoS 0
              0,    // not retained
              mqtt_pub_request_cb, NULL);
    }

    void publish_retain(const char* topic, const char* payload, short unsigned int payload_len)
    {
        mqtt_publish(_client, topic, payload, payload_len,
              0,    // QoS 0
              1,    // retained
              mqtt_pub_request_cb, NULL);
    }

private:
    short unsigned int _port = 1883;
    ip_addr_t _broker_ip;
    mqtt_client_t* _client;
    mqtt_connect_client_info_t _ci = {};
    bool _received_new_color = false;
    bool _connected = false;
    Color _received_color = {0,0,0,0.2}; // sea green
    u8_t* _rcv_color_raw;
    u16_t _rcv_color_raw_len;

};
#endif //MQTTBME280_MQTT_CPP_H