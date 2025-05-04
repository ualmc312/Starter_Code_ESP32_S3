const byte adcChns[] = {19, 20, 14};         // define the ADC channels
const byte ledPins[] = {38, 39, 40, 1, 2, 42}; //B, G, R
int colors[] = {0, 0, 0};                    // red, green, blue values

void setup() {
  for (int i = 0; i < 6; i++) {
    ledcAttachChannel(ledPins[i], 1000, 8, i); // 1kHz, 8-bit PWM
  }
}

void loop() {
  for (int i = 0; i < 6; i++) {
    colors[i % 3] = map(analogRead(adcChns[i % 3]), 0, 4095, 0, 255);
    if (i < 3) {
      ledcWrite(ledPins[i], abs(255 -colors[i % 3]));
    } else {
      ledcWrite(ledPins[i], colors[i % 3]);
    }
  }
  delay(10);
}
