/*

  ESP32-CAM_cam_speed_test

  This program tests the speed of the ESP32-CAM, 
  and displays average frame-lengths for different frame-size and jpeg quality.

  edit changes to 
  -  config.xclk_freq_hz = 20000000;     // 10000000 or 20000000 -- 10 is faster !!
  -  config.frame_size = FRAMESIZE_CIF;  // UXGA, SVGA, CIF, etc
  -  config.jpeg_quality = 1;            // 1 to 63 - smaller number is higher quality and more data

  ... and then the results on the serial monitor, every 100 frames - with avg framesize for last 100.

  Small red led on the back blinks with every frame.

  And the results are ... the same as written here:
  
  https://github.com/espressif/esp32-camera/issues/15#issuecomment-532644189
 
  by James Zahary Sep 11, 2020
     jamzah.plc@gmail.com

  https://github.com/jameszah/ESP32-CAM-Video-Recorder

    jameszah/ESP32-CAM-Video-Recorder is licensed under the
    GNU General Public License v3.0

  The is Arduino code, with standard setup for ESP32-CAM
    - Board ESP32 Wrover Module
    - Partition Scheme Huge APP (3MB No OTA)

*/

static const char vernum[] = "v01";

//#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_camera.h"


// CAMERA_MODEL_AI_THINKER
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
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
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

int fb_max = 12;

camera_fb_t * fb_q[30];
int fb_in = 0;
int fb_out = 0;

camera_fb_t * fb = NULL;

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "soc/soc.h"
#include "soc/cpu.h"
#include "soc/rtc_cntl_reg.h"

static esp_err_t cam_err;

static esp_err_t config_camera() {

  camera_config_t config;

  //Serial.println("config camera");

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
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  
  config.xclk_freq_hz = 10000000;     // 10000000 or 20000000 -- 10 is faster !!
  
  config.pixel_format = PIXFORMAT_JPEG;

  config.frame_size = FRAMESIZE_SVGA; // edit in framesizes below
/*
    FRAMESIZE_96X96,    // 96x96
    FRAMESIZE_QQVGA,    // 160x120
    FRAMESIZE_QCIF,     // 176x144
    FRAMESIZE_HQVGA,    // 240x176
    FRAMESIZE_240X240,  // 240x240
    FRAMESIZE_QVGA,     // 320x240
    FRAMESIZE_CIF,      // 400x296
    FRAMESIZE_HVGA,     // 480x320
    FRAMESIZE_VGA,      // 640x480
    FRAMESIZE_SVGA,     // 800x600
    FRAMESIZE_XGA,      // 1024x768
    FRAMESIZE_HD,       // 1280x720
    FRAMESIZE_SXGA,     // 1280x1024
    FRAMESIZE_UXGA,     // 1600x1200
*/

  config.jpeg_quality = 20;  // 1 to 63 - smaller number is higher quality and more data
  
  config.fb_count = 7;

  // camera init
  cam_err = esp_camera_init(&config);
  if (cam_err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", cam_err);
  }

}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n---");

  pinMode(33, OUTPUT);             // little red led on back of chip
  digitalWrite(33, LOW);           // turn on the red LED on the back of chip

  //Serial.setDebugOutput(true);

  // zzz
  Serial.println("                                    ");
  Serial.println("-------------------------------------");
  Serial.printf("ESP32-CAM cam_speed_test %s\n", vernum);
  Serial.println("-------------------------------------");

  Serial.print("setup, core ");  Serial.print(xPortGetCoreID());
  Serial.print(", priority = "); Serial.println(uxTaskPriorityGet(NULL));

  // put your setup code here, to run once:
  Serial.printf("Internal Total heap %d, internal Free Heap %d\n", ESP.getHeapSize(), ESP.getFreeHeap());
  Serial.printf("SPIRam Total heap   %d, SPIRam Free Heap   %d\n", ESP.getPsramSize(), ESP.getFreePsram());

  Serial.println("Setting up the camera ...");

  config_camera();

  Serial.printf("Internal Total heap %d, internal Free Heap %d\n", ESP.getHeapSize(), ESP.getFreeHeap());
  Serial.printf("SPIRam Total heap   %d, SPIRam Free Heap   %d\n", ESP.getPsramSize(), ESP.getFreePsram());

  digitalWrite(33, HIGH);         // red light turns off when setup is complete

  Serial.println("Warming up the camera ...");
  
  delay(100); // give camera warm up time
  
  for (int i = 0; i < 10; i++) {
    camera_fb_t * fb = esp_camera_fb_get();
    Serial.printf("frame %d, len %d\n",i, fb->len);
    esp_camera_fb_return(fb);
    delay(100);
  }
  Serial.println("  End of setup()\n\n");
}



int first = 1;
int frames = 0;
long frame_start = 0;
long frame_end = 0;
long frame_total = 0;
long frame_average = 0;
long loop_average = 0;
long loop_total = 0;
long total_frame_data = 0;
long last_frame_length = 0;

void loop() {
  if (first) {
    Serial.print("the loop, core ");  Serial.print(xPortGetCoreID());
    Serial.print(", priority = "); Serial.println(uxTaskPriorityGet(NULL));
    first = 0;
  }

  frames++;          // avoid the zero divide problem

  frame_start = millis();

  digitalWrite(33, frames % 2);

  if (frames > 1) {
    loop_total = loop_total + frame_start - frame_end;  // start of this frame less the end of the previous frame
  }

  camera_fb_t * fb = esp_camera_fb_get();

  total_frame_data += fb->len;
  last_frame_length = fb->len;

  esp_camera_fb_return(fb);

  //digitalWrite(33, HIGH);

  frame_end = millis();

  
  frame_total = frame_total + frame_end - frame_start;

  

  if (( frames > 1 && frames < 10 ) || frames % 100 == 0) {
    loop_average = loop_total / frames;
    frame_average = frame_total / frames;

    long avg_frame_size = 0;
    
    if (frames % 100 == 0){
      avg_frame_size = total_frame_data /  100;  
      total_frame_data = 0;
    } else {
      avg_frame_size = total_frame_data / frames;
    }
    
    Serial.printf("millis for camera %d, fps %.2f, millis not camera %d, avg frame length %d", frame_average, 1000.0 / frame_average, loop_average, avg_frame_size);
    Serial.printf("  --  frame %d, length %d\n", frames, last_frame_length);
    
  }
  //delay (1000);
}
