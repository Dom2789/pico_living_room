#pragma once
#include "pico/binary_info/code.h"

class LED {
public:
    LED(int red = 0, int green = 0, int blue = 0, float brightness = 1.0f)
        : _red(red), _green(green), _blue(blue), _brightness(brightness) {}

    uint8_t get_red()        const { return _red; }
    uint8_t get_green()      const { return _green; }
    uint8_t get_blue()       const { return _blue; }
    float get_brightness() const { return _brightness; }

    void set_red(const int r)
    {
        if (r >255) _red = 255;
        else if (r < 0) _red = 0;
        else _red = r;
    }

    void set_green(const int g)
    {
        if (g > 255) _green = 255;
        else if (g < 0) _green = 0;
        else _green = g;
    }

    void set_blue(const int b)
    {
        if (b > 255) _blue = 255;
        else if (b < 0) _blue = 0;
        else _blue = b;
    }

    void set_brightness(float br)
    {
        if (br > 1.0f) _brightness = 1.0f;
        else if (br < 0.0f) _brightness = 0.0f;
        else _brightness = br;
    }

    void set_led_values(int red, int green, int blue, float brightness)
    {
        set_red(red);
        set_green(green);
        set_blue(blue);
        set_brightness(brightness);
    }
    const char* to_string() const {
        static char buf[52];
        snprintf(buf, sizeof(buf), "{brightness: %.2f, red: %i, green: %i, blue: %i}",
                 _brightness, _red, _green, _blue);
        return buf;
    }

    const char* to_mqtt_json(const char* color_mode = "rgb") const
    {
        static char buf[96];
        if (_red == 0 && _green == 0 && _blue == 0)
        {
            snprintf(buf, sizeof(buf), "{\"state\":\"OFF\"}");
        } else
        {
            snprintf(buf, sizeof(buf),
                "{\"state\":\"%s\",\"brightness\":%u,"
                "\"color\":{\"r\":%u,\"g\":%u,\"b\":%u},"
                "\"color_mode\":\"%s\"}",
                "ON",
                static_cast<unsigned>(_brightness * 255.0f + 0.5f),  // float → 0..255
                _red, _green, _blue,
                color_mode);
        }

        return buf;
    }

    uint32_t ws2812_rgb_scaled() {
        const uint8_t  r = (uint8_t)(_red * _brightness);
        const uint8_t  g = (uint8_t)(_green * _brightness);
        const uint8_t  b = (uint8_t)(_blue * _brightness);
        return ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
    }

    uint32_t ws2812_rgb() {
        return ((uint32_t)_green << 16) | ((uint32_t)_red << 8) | _blue;
    }

    void set_leds_form_MQTT(const uint led_strip_len, void(* put_pixel)(uint32_t))
    {
        for (int i = 0; i < led_strip_len; ++i)
        {
            put_pixel(this->ws2812_rgb_scaled());
        }
        sleep_us(100);  // latch
        printf("%s\n", this->to_string());
    }

private:
    uint8_t _red, _green, _blue;
    float _brightness;
};
