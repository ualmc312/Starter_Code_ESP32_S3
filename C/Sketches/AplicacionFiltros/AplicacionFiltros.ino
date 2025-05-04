#include "esp_camera.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

// Replace with your network credentials
const char* ssid = "P6TDRMC";
const char* password = "88a4O07;";

// Create AsyncWebServer on port 80
AsyncWebServer server(80);

// Select your camera model
#define CAMERA_MODEL_ESP32S3_EYE
#include "camera_pins.h"

void startCameraStream();

// Setup
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

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
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Enable higher quality settings if PSRAM is available
  if(psramFound()){
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
    config.fb_location = CAMERA_FB_IN_DRAM;
  }

  // Initialize the camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("WiFi connected, IP: ");
  Serial.println(WiFi.localIP());

  // Endpoint: root page with simple HTML to show stream
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", 
    "<html><body><h1>ESP32-CAM Stream</h1><img src='/stream'></body></html>");
  });

  // Endpoint: Single image capture
  server.on("/capture", HTTP_GET, [](AsyncWebServerRequest *request){
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      request->send(500, "text/plain", "Camera capture failed");
      return;
    }
    AsyncWebServerResponse *response = request->beginResponse_P("image/jpeg", fb->len, fb->buf);
    response->addHeader("Content-Disposition", "inline; filename=capture.jpg");
    request->send(response);
    esp_camera_fb_return(fb);
  });

  // Endpoint: Live MJPEG stream
  server.on("/stream", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse("multipart/x-mixed-replace; boundary=frame");
    response->setCode(200);
    response->addHeader("Cache-Control", "no-cache");
    response->addHeader("Connection", "close");
    response->setContentLength(CONTENT_LENGTH_UNKNOWN);
    request->send(response);

    // Stream handler
    response->onChunk([](uint8_t *buffer, size_t maxLen, size_t alreadySent) -> size_t {
      camera_fb_t * fb = esp_camera_fb_get();
      if (!fb) {
        return 0;
      }
      size_t len = snprintf((char *)buffer, maxLen,
                            "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
      memcpy(buffer + len, fb->buf, fb->len);
      size_t total = len + fb->len;
      memcpy(buffer + total, "\r\n", 2);
      total += 2;
      esp_camera_fb_return(fb);
      return total;
    });
  });

  // Start the server
  server.begin();
}

void loop() {
  // Nothing needed here
}
