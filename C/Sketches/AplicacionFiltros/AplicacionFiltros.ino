#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "esp_camera.h"
#define CAMERA_MODEL_ESP32S3_EYE
#include "camera_pins.h"


// Replace with your network credentials
const char* ssid = "P6TDRMC";
const char* password = "88a4O07;";
const byte adcChns[] = {19, 20, 14};         // define the ADC channels
const byte ledPins[] = {38, 39, 40, 1, 2, 42}; //R, G, B
int colors[] = {0, 0, 0};                    // red, green, blue values

// Async web server on port 80
AsyncWebServer server(80);

// HTML Page in PROGMEM
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
  <head>
    <meta charset="UTF-8" />
    <title>ESP32 CAM RGB Filter</title>
    <style>
      body { font-family: sans-serif; text-align: center; margin-top: 20px; }
      img { margin-top: 10px; width: 320px; height: 240px; }
      .rgb-values { font-weight: bold; margin-top: 10px; }
    </style>
  </head>
  <body>
    <h2>ESP32 Filtered Capture</h2>
    <button onclick="capture()">Capture with Filter</button>
    <div class="rgb-values" id="rgb">R: 0, G: 0, B: 0</div>
    <img id="img" src="/filtered.jpg" />
    <script>
      function capture() {
        fetch("/rgb").then(res => res.json()).then(rgb => {
          document.getElementById("rgb").innerText = `R: ${rgb.r}, G: ${rgb.g}, B: ${rgb.b}`;
          document.getElementById("img").src = "/filtered.jpg?_=" + new Date().getTime();
        });
      }
    </script>
  </body>
</html>
)rawliteral";

// ========== Filter Function ==========
void applyColorFilter(uint8_t* buf, size_t len, int red, int green, int blue) {
  for (size_t i = 0; i < len; i += 2) {  // RGB565 format (2 bytes per pixel)
    uint16_t pixel = buf[i] | (buf[i+1] << 8);
    
    // Extract RGB565 components
    uint8_t r = ((pixel >> 11) & 0x1F) << 3;
    uint8_t g = ((pixel >> 5) & 0x3F) << 2;
    uint8_t b = (pixel & 0x1F) << 3;

    // Apply the color filter
    r = (r * red) / 255;
    g = (g * green) / 255;
    b = (b * blue) / 255;

    // Reassemble the pixel with the filtered values
    pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

    buf[i] = pixel & 0xFF;
    buf[i+1] = (pixel >> 8) & 0xFF;
  }
}

// ========== Camera Setup ==========
// Set up camera and other parameters
void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 10000000;
  config.frame_size = FRAMESIZE_QQVGA;
  config.pixel_format = PIXFORMAT_RGB565; // Use RGB565 instead of JPEG
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.fb_count = 2;

  if (psramFound()) {
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  if (esp_camera_init(&config) != ESP_OK) {
    Serial.println("Camera init failed");
    while (true);
  }
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 6; i++) {
    ledcAttachChannel(ledPins[i], 1000, 8, i); // 1kHz, 8-bit PWM
  }

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());

  setupCamera();

  // Serve HTML page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  // Serve JSON RGB values
  server.on("/rgb", HTTP_GET, [](AsyncWebServerRequest *request){
    for (int i = 0; i < 6; i++) {
      colors[i%3] = map(analogRead(adcChns[i%3]), 0, 4095, 0, 255);
      if (i < 3) {
        ledcWrite(ledPins[i], abs(255 - colors[i % 3]));
      } else {
        ledcWrite(ledPins[i], colors[i % 3]);
      }
    }
    String json = "{\"r\":" + String(colors[0]) + ",\"g\":" + String(colors[1]) + ",\"b\":" + String(colors[2]) + "}";
    request->send(200, "application/json", json);
  });

  // Serve filtered BMP image
server.on("/filtered.jpg", HTTP_GET, [](AsyncWebServerRequest *request) {
  Serial.println("Capturing and filtering image...");

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb || fb->format != PIXFORMAT_RGB565) {
    request->send(500, "text/plain", "Camera capture failed or format not RGB565");
    if (fb) esp_camera_fb_return(fb);
    return;
  }

  // Apply color filter
  applyColorFilter(fb->buf, fb->len, colors[0], colors[1], colors[2]);

  // Encode RGB565 to JPEG
  uint8_t *jpg_buf = nullptr;
  size_t jpg_len = 0;
  bool jpeg_converted = frame2jpg(fb, 80, &jpg_buf, &jpg_len); // 80 = quality

  esp_camera_fb_return(fb); // Done with original buffer

  if (!jpeg_converted || jpg_buf == nullptr) {
    request->send(500, "text/plain", "JPEG compression failed");
    return;
  }

  // Send JPEG response
  AsyncWebServerResponse *response = request->beginResponse_P("image/jpeg", jpg_len, jpg_buf, [](void *arg) {
    free(arg); // Free JPEG buffer when done
  }, jpg_buf);

  response->addHeader("Content-Disposition", "inline; filename=filtered.jpg");
  request->send(response);
});
  server.begin();
}

void loop() {
  // Nothing needed here
}