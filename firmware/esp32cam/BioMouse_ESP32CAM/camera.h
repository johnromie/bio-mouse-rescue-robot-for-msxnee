/* ============================================================================
 *  CAMERA  (ESP32-CAM OV2640)
 * ----------------------------------------------------------------------------
 *  Initialises the camera and provides a still-frame helper. The live MJPEG
 *  stream handler lives in the main .ino together with the HTTP routes.
 *
 *  NOTE ON FRAME SIZE: the thermal array has a fairly narrow horizontal
 *  field of view, so keeping the camera resolution modest actually HELPS the
 *  operator align the robot with the thermal reading. VGA is a good default.
 * ========================================================================== */
#ifndef BIOMOUSE_ESP_CAMERA_H
#define BIOMOUSE_ESP_CAMERA_H

#include "config.h"
// esp_camera.h already includes sensor.h (which declares sensor_t and
  // framesize_t), so nothing extra is needed here.
#include <esp_camera.h>

/* AI-Thinker ESP32-CAM pin map. If your board differs, edit ONLY these. */
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    15
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM       5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

inline bool cameraBegin() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;      // 20 MHz is the OV2640's normal clock
  config.pixel_format = PIXFORMAT_JPEG;   // JPEG lets the stream run smoothly

  // NOTE: ESP32 core 3.x removed the old sram32_gb_iram0_dflt / esp_camera_de_mem
  // trick that used to hand RAM from Wi-Fi to the camera. Those symbols no longer
  // exist, and the driver now manages its own memory, so they are intentionally
  // NOT set here.
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.print(F("[CAMERA] init failed, error "));
    Serial.println((int)err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 0);   // 0 = normal
    s->set_gain_ctrl(s, 0);     // 0 = auto gain
    s->set_exposure_ctrl(s, 0); // 0 = auto exposure
    s->set_whitebal(s, 1);      // 1 = restart white balance
    // Frame size and JPEG quality: the two knobs that matter for a live stream.
    s->set_framesize(s, CAMERA_FRAME_SIZE);
    s->set_quality(s, CAMERA_QUALITY);
  }

  Serial.println(F("[CAMERA] OV2640 ready"));
  return true;
}

#endif // BIOMOUSE_ESP_CAMERA_H