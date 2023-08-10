#include <Arduino.h>

#include <Wire.h>
#include "SSD1306Wire.h" 

#define OLED_SCL    40
#define OLED_SDA    41
#define OLED_ADDR   0x3C

SSD1306Wire oled(OLED_ADDR, OLED_SDA, OLED_SCL);

void setup() {
    
    // Connect to USB serial
    Serial.begin();

    // Initialize the display
    oled.init();
    oled.setFont(ArialMT_Plain_10);
    oled.flipScreenVertically();
    oled.setTextAlignment(TEXT_ALIGN_LEFT);
}

void loop() {

    oled.clear();
    oled.drawString(0, 0, "Testing!");
    oled.display();

    Serial.println("TEST");
    delay(500);
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}