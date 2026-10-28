#include <M5Unified.h>

void setup() {
    auto config = M5.config();
    config.serial_baudrate = 115200;
    config.internal_mic = false;
    config.internal_spk = false;
    M5.begin(config);

    M5.Display.setRotation(1);
    M5.Display.setBrightness(128);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextSize(3);
    M5.Display.drawString("Hello, CoreS3 SE!", M5.Display.width() / 2, 90);
    M5.Display.setTextSize(2);
    M5.Display.drawString("Home Control", M5.Display.width() / 2, 145);
    Serial.println("DISPLAY_READY: Hello, CoreS3 SE!");
}

void loop() {
    M5.update();
    delay(20);
}
