#pragma once
#include <string>

class LED {
public:
    LED(int red = 0, int green = 0, int blue = 0, float brightness = 1.0f)
        : _red(red), _green(green), _blue(blue), _brightness(brightness) {}

    uint8_t getRed()        const { return _red; }
    uint8_t getGreen()      const { return _green; }
    uint8_t getBlue()       const { return _blue; }
    float getBrightness() const { return _brightness; }

    void setRed(const int r)
    {
        if (r >255) _red = 255;
        else if (r < 0) _red = 0;
        else _red = r;
    }
    void setGreen(const int g)
    {
        if (g > 255) _green = 255;
        else if (g < 0) _green = 0;
        else _green = g;
    }
    void setBlue(const int b)
    {
        if (b > 255) _blue = 255;
        else if (b < 0) _blue = 0;
        else _blue = b;
    }
    void setBrightness(float br)
    {
        if (br > 1.0f) _brightness = 1.0f;
        else if (br < 0.0f) _brightness = 0.0f;
        else _brightness = br;
    }
    const char* toString() const {
        static char buf[128];
        snprintf(buf, sizeof(buf), "{brightness: %.2f, red: %i, green: %i, blue: %i}",
                 _brightness, _red, _green, _blue);
        return buf;
    }
private:
    uint8_t _red, _green, _blue;
    float _brightness;
};
