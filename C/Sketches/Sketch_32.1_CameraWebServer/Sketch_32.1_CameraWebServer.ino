/**********************************************************************
  Filename    : Camera Web Server
  Description : The camera images captured by the ESP32S3 are displayed on the web page.
  Auther      : www.freenove.com
  Modification: 2024/07/01
**********************************************************************/
#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include "img_converters.h"
#define CAMERA_MODEL_ESP32S3_EYE
#include "camera_pins.h"

const char* ssid     = "P6TDRMC";
const char* password = "88a4O07;";
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32 Cam with RGB Filter</title>
  <style>
    body { font-family: sans-serif; text-align: center; }
    img { max-width: 100%; border: 2px solid #333; margin: 10px; }
    #rgb { font-size: 18px; margin: 10px; }
  </style>
</head>
<body>
  <h2>ESP32 Live Stream</h2>
  <img src="/stream" id="live">
  
  <h2>Filtered Snapshot</h2>
  <button onclick="getFiltered()">Take Filtered Photo</button><br>
  <div id="rgb">RGB: (255, 255, 255)</div>
  <img id="filtered" src="" alt="Filtered image appears here">

  <script>
    function getFiltered() {
      const ts = Date.now();
      document.getElementById("filtered").src = "/filtered.jpg?ts=" + ts;
      fetch("/rgb").then(res => res.text()).then(text => {
        document.getElementById("rgb").innerText = "RGB: " + text;
      });
    }
  </script>
</body>
</html>
)rawliteral";
const byte adcChns[] = {19, 20, 14};         // define the ADC channels
const byte ledPins[] = {38, 39, 40, 1, 2, 42}; //B, G, R
int colors[] = {0, 0, 0};                    // red, green, blue values

WebServer server(80);
void readPotentiometers() {
  for (int i = 0; i < 6; i++) {
    colors[i % 3] = map(analogRead(adcChns[i % 3]), 0, 4095, 0, 255);
    if (i < 3) {
      ledcWrite(ledPins[i], abs(255 -colors[i % 3]));
    } else {
      ledcWrite(ledPins[i], colors[i % 3]);
    }
  }
}

void handleRGB() {
  readPotentiometers();
  char rgb[32];
  snprintf(rgb, sizeof(rgb), "(%d, %d, %d)", colors[0], colors[1], colors[2]);
  server.send(200, "text/plain", rgb);
}

void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handleFilteredJPG() {
  readPotentiometers();

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Camera capture failed");
    return;
  }

  if (fb->format != PIXFORMAT_RGB565) {
    server.send(500, "text/plain", "Camera format not RGB565");
    esp_camera_fb_return(fb);
    return;
  }

  uint8_t *filtered_buf = (uint8_t *)malloc(fb->len);
  if (!filtered_buf) {
    server.send(500, "text/plain", "Memory allocation failed");
    esp_camera_fb_return(fb);
    return;
  }

  memcpy(filtered_buf, fb->buf, fb->len);

  for (size_t i = 0; i < fb->len; i += 2) {
    uint16_t pixel = (filtered_buf[i + 1] << 8) | filtered_buf[i];

    uint8_t r = ((pixel >> 11) & 0x1F) << 3;
    uint8_t g = ((pixel >> 5) & 0x3F) << 2;
    uint8_t b = (pixel & 0x1F) << 3;

    r = (r * colors[0]) / 255;
    g = (g * colors[1]) / 255;
    b = (b * colors[2]) / 255;

    uint16_t new_pixel = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    filtered_buf[i]     = new_pixel & 0xFF;
    filtered_buf[i + 1] = (new_pixel >> 8);
  }

  uint8_t *jpg_buf = nullptr;
  size_t jpg_len = 0;
  bool converted = fmt2jpg(filtered_buf, fb->len, fb->width, fb->height, PIXFORMAT_RGB565, 90, &jpg_buf, &jpg_len);


  if (!converted) {
    server.send(500, "text/plain", "JPEG conversion failed");
    free(filtered_buf);
    esp_camera_fb_return(fb);
    return;
  }

  server.sendHeader("Content-Type", "image/jpeg");
  server.sendHeader("Content-Length", String(jpg_len));
  server.send(200);
  WiFiClient client = server.client();
  client.write(jpg_buf, jpg_len);

  free(filtered_buf);
  free(jpg_buf);
  esp_camera_fb_return(fb);
}
void startCameraServer();

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  for (int i = 0; i < 6; i++) {
    ledcAttachChannel(ledPins[i], 1000, 8, i); // 1kHz, 8-bit PWM
  }
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
  config.frame_size = FRAMESIZE_SVGA;
  config.pixel_format = PIXFORMAT_RGB565; // for streaming
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count = 2;
  
  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  // for larger pre-allocated frame buffer.
  if(psramFound()){
    config.jpeg_quality = 10;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    // Limit the frame size when PSRAM is not available
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  s->set_vflip(s, 1); // flip it back
  s->set_brightness(s, 1); // up the brightness just a bit
  s->set_saturation(s, 0); // lower the saturation
  
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  while (WiFi.STA.hasIP() != true) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("");
  Serial.println("WiFi connected");
  
  server.on("/", handleRoot);
  server.on("/filtered.jpg", HTTP_GET, handleFilteredJPG);
  server.on("/rgb", HTTP_GET, handleRGB);

  startCameraServer();

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  // Do nothing. Everything is done in another task by the web server
}
