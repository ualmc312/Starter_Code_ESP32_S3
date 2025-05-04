#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include "esp_camera.h"
#define CAMERA_MODEL_ESP32S3_EYE
#include "camera_pins.h"

// Network credentials
const char* ssid = "P6TDRMC";
const char* password = "88a4O07;";

// Pins for potentiometers
const byte adcChns[] = {19, 20, 14};         // define the ADC channels
const byte ledPins[] = {38, 39, 40, 1, 2, 42}; //R, G, B
int colors[] = {0, 0, 0};   

AsyncWebServer server(80);

// HTML with JavaScript and Canvas
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>ESP32-CAM with RGB Filter</title>
</head>
<body>
  <h2>ESP32-CAM - RGB Filter Controlled by Potentiometers</h2>
  <button onclick="capture()">Capture</button>
  <br><br>
  <img id="snapshot" src="" width="320">
  <canvas id="canvas" width="320" height="240" style="display:none;"></canvas>
  <p>
    Red: <span id="rVal">255</span><br>
    Green: <span id="gVal">255</span><br>
    Blue: <span id="bVal">255</span>
  </p>
  <script>
    let red = 255, green = 255, blue = 255;

    function updateRGBFromESP() {
    fetch('/rgb')
    .then(res => res.json())
    .then(data => {
      red = data.r;
      green = data.g;
      blue = data.b;

      // Update HTML spans
      document.getElementById("rVal").textContent = red;
      document.getElementById("gVal").textContent = green;
      document.getElementById("bVal").textContent = blue;

      console.log("RGB:", red, green, blue);
    });
}


    setInterval(updateRGBFromESP, 1000); // Fetch RGB every second

    function capture() {
      const img = new Image();
      img.crossOrigin = "anonymous";

      img.onload = () => {
        const canvas = document.getElementById('canvas');
        const ctx = canvas.getContext('2d');
        ctx.drawImage(img, 0, 0, canvas.width, canvas.height);

        const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
        const data = imageData.data;

        for (let i = 0; i < data.length; i += 4) {
          data[i]     = data[i] * red / 255;   // R
          data[i + 1] = data[i + 1] * green / 255; // G
          data[i + 2] = data[i + 2] * blue / 255;  // B
        }

        ctx.putImageData(imageData, 0, 0);
        document.getElementById('snapshot').src = canvas.toDataURL("image/jpeg");
      };

      img.src = '/capture.jpg?t=' + new Date().getTime();
    }
  </script>
</body>
</html>
)rawliteral";

// Start camera
void startCamera() {
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
  config.pixel_format = PIXFORMAT_JPEG; // for streaming
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 16;
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

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
}

void setup() {
  Serial.begin(115200);
  for (int i = 0; i < 6; i++) {
    ledcAttachChannel(ledPins[i], 1000, 8, i); // 1kHz, 8-bit PWM
  }

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());

  startCamera();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.on("/capture.jpg", HTTP_GET, [](AsyncWebServerRequest *request){
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      request->send(500, "text/plain", "Camera capture failed");
      return;
    }

    AsyncWebServerResponse *response = request->beginResponse("image/jpeg", fb->len,
      [fb](uint8_t *buffer, size_t maxLen, size_t alreadySent) -> size_t {
        size_t toSend = fb->len - alreadySent;
        if (toSend > maxLen) toSend = maxLen;
        memcpy(buffer, fb->buf + alreadySent, toSend);
        if (toSend + alreadySent >= fb->len) {
          esp_camera_fb_return(fb);
        }
        return toSend;
      });
    response->addHeader("Cache-Control", "no-store");
    request->send(response);
  });

  server.on("/rgb", HTTP_GET, [](AsyncWebServerRequest *request){
    for (int i = 0; i < 6; i++) {
      colors[i % 3] = map(analogRead(adcChns[i % 3]), 0, 4095, 0, 255);
    if (i < 3) {
      ledcWrite(ledPins[i], abs(255 -colors[i % 3]));
    } else {
      ledcWrite(ledPins[i], colors[i % 3]);
    }
  }

    String json = "{\"r\":" + String(colors[0]) + ",\"g\":" + String(colors[1]) + ",\"b\":" + String(colors[2]) + "}";
    request->send(200, "application/json", json);
  });

  server.begin();
}

void loop() {
  // Nothing to do here
}
