/*

  ESP32-CAM-Video-Recorder-junior

  This program records an mjpeg avi video to the sd card of an ESP32-CAM.


  It is the junior version of   https://github.com/jameszah/ESP32-CAM-Video-Recorder
  which has 100 other features of wifi, streaming video, http control, telegram updates, pir control,
  touch control, ftp downloads, .... and other things that make it very big and complex.

  Just set a few parameters, compile and download, and it will record on power-on, until sd is full, or power-off.
  Then pull out the sd and move it to your computer, and you will see all but the last file avi which died during the unplug.

  The files will have the name such as:

    desklens001.003.avi

    "desklens" is your devname
    001 - is a number stored in eprom that will increase everytime your device boots
    003 - is the 3rd file created during the current boot

  Small red led on the back blinks with every frame.


  by James Zahary Sep 12, 2020
     jamzah.plc@gmail.com

    - Sep 17, 2024 arduino 1.8.19
                   esp32-arduino 3.04
                   change to soc line
                   ESP32-CAM-Video-Recorder-junior-60x.4.7soc ~ ArduinoSketch folder

    - Feb 24, 2025 arduino 1.8.19
                  esp32-arduino 3.1.1
                  ESP32-CAM-Video-Recorder-junior-62
                  ota passowrd "mrpeanut"
                  ap mode password "12344321"
                  config file is now config2.txt

  https://github.com/jameszah/ESP32-CAM-Video-Recorder-junior

    jameszah/ESP32-CAM-Video-Recorder-junior is licensed under the
    GNU General Public License v3.0

  The is Arduino code, with standard setup for ESP32-CAM
    - Board ESP32 Wrover Module or AI Thinker ESP32-CAM
    - Partition Scheme Minimal SPIFFS with OTA

  Needs these libraries or better:


*/

//#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "sensor.h"
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#define jpr(format, ...) \
  { \
    char buffer[256]; \
    snprintf(buffer, sizeof(buffer), format, ##__VA_ARGS__); \
    Serial.print(buffer); \
    if (logfile) { \
      logfile.print(buffer); \
    } \
  }

#define jprln(format, ...) \
  { \
    char buffer[256]; \
    snprintf(buffer, sizeof(buffer), format, ##__VA_ARGS__); \
    Serial.println(buffer); \
    if (logfile) { \
      logfile.println(buffer); \
    } \
  }

static const char vernum[] = "v62.34";
char devname[30];

// https://sites.google.com/a/usapiens.com/opnode/time-zones  -- find your timezone here
String TIMEZONE = "GMT0BST,M3.5.0/01,M10.5.0/02";

#define Lots_of_Stats 1
#define blinking 0

int framesize;
int quality ;
int framesizeconfig ;
int qualityconfig ;
int buffersconfig ;
int avi_length ;            // how long a movie in seconds -- 1800 sec = 30 min
int frame_interval ;          // record at full speed
int speed_up_factor ;          // play at realtime
int stream_delay ;           // minimum of 500 ms delay between frames

int MagicNumber = 12;                // change this number to reset the eprom in your esp32 for file numbers

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
bool configfile = false;
bool InternetOff = true;
bool reboot_now = false;
bool restart_now = false;
String cssid1, cssid2, cssid3;
String cpass1, cpass2, cpass3;

String czone;
//char apssid[30];
//char appass[14];

TaskHandle_t the_camera_loop_task;
TaskHandle_t the_sd_loop_task;
TaskHandle_t the_streaming_loop_task;

static SemaphoreHandle_t wait_for_sd;
static SemaphoreHandle_t sd_go;
SemaphoreHandle_t baton;

long current_frame_time;
long last_frame_time;
int frame_buffer_size;
bool web_stop = false;

// https://github.com/espressif/esp32-camera/issues/182
#define fbs  1 // was 64 -- how many kb of static ram for psram -> sram buffer for sd write
uint8_t fb_record_static[fbs * 1024 + 20];

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

camera_fb_t * fb_curr = NULL;
camera_fb_t * fb_next = NULL;

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "soc/soc.h"
#include "esp_cpu.h" //#include "soc/cpu.h"
#include "soc/rtc_cntl_reg.h"

static esp_err_t cam_err;
float most_recent_fps = 0;
int most_recent_avg_framesize = 0;

uint8_t* fb_record;
uint8_t* fb_curr_record_buf;
uint8_t* fb_streaming;
uint8_t* fb_capture;

int fb_record_len;
int fb_curr_record_len;
int fb_streaming_len;
int fb_capture_len;
long fb_record_time = 0;
long fb_curr_record_time = 0;
long fb_streaming_time = 0;
long fb_capture_time = 0;

int first = 1;
long frame_start = 0;
long frame_end = 0;
long frame_total = 0;
long frame_average = 0;
long loop_average = 0;
long loop_total = 0;
long total_frame_data = 0;
long last_frame_length = 0;
int done = 0;
long avi_start_time = 0;
long avi_end_time = 0;
int start_record = 0;
int start_record_2nd_opinion = -2;
int start_record_1st_opinion = -1;

int we_are_already_stopped = 0;
long total_delay = 0;
long bytes_before_last_100_frames = 0;
long time_before_last_100_frames = 0;

long time_in_loop = 0;
long time_in_camera = 0;
long time_in_sd = 0;
long time_in_good = 0;
long time_total = 0;
long time_in_web1 = 0;
long time_in_web2 = 0;
long delay_wait_for_sd = 0;
long wait_for_cam = 0;
int very_high = 0;

bool do_the_ota = false;

int do_it_now = 0;
int gframe_cnt;
int gfblen;
int gj;
int  gmdelay;
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//  Avi Writer Stuff here


// MicroSD
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include "FS.h"
#include <SD_MMC.h>

File logfile;
File avifile;
File idxfile;

char avi_file_name[100];
char file_to_edit[50] = "/JamCam0481.0007.avi"; //61.3

static int i = 0;
uint16_t frame_cnt = 0;
uint16_t remnant = 0;
uint32_t length = 0;
uint32_t startms;
uint32_t elapsedms;
uint32_t uVideoLen = 0;

int bad_jpg = 0;
int extend_jpg = 0;
int normal_jpg = 0;

int file_number = 0;
int file_group = 0;
long boot_time = 0;

long totalp;
long totalw;

#define BUFFSIZE 512

uint8_t buf[BUFFSIZE];

#define AVIOFFSET 240 // AVI main header length

unsigned long movi_size = 0;
unsigned long jpeg_size = 0;
unsigned long idx_offset = 0;

uint8_t zero_buf[4] = {0x00, 0x00, 0x00, 0x00};
uint8_t dc_buf[4] = {0x30, 0x30, 0x64, 0x63};    // "00dc"
uint8_t dc_and_zero_buf[8] = {0x30, 0x30, 0x64, 0x63, 0x00, 0x00, 0x00, 0x00};

uint8_t avi1_buf[4] = {0x41, 0x56, 0x49, 0x31};    // "AVI1"
uint8_t idx1_buf[4] = {0x69, 0x64, 0x78, 0x31};    // "idx1"


struct frameSizeStruct {
  uint8_t frameWidth[2];
  uint8_t frameHeight[2];
};

//  data structure from here https://github.com/s60sc/ESP32-CAM_MJPEG2SD/blob/master/avi.cpp, extended for ov5640
// must match https://github.com/espressif/esp32-camera/blob/b6a8297342ed728774036089f196d599f03ea367/driver/include/sensor.h#L87
// which changed in Nov 2024
static const frameSizeStruct frameSizeData[] = {
  {{0x60, 0x00}, {0x60, 0x00}}, // FRAMESIZE_96X96,    // 96x96    0 framesize
  {{0xA0, 0x00}, {0x78, 0x00}}, // FRAMESIZE_QQVGA,    // 160x120  1
  {{0x60, 0x00}, {0x60, 0x00}}, // FRAMESIZE_128X128   // 128x128  2
  {{0xB0, 0x00}, {0x90, 0x00}}, // FRAMESIZE_QCIF,     // 176x144  3
  {{0xF0, 0x00}, {0xB0, 0x00}}, // FRAMESIZE_HQVGA,    // 240x176  4
  {{0xF0, 0x00}, {0xF0, 0x00}}, // FRAMESIZE_240X240,  // 240x240  5
  {{0x40, 0x01}, {0xF0, 0x00}}, // FRAMESIZE_QVGA,     // 320x240  6
  {{0x40, 0x01}, {0xF0, 0x00}}, // FRAMESIZE_320X320,  // 320x320  7
  {{0x90, 0x01}, {0x28, 0x01}}, // FRAMESIZE_CIF,      // 400x296  8
  {{0xE0, 0x01}, {0x40, 0x01}}, // FRAMESIZE_HVGA,     // 480x320  9
  {{0x80, 0x02}, {0xE0, 0x01}}, // FRAMESIZE_VGA,      // 640x480  10
  //               38,400    61,440    153,600
  {{0x20, 0x03}, {0x58, 0x02}}, // FRAMESIZE_SVGA,     // 800x600   11
  {{0x00, 0x04}, {0x00, 0x03}}, // FRAMESIZE_XGA,      // 1024x768  12
  {{0x00, 0x05}, {0xD0, 0x02}}, // FRAMESIZE_HD,       // 1280x720  13
  {{0x00, 0x05}, {0x00, 0x04}}, // FRAMESIZE_SXGA,     // 1280x1024 14
  {{0x40, 0x06}, {0xB0, 0x04}}, // FRAMESIZE_UXGA,     // 1600x1200 15
  // 3MP Sensors
  {{0x80, 0x07}, {0x38, 0x04}}, // FRAMESIZE_FHD,      // 1920x1080 16
  {{0xD0, 0x02}, {0x00, 0x05}}, // FRAMESIZE_P_HD,     //  720x1280 17
  {{0x60, 0x03}, {0x00, 0x06}}, // FRAMESIZE_P_3MP,    //  864x1536 18
  {{0x00, 0x08}, {0x00, 0x06}}, // FRAMESIZE_QXGA,     // 2048x1536 19
  // 5MP Sensors
  {{0x00, 0x0A}, {0xA0, 0x05}}, // FRAMESIZE_QHD,      // 2560x1440 20
  {{0x00, 0x0A}, {0x40, 0x06}}, // FRAMESIZE_WQXGA,    // 2560x1600 21
  {{0x38, 0x04}, {0x80, 0x07}}, // FRAMESIZE_P_FHD,    // 1080x1920 22
  {{0x00, 0x0A}, {0x80, 0x07}}  // FRAMESIZE_QSXGA,    // 2560x1920 23

};

const int avi_header[AVIOFFSET] PROGMEM = {
  0x52, 0x49, 0x46, 0x46, 0xD8, 0x01, 0x0E, 0x00, 0x41, 0x56, 0x49, 0x20, 0x4C, 0x49, 0x53, 0x54,
  0xD0, 0x00, 0x00, 0x00, 0x68, 0x64, 0x72, 0x6C, 0x61, 0x76, 0x69, 0x68, 0x38, 0x00, 0x00, 0x00,
  0xA0, 0x86, 0x01, 0x00, 0x80, 0x66, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
  0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x80, 0x02, 0x00, 0x00, 0xe0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4C, 0x49, 0x53, 0x54, 0x84, 0x00, 0x00, 0x00,
  0x73, 0x74, 0x72, 0x6C, 0x73, 0x74, 0x72, 0x68, 0x30, 0x00, 0x00, 0x00, 0x76, 0x69, 0x64, 0x73,
  0x4D, 0x4A, 0x50, 0x47, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x73, 0x74, 0x72, 0x66,
  0x28, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x80, 0x02, 0x00, 0x00, 0xe0, 0x01, 0x00, 0x00,
  0x01, 0x00, 0x18, 0x00, 0x4D, 0x4A, 0x50, 0x47, 0x00, 0x84, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x49, 0x4E, 0x46, 0x4F,
  0x10, 0x00, 0x00, 0x00, 0x6A, 0x61, 0x6D, 0x65, 0x73, 0x7A, 0x61, 0x68, 0x61, 0x72, 0x79, 0x20,
  0x76, 0x36, 0x32, 0x20, 0x4C, 0x49, 0x53, 0x54, 0x00, 0x01, 0x0E, 0x00, 0x6D, 0x6F, 0x76, 0x69,
};


//
// Writes an uint32_t in Big Endian at current file position
//
static void inline print_quartet(unsigned long i, File fd) {

  uint8_t y[4];
  y[0] = i % 0x100;
  y[1] = (i >> 8) % 0x100;
  y[2] = (i >> 16) % 0x100;
  y[3] = (i >> 24) % 0x100;
  size_t i1_err = fd.write(y , 4);
}

//
// Writes 2 uint32_t in Big Endian at current file position
//
static void inline print_2quartet(unsigned long i, unsigned long j, File fd) {

  uint8_t y[8];
  y[0] = i % 0x100;
  y[1] = (i >> 8) % 0x100;
  y[2] = (i >> 16) % 0x100;
  y[3] = (i >> 24) % 0x100;
  y[4] = j % 0x100;
  y[5] = (j >> 8) % 0x100;
  y[6] = (j >> 16) % 0x100;
  y[7] = (j >> 24) % 0x100;
  size_t i1_err = fd.write(y , 8);
}

void print_mem(const char* text) {
  jpr("%s core: %d, Prio: %d, Internal Free Heap %6d of %6d, SPI Free %6d of %6d\n", text, xPortGetCoreID(), uxTaskPriorityGet(NULL), ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getFreePsram(), ESP.getPsramSize() );
}

#include "lwip/sockets.h"
#include <lwip/netdb.h>

void print_sock(int sock) {

  sockaddr_in6 clientAddr;
  socklen_t addrLen = sizeof(clientAddr);

  int clientFd = sock; //client.getSocket();

  char ip[INET6_ADDRSTRLEN] = {0};

  if (getpeername(clientFd, (struct sockaddr*)&clientAddr, &addrLen) == 0) {
    //inet_ntop(AF_INET, &clientAddr.sin_addr.s_addr, ip, sizeof(ip));
    jpr("family %d ", clientAddr.sin6_family);
    inet_ntop(AF_INET, &clientAddr.sin6_addr.un.u32_addr[3], ip, sizeof(ip));
    jpr("Peer Client IP4: ");
    jpr(ip);
    inet_ntop(AF_INET6, &clientAddr.sin6_addr.un.u32_addr[3], ip, sizeof(ip));
    jpr(", Peer Client IP6: ");
    jpr(ip);
    uint16_t clientPort = ntohs(clientAddr.sin6_port); // Extract port
    jpr(", Client Port: ");
    jprln("%d", clientPort);

  } else {
    Serial.println("Failed to get client address.");
  }

  if (getsockname(clientFd, (struct sockaddr*)&clientAddr, &addrLen) == 0) {
    //inet_ntop(AF_INET, &clientAddr.sin_addr.s_addr, ip, sizeof(ip));
    jpr("family %d ", clientAddr.sin6_family);
    inet_ntop(AF_INET, &clientAddr.sin6_addr.un.u32_addr[3], ip, sizeof(ip));
    jpr("Sock Client IP4: ");
    jpr(ip);
    inet_ntop(AF_INET6, &clientAddr.sin6_addr.un.u32_addr[3], ip, sizeof(ip));
    jpr(", Sock Client IP6: ");
    jpr(ip);
    uint16_t clientPort = ntohs(clientAddr.sin6_port); // Extract port
    jpr(", Client Port: ");
    jprln("%d", clientPort);

  } else {
    Serial.println("Failed to get client address.");
  }
}

//
// if we have no camera, or sd card, then flash rear led on and off to warn the human SOS - SOS
//
void major_fail() {

  Serial.println(" ");
  logfile.close();

  for  (int i = 0;  i < 10; i++) {                 // 10 loops or about 100 seconds then reboot
    for (int j = 0; j < 3; j++) {
      digitalWrite(33, LOW);   delay(150);
      digitalWrite(33, HIGH);  delay(150);
    }
    delay(1000);

    for (int j = 0; j < 3; j++) {
      digitalWrite(33, LOW);  delay(500);
      digitalWrite(33, HIGH); delay(500);
    }
    delay(1000);
    Serial.print("Major Fail  "); Serial.print(i); Serial.print(" / "); Serial.println(10);
  }

  ESP.restart();
}



//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//

static void config_camera() {

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

  config.xclk_freq_hz = 20000000;

  config.pixel_format = PIXFORMAT_JPEG;

  jpr("Frame config %d, quality config %d, buffers config %d\n", framesizeconfig, qualityconfig, buffersconfig);

  config.frame_size =  (framesize_t)framesize;
  config.jpeg_quality = quality;
  config.fb_count = buffersconfig;

  // https://github.com/espressif/esp32-camera/issues/357#issuecomment-1047086477
  config.grab_mode      = CAMERA_GRAB_LATEST; //61.92

  if (Lots_of_Stats) {
    print_mem("Before camera config ... ");
  }
  esp_err_t cam_err = ESP_FAIL;
  int attempt = 5;
  while (attempt && cam_err != ESP_OK) {
    cam_err = esp_camera_init(&config);
    if (cam_err != ESP_OK) {
      jpr("Camera init failed with error 0x%x\n", cam_err);
      digitalWrite(PWDN_GPIO_NUM, 1);
      delay(500);
      digitalWrite(PWDN_GPIO_NUM, 0); // power cycle the camera (OV2640)
      attempt--;
    }
  }

  if (Lots_of_Stats) {
    print_mem("After  camera config ... ");
  }

  if (cam_err != ESP_OK) {
    major_fail();
  }

  sensor_t * ss = esp_camera_sensor_get();

  jpr("\nCamera started correctly, Type is %x (hex) of 9650, 7725, 2640, 3660, 5640\n\n", ss->id.PID);

  if (ss->id.PID == OV5640_PID ) {
    //Serial.println("56 - going mirror");
    ss->set_hmirror(ss, 1);        // 0 = disable , 1 = enable
  } else {
    ss->set_hmirror(ss, 0);        // 0 = disable , 1 = enable
  }

  ss->set_brightness(ss, 1);  //up the blightness just a bit
  ss->set_saturation(ss, -2); //lower the saturation

  int x = 0;
  delay(500);
  for (int j = 0; j < 30; j++) {
    camera_fb_t * fb = esp_camera_fb_get(); // get_good_jpeg();
    if (!fb) {
      Serial.println("Camera Capture Failed");
    } else {
      if (j < 3 || j > 27) jpr("Pic %2d, len=%7d, at mem %X\n", j, fb->len, (long)fb->buf);
      x = fb->len;
      esp_camera_fb_return(fb);
      delay(30);
    }
  }
  frame_buffer_size  = (( (x * 4) / (16 * 1024) ) + 1) * 16 * 1024  ;
  // 4 times buffer size, rounded up to 16kb

  jpr("Buffer size for %d is %d\n", x, frame_buffer_size);
  print_mem("End of camera setup");
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//


static esp_err_t init_sdcard()
{

  int succ = SD_MMC.begin("/sdcard", true, false, BOARD_MAX_SDMMC_FREQ, 7);
  if (succ) {
    Serial.printf("SD_MMC Begin: %d\n", succ);
    uint8_t cardType = SD_MMC.cardType();
    Serial.print("SD_MMC Card Type: ");
    if (cardType == CARD_MMC) {
      Serial.println("MMC");
    } else if (cardType == CARD_SD) {
      Serial.println("SDSC");
    } else if (cardType == CARD_SDHC) {
      Serial.println("SDHC");
    } else {
      Serial.println("UNKNOWN");
    }

    uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
    Serial.printf("SD_MMC Card Size: %lluMB\n", cardSize);

  } else {
    Serial.printf("Failed to mount SD card VFAT filesystem. \n");
    Serial.println("Do you have an SD Card installed?");
    Serial.println("Check pin 12 and 13, not grounded, or grounded with 10k resistors!\n\n");
    major_fail();
  }

  return ESP_OK;
}

#include "config.h"

void read_config_file() {

  // if there is a config.txt, use it plus defaults
  // else use defaults, and create a config.txt

  // put a file "config.txt" onto SD card, to set parameters different from your hardcoded parameters
  // it should look like this - one paramter per line, in the correct order, followed by 2 spaces, and any comments you choose

  String junk;

  String cname ;
  int cframesize ;
  int cquality = 12 ;
  int cbuffersconfig = 4;
  int clength ;
  int cinterval ;
  int cspeedup ;
  int cstreamdelay ;
  String czone ;

  delay(1000);

  File config_file = SD_MMC.open("/config2.txt", "r");

  if (config_file) {
    jpr("Opened config2.txt from SD");
  } else {
    jpr("Failed to open config2.txt - writing a default");

    // lets make a simple.txt config file
    File new_simple = SD_MMC.open("/config2.txt", "w");
    new_simple.print(config_txt);
    new_simple.close();

    file_group = 1;
    file_number = 1;

    do_eprom_write();

    config_file = SD_MMC.open("/config2.txt", "r");
  }

  jpr("Reading config2.txt\n");
  cname = config_file.readStringUntil(' ');
  junk = config_file.readStringUntil('\n');
  cframesize = config_file.parseInt();
  junk = config_file.readStringUntil('\n');

  clength = config_file.parseInt();
  junk = config_file.readStringUntil('\n');
  cinterval = config_file.parseInt();
  junk = config_file.readStringUntil('\n');
  cspeedup = config_file.parseInt();
  junk = config_file.readStringUntil('\n');
  cstreamdelay = config_file.parseInt();
  junk = config_file.readStringUntil('\n');
  czone = config_file.readStringUntil(' ');
  junk = config_file.readStringUntil('\n');
  cssid1 = config_file.readStringUntil(' ');
  junk = config_file.readStringUntil('\n');
  cpass1 = config_file.readStringUntil(' ');
  junk = config_file.readStringUntil('\n');
  cssid2 = config_file.readStringUntil(' ');
  junk = config_file.readStringUntil('\n');
  cpass2 = config_file.readStringUntil(' ');
  junk = config_file.readStringUntil('\n');
  cssid3 = config_file.readStringUntil(' ');
  junk = config_file.readStringUntil('\n');
  cpass3 = config_file.readStringUntil(' ');
  junk = config_file.readStringUntil('\n');
  config_file.close();

  jpr("=========   Data from config2.txt and defaults  =========\n");
  jpr("Name %s\n", cname);
  jpr("Framesize %d\n", cframesize);
  jpr("Quality %d\n", cquality);
  jpr("Buffers config %d\n", cbuffersconfig);
  jpr("Length %d\n", clength);
  jpr("Interval %d\n", cinterval);
  jpr("Speedup %d\n", cspeedup);
  jpr("Streamdelay %d\n", cstreamdelay);

  jpr("Zone len %d, %s\n", czone.length(), czone.c_str());
  jpr("ssid1 %s\n", cssid1);
  //jpr("pass1 %s\n", cpass1);
  jpr("ssid2 %s\n", cssid2);
  //jpr("pass2 %s\n", cpass2);
  jpr("ssid3 %s\n", cssid3);
  jpr("pass3 %s\n", cpass3);


  framesize = cframesize;
  quality = cquality;
  buffersconfig = cbuffersconfig;
  avi_length = clength;
  frame_interval = cinterval;
  speed_up_factor = cspeedup;
  stream_delay = cstreamdelay;
  configfile = true;
  TIMEZONE = czone;

  cname.toCharArray(devname, cname.length() + 1);

}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
//  delete_old_stuff() - delete oldest files to free diskspace
//

#include <list>
#include <tuple>

void delete_old_stuff() {
  using namespace std;
  using records = tuple<String, String, size_t, time_t>;
  list<records> dirList;

  int card = SD_MMC.cardSize()  / (1024 * 1024);
  int total = SD_MMC.totalBytes()  / (1024 * 1024);
  int used = SD_MMC.usedBytes()  / (1024 * 1024);

  jpr("Card  space: %5dMB\n", card);  // %llu
  jpr("Total space: %5dMB\n", total);
  jpr("Used  space: %5dMB\n", used);

  float full = 1.0 * used / total;
  if (full  <  0.8) {
    jpr("Nothing deleted, %.1f%% disk full\n", 100.0 * full);
  } else {
    jpr("Disk is %.1f%% full ... deleting ...\n", 100.0 * full);

    int x = millis();
    File xdir = SD_MMC.open("/");
    File xf = xdir.openNextFile();

    while (xf) {
      if (xf.isDirectory()) {
        String the_dir = xf.name();
        if (SD_MMC.rmdir("/" + the_dir )) {                      // remove empty dir
          jpr("Dir removed\n"); Serial.println("/" + the_dir);
        } else {
          String log_name = "/" + the_dir + "/" + the_dir + ".999.txt";
          //Serial.println(log_name);
          File the_log = SD_MMC.open(log_name, "r");
          time_t the_fold = xf.getLastWrite();
          time_t the_logfile = the_log.getLastWrite();
          the_log.close();

          if ( the_fold > the_logfile) {
            dirList.emplace_back("", the_dir, 0, the_fold);
          } else {
            dirList.emplace_back("", the_dir, 0, the_logfile);
            //Serial.printf("Log is newer than dir by %d -- ", the_logfile - the_fold);
          }
        }
      } else {
        // skip files
        //dirList.emplace_back("", xf.name(), xf.size(), xf.getLastWrite());
        //Serial.printf("Added: "); Serial.println(xf.name());
      }
      xf = xdir.openNextFile();
    }
    xdir.close();

    dirList.sort([](const records & f, const records & l) {                                 // sort by date
      return get<3>(f) < get<3>(l);
      return false;
    });

    jpr("Sort files took %d ms\n", millis() - x);

    for ( auto& iter : dirList) {
      String fn =  get<1>(iter);

      //jpr("Oldest file is "); Serial.print(fn);
      deleteFolderOrFile(fn.c_str());

      total = SD_MMC.totalBytes()  / (1024 * 1024);
      used = SD_MMC.usedBytes()  / (1024 * 1024);

      full = 1.0 * used / total;

      Serial.println(full);
      if (full < 0.7) break;
    }
  }
}

void deleteFolderOrFile(const char * val) {
  Serial.printf("Deleting : %s\n", val);
  File f = SD_MMC.open("/" + String(val));
  if (!f) {
    jpr("Failed to open %s\n", val);
    return;
  }

  if (f.isDirectory()) {
    File file = f.openNextFile();
    while (file) {
      if (file.isDirectory()) {
        Serial.print("  DIR : ");
        Serial.println(file.name());
      } else {
        Serial.print("  FILE: ");
        Serial.print(file.name());
        Serial.print("  SIZE: ");
        Serial.print(file.size());
        if (SD_MMC.remove("/" + String(val) + "/" + file.name())) {
          Serial.println(" deleted.");
        } else {
          Serial.println(" FAILED.");
        }
      }
      int total = SD_MMC.totalBytes()  / (1024 * 1024);
      int used = SD_MMC.usedBytes()  / (1024 * 1024);

      float full = 1.0 * used / total;

      Serial.println(full);
      if (full < 0.7) break;
      file = f.openNextFile();
    }
    f.close();
    //Remove the dir
    if (SD_MMC.rmdir("/" + String(val))) {
      Serial.printf("Dir %s removed\n", val);
    } else {
      Serial.println("Remove dir failed");
    }

  } else {
    //Remove the file
    if (SD_MMC.remove("/" + String(val))) {
      Serial.printf("File %s deleted\n", val);
    } else {
      Serial.println("Delete failed");
    }
  }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
//  get_good_jpeg()  - take a picture and make sure it has a good jpeg
//
camera_fb_t *  get_good_jpeg() {

  camera_fb_t * fb;

  long start;
  int failures = 0;

  do {
    int fblen = 0;
    int foundffd9 = 0;
    long bp = millis();
    long mstart = micros();

    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera Capture Failed");
      failures++;
    } else {
      long mdelay = micros() - mstart;

      int get_fail = 0;

      totalp = totalp + millis() - bp;
      time_in_camera = totalp;

      fblen = fb->len;

      for (int j = 1; j <= 1025; j++) {
        if (fb->buf[fblen - j] != 0xD9) {
          // no d9, try next for
        } else {                                     //Serial.println("Found a D9");
          if (fb->buf[fblen - j - 1] == 0xFF ) {     //Serial.print("Found the FFD9, junk is "); Serial.println(j);
            if (j == 1) {
              normal_jpg++;
            } else {
              extend_jpg++;
            }
            foundffd9 = 1;
            if (Lots_of_Stats) {
              if (j > 9000) {                // was 900             //  rarely happens - sometimes on 2640
                jpr("Frame %d, Len %d, Extra %d ", frame_cnt, fblen, j - 1 );
                logfile.flush();
              }

              if ( (frame_cnt % 1000 == 50) || (frame_cnt < 1000 && frame_cnt % 100 == 50)) {
                gframe_cnt = frame_cnt;
                gfblen = fblen;
                gj = j;
                gmdelay = mdelay;
                //Serial.printf("Frame %6d, len %6d, extra  %4d, cam time %7d ", frame_cnt, fblen, j - 1, mdelay / 1000);
                //logfile.printf("Frame %6d, len %6d, extra  %4d, cam time %7d ", frame_cnt, fblen, j - 1, mdelay / 1000);
                do_it_now = 1;
              }
            }
            break;
          }
        }
      }

      if (!foundffd9) {
        bad_jpg++;
        jpr("Bad jpeg, Frame %d, Len = %d \n", frame_cnt, fblen);
        esp_camera_fb_return(fb);
        failures++;

      } else {
        break;
        // count up the useless bytes
      }
    }

  } while (failures < 10);   // normally leave the loop with a break()

  // if we get 10 bad frames in a row, then quality parameters are too high - set them lower (+5), and start new movie
  if (failures == 10) {
    jpr("10 failures");

    sensor_t * ss = esp_camera_sensor_get();
    int qual = ss->status.quality ;
    ss->set_quality(ss, qual + 5);
    quality = qual + 5;
    jpr("\n\nDecreasing quality due to frame failures %d -> %d\n\n", qual, qual + 5);
    delay(1000);

    start_record = 0;
    //reboot_now = true;
  }
  return fb;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
//  eprom functions  - increment the file_group, so files are always unique
//

#include <EEPROM.h>

struct eprom_data {
  int eprom_good;
  int file_group;
};

void do_eprom_read() {

  eprom_data ed;

  EEPROM.begin(200);
  EEPROM.get(0, ed);

  if (ed.eprom_good == MagicNumber) {
    jpr("Good settings in the EPROM ");
    file_group = ed.file_group;
    file_group++;
    jpr("New File Group "); Serial.println(file_group );
  } else {
    jpr("No settings in EPROM - Starting with File Group 1 ");
    file_group = 1;
  }
  do_eprom_write();
  file_number = 1;
}

void do_eprom_write() {

  eprom_data ed;
  ed.eprom_good = MagicNumber;
  ed.file_group  = file_group;

  Serial.println("Writing to EPROM ...");

  EEPROM.begin(200);
  EEPROM.put(0, ed);
  EEPROM.commit();
  EEPROM.end();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Make the avi functions
//
//   start_avi() - open the file and write headers
//   another_pic_avi() - write one more frame of movie
//   end_avi() - write the final parameters and close the file


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// start_avi - open the files and write in headers
//

static void start_avi() {
  char the_directory[50];

  long start = millis();

  jpr("Starting an avi ");
  sprintf(the_directory, "/%s%03d",  devname, file_group);
  SD_MMC.mkdir(the_directory);

  sprintf(avi_file_name, "/%s%03d/%s%03d.%03d.avi",  devname, file_group, devname, file_group, file_number);

  file_number++;

  avifile = SD_MMC.open(avi_file_name, "w");
  idxfile = SD_MMC.open("/idx.tmp", "w");

  if (avifile) {
    jpr("File open: %s\n", avi_file_name);
  }  else  {
    jpr("Could not open avi file");
    major_fail();
  }

  if (idxfile)  {
    //Serial.printf("File open: %s\n", "//idx.tmp");
  }  else  {
    jpr("Could not open file /idx.tmp");
    major_fail();
  }

  for ( i = 0; i < AVIOFFSET; i++) {
    char ch = pgm_read_byte(&avi_header[i]);
    buf[i] = ch;
  }

  memcpy(buf + 0x40, frameSizeData[framesize].frameWidth, 2);
  memcpy(buf + 0xA8, frameSizeData[framesize].frameWidth, 2);
  memcpy(buf + 0x44, frameSizeData[framesize].frameHeight, 2);
  memcpy(buf + 0xAC, frameSizeData[framesize].frameHeight, 2);

  size_t err = avifile.write(buf, AVIOFFSET);

  uint8_t ex_fps = 1;
  if (frame_interval == 0) {
    if (framesize >= 11) {
      ex_fps = 12.5 * speed_up_factor ;;
    } else {
      ex_fps = 25.0 * speed_up_factor;
    }
  } else {
    ex_fps = round(1000.0 / frame_interval * speed_up_factor);
  }

  avifile.seek( 0x84 , SeekSet);
  print_quartet((int)ex_fps, avifile);

  avifile.seek( 0x30 , SeekSet);
  print_quartet(3, avifile);  // magic number 3 means frame count not written // 61.3

  avifile.seek( AVIOFFSET, SeekSet);

  jpr("Recording %d seconds\n", avi_length);

  startms = millis();

  totalp = 0;
  totalw = 0;

  jpeg_size = 0;
  movi_size = 0;
  uVideoLen = 0;
  idx_offset = 4;

  bad_jpg = 0;
  extend_jpg = 0;
  normal_jpg = 0;

  time_in_loop = 0;
  time_in_camera = 0;
  time_in_sd = 0;
  time_in_good = 0;
  time_total = 0;
  time_in_web1 = 0;
  time_in_web2 = 0;
  delay_wait_for_sd = 0;
  wait_for_cam = 0;
  very_high = 0;

  time_in_sd += (millis() - start);

  logfile.flush();
  avifile.flush();

} // end of start avi

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
//  another_save_avi saves another frame to the avi file, uodates index
//           -- pass in a fb pointer to the frame to add
//

static void another_save_avi(uint8_t* fb_buf, int fblen ) {
  long start = millis();

  int fb_block_length;
  uint8_t* fb_block_start;

  jpeg_size = fblen;

  remnant = (4 - (jpeg_size & 0x00000003)) & 0x00000003;

  long bw = millis();
  long frame_write_start = millis();

  int block_delay[10];
  int block_num = 0;

  fb_record_static[0] = 0x30;       // "00dc"
  fb_record_static[1] = 0x30;
  fb_record_static[2] = 0x64;
  fb_record_static[3] = 0x63;

  int jpeg_size_rem = jpeg_size + remnant;

  fb_record_static[4] = jpeg_size_rem % 0x100;
  fb_record_static[5] = (jpeg_size_rem >> 8) % 0x100;
  fb_record_static[6] = (jpeg_size_rem >> 16) % 0x100;
  fb_record_static[7] = (jpeg_size_rem >> 24) % 0x100;

  fb_block_start = fb_buf;

  if (fblen > fbs * 1024 - 8 ) {                     // fbs is the size of frame buffer static
    fb_block_length = fbs * 1024;
    fblen = fblen - (fbs * 1024 - 8);
    memcpy(fb_record_static + 8, fb_block_start, fb_block_length - 8);
    fb_block_start = fb_block_start + fb_block_length - 8;

  } else {
    fb_block_length = fblen + 8  + remnant;
    memcpy(fb_record_static + 8, fb_block_start,  fblen);
    fblen = 0;
  }

  size_t err = avifile.write(fb_record_static, fb_block_length);

  if (err != fb_block_length) {
    start_record = 0;
    jpr("Giving up - Error on avi write: err = %d, len = %d \n", err, fb_block_length);
    return;
  }

  if (block_num < 10) block_delay[block_num++] = millis() - bw;

  while (fblen > 0) {

    if (fblen > fbs * 1024) {
      fb_block_length = fbs * 1024;
      fblen = fblen - fb_block_length;
    } else {
      fb_block_length = fblen  + remnant;
      fblen = 0;
    }

    memcpy(fb_record_static, fb_block_start, fb_block_length);

    size_t err = avifile.write(fb_record_static,  fb_block_length);

    if (err != fb_block_length) {
      jpr("Giving up - Error on avi write: err = %d, len = %d \n", err, fb_block_length);
      return;
    }

    if (block_num < 10) block_delay[block_num++] = millis() - bw;

    fb_block_start = fb_block_start + fb_block_length;
    delay(0);
  }


  movi_size += jpeg_size;
  uVideoLen += jpeg_size;
  long frame_write_end = millis();

  print_2quartet(idx_offset, jpeg_size, idxfile);

  idx_offset = idx_offset + jpeg_size + remnant + 8;

  movi_size = movi_size + remnant;

  if ( do_it_now == 1 ) {  // && frame_cnt < 1011
    do_it_now = 0;
    //jpr("Frame %6d, len %6d, extra  %4d, cam time %7d,  sd time %4d -- \n", gframe_cnt, gfblen, gj - 1, gmdelay / 1000, millis() - bw);
    jpr("Frame %6d, len %6d, cam time %7d,  sd time %4d -- \n", gframe_cnt, gfblen, gmdelay / 1000, millis() - bw);
    logfile.flush();
  }

  totalw = totalw + millis() - bw;
  time_in_sd += (millis() - start);


  if ( (millis() - bw) > totalw / frame_cnt * 10) {
    unsigned long x = avifile.position();
    jpr ("Frame %6d, sd time very high %4d >>> %4d -- pos %X, ",  frame_cnt, millis() - bw, (totalw / frame_cnt), x );

    very_high++;
    jpr("Block %d, delay %5d ... \n", 0, block_delay[0]);
    //for (int i = 1; i < block_num; i++) {
    //  jpr("Block %d, delay %5d ..., ", i, block_delay[i] - block_delay[i - 1]);
    //}
    //Serial.println(" ");
    //logfile.println(" ");
  }
  avifile.flush();
  idxfile.flush();

} // end of another_pic_avi

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
//  end_avi writes the index, and closes the files
//

static void end_avi() {

  long start = millis();

  unsigned long current_end = avifile.position();

  jpr("End of avi - closing the files");

  if (frame_cnt <  5 ) {
    jpr("Recording screwed up, less than 5 frames, forget index\n");
    idxfile.close();
    avifile.close();
    int xx = remove("/idx.tmp");
    int yy = remove(avi_file_name);

  } else {

    elapsedms = millis() - startms;

    float fRealFPS = (1000.0f * (float)frame_cnt) / ((float)elapsedms) * speed_up_factor;

    float fmicroseconds_per_frame = 1000000.0f / fRealFPS;
    uint8_t iAttainedFPS = round(fRealFPS) ;
    uint32_t us_per_frame = round(fmicroseconds_per_frame);

    //Modify the MJPEG header from the beginning of the file, overwriting various placeholders

    avifile.seek( 4 , SeekSet);
    print_quartet(movi_size + 240 + 16 * frame_cnt + 8 * frame_cnt, avifile);

    avifile.seek( 0x20 , SeekSet);
    print_quartet(us_per_frame, avifile);

    unsigned long max_bytes_per_sec = (1.0f * movi_size * iAttainedFPS) / frame_cnt;

    avifile.seek( 0x24 , SeekSet);
    print_quartet(max_bytes_per_sec, avifile);

    avifile.seek( 0x30 , SeekSet);
    print_quartet(frame_cnt, avifile);

    avifile.seek( 0x8c , SeekSet);
    print_quartet(frame_cnt, avifile);

    avifile.seek( 0x84 , SeekSet);
    print_quartet((int)iAttainedFPS, avifile);

    avifile.seek( 0xe8 , SeekSet);
    print_quartet(movi_size + frame_cnt * 8 + 4, avifile);

    jpr("\n*** Video recorded and saved ***\n");

    jpr("Recorded %5d frames in %5d seconds\n", frame_cnt, elapsedms / 1000);
    jpr("File size is %u bytes\n", movi_size + 12 * frame_cnt + 4);
    jpr("Adjusted FPS is %5.2f\n", fRealFPS);
    jpr("Max data rate is %lu bytes/s\n", max_bytes_per_sec);
    jpr("Frame duration is %d us\n", us_per_frame);
    jpr("Average frame length is %d bytes\n", uVideoLen / frame_cnt);
    jpr("Average picture time (ms) %f\n", 1.0 * totalp / frame_cnt);
    jpr("Average write time (ms)  %f\n", 1.0 * totalw / frame_cnt );
    jpr("Normal jpg % %3.1f\n", 100.0 * normal_jpg / frame_cnt );
    jpr("Extend jpg % %3.1f\n", 100.0 * extend_jpg / frame_cnt );
    jpr("Bad    jpg % %6.5f\n", 100.0 * bad_jpg / frame_cnt);
    jpr("Slow sd writes %d, %5.3f %% \n", very_high, 100.0 * very_high / frame_cnt, 5 );

    jpr("Writng the index, %d frames\n", frame_cnt);

    avifile.seek( current_end , SeekSet);

    idxfile.close();

    size_t i1_err = avifile.write(idx1_buf, 4);

    print_quartet(frame_cnt * 16, avifile);

    idxfile = SD_MMC.open("/idx.tmp", "r");

    if (idxfile)  {
      //Serial.printf("File open: %s\n", "//idx.tmp");
      //logfile.printf("File open: %s\n", "/idx.tmp");
    }  else  {
      jpr("Could not open index file");
      major_fail();
    }

    char * AteBytes;
    AteBytes = (char*) malloc (8);

    for (int i = 0; i < frame_cnt; i++) {
      size_t res = idxfile.readBytes( AteBytes, 8);
      size_t i1_err = avifile.write(dc_buf, 4);
      size_t i2_err = avifile.write(zero_buf, 4);
      size_t i3_err = avifile.write((uint8_t *)AteBytes, 8);
    }

    free(AteBytes);

    idxfile.close();
    avifile.close();

    //    int resss = SD_MMC.mkdir(the_directory);
    //    Serial.printf("remake the foler ?? %d\n",resss);
    int xx = SD_MMC.remove("/idx.tmp");
  }

  jpr("---\n");

  time_in_sd += (millis() - start);

  //Serial.println("");
  time_total = millis() - startms;
  jpr("waiting for cam %10dms, %4.1f%%\n", wait_for_cam , 100.0 * wait_for_cam  / time_total);
  jpr("Time in camera  %10dms, %4.1f%%\n", time_in_camera, 100.0 * time_in_camera / time_total);
  jpr("waiting for sd  %10dms, %4.1f%%\n", delay_wait_for_sd , 100.0 * delay_wait_for_sd  / time_total);
  jpr("Time in sd      %10dms, %4.1f%%\n", time_in_sd    , 100.0 * time_in_sd     / time_total);
  jpr("web (core 1)    %10dms, %4.1f%%\n", time_in_web1  , 100.0 * time_in_web1   / time_total);
  jpr("web (core 0)    %10dms, %4.1f%%\n", time_in_web2  , 100.0 * time_in_web2   / time_total);
  jpr("time total      %10dms, %4.1f%%\n", time_total    , 100.0 * time_total     / time_total);

  logfile.flush();

  if (file_number == 100) {
    reboot_now = true;
  }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Time
#include "time.h"

#include "WiFi.h"
#include <WiFiMulti.h>
WiFiMulti jMulti;
#include <ArduinoOTA.h>
char ssidota[20];
#include "ESPmDNS.h"

#include "ESPxWebFlMgr.h"          //v56
const word filemanagerport = 8080;
ESPxWebFlMgr filemgr(filemanagerport); // we want a different port than the webserver


time_t now;
struct tm timeinfo;
char localip[20];
WiFiEventId_t eventID;
#include "esp_wifi.h"
bool found_router = false;

bool init_wifi() {

  int connAttempts = 0;

  //uint32_t brown_reg_temp = READ_PERI_REG(RTC_CNTL_BROWN_OUT_REG);
  //Serial.printf("Brownout was %d\n", brown_reg_temp);
  //WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  //WiFi.disconnect(true, true);

  WiFi.setHostname(devname);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(devname);

  char ssidch1[20];
  char passch1[20];
  char ssidch2[20];
  char passch2[20];
  char ssidch3[20];
  char passch3[20];

  if (cssid3 == "ssid") {
    cssid3 = String(devname);
  }

  cssid1.toCharArray(ssidch1, cssid1.length() + 1);
  cpass1.toCharArray(passch1, cpass1.length() + 1);
  cssid2.toCharArray(ssidch2, cssid2.length() + 1);
  cpass2.toCharArray(passch2, cpass2.length() + 1);
  cssid3.toCharArray(ssidch3, cssid3.length() + 1);
  cssid3.toCharArray(ssidota, cssid3.length() + 1);
  cpass3.toCharArray(passch3, cpass3.length() + 1);


  jpr("\n>>>>>>>>>>>>>>>>>>>>>%s<\n", ssidch1);
  jpr(">>>>>>>>>>>>>>>>>>>>>%s<\n", ssidch2);
  jpr(">>>>>>>>>>>>>>>>>>>>>%s< / >%s<\n", ssidch3, passch3);

  if (String(cssid1) != "ssid") {
    found_router = true;
    jMulti.addAP(ssidch1, passch1);
  }
  if (String(cssid2) != "ssid") {
    found_router = true;
    jMulti.addAP(ssidch2, passch2);
  }
  if (found_router) {
    jMulti.run();
  }
  String wifiMacString = WiFi.macAddress();
  Serial.println(wifiMacString);
  String idfver = esp_get_idf_version();
  Serial.println(esp_get_idf_version());

  jpr("Setting AP (Access Point)…");

  WiFi.softAP(ssidch3, passch3);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  sprintf(localip, "%s", WiFi.softAPIP().toString().c_str());
  Serial.print("AP IP: "); Serial.println(localip); Serial.println(" ");

  /*
    while (WiFi.status() != WL_CONNECTED ) {
      delay(1000);
      Serial.print(".");
      if (connAttempts++ == 5) break;     // try for 15 seconds to get internet, then give up
    }
  */
  configTime(0, 0, "pool.ntp.org");

  char tzchar[60];
  TIMEZONE.toCharArray(tzchar, TIMEZONE.length() + 1);        // name of your camera for mDNS, Router, and filenames

  Serial.printf("Char >%s<\n", tzchar);
  setenv("TZ", tzchar, 1);  // mountain time zone from #define at top
  tzset();

  time(&now);

  while (now < 5) {        // try for 15 seconds to get the time, then give up - 10 seconds after boot
    delay(1000);
    Serial.print("o");
    time(&now);
  }

  Serial.print("\nLocal time: "); Serial.print(ctime(&now));
  sprintf(localip, "%s", WiFi.localIP().toString().c_str());
  Serial.print("IP: "); Serial.println(localip); Serial.println(" ");

  if (!MDNS.begin(devname)) {
    jpr("Error setting up MDNS responder!");
  } else {
    jpr("mDNS responder started '%s'\n", devname);
  }

  eventID = WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    //  info.disconnected.reason ==>  info.wifi_sta_disconnected.reason - update with esp32_arduino 2.00 v58
    if (info.wifi_sta_disconnected.reason != 201) {
      jpr( "\nframe_cnt: %8d, WiFi event Reason: %d , Status: %d\n", frame_cnt, info.wifi_sta_disconnected.reason, WiFi.status());
    }
  });


  wifi_ps_type_t the_type;
  esp_err_t get_ps = esp_wifi_get_ps(&the_type);
  //Serial.printf("The power save was : %d\n", the_type);
  esp_err_t set_ps = esp_wifi_set_ps(WIFI_PS_NONE);
  esp_err_t new_ps = esp_wifi_get_ps(&the_type);
  //Serial.printf("The power save is : %d\n", the_type);

  //WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, brown_reg_temp);

  return true;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
//
#include <HTTPClient.h>

httpd_handle_t camera_httpd = NULL;
httpd_handle_t stream81_httpd = NULL;
httpd_handle_t stream82_httpd = NULL;

char the_page[4200];
int previous_capture = 0;
int capture_timer = 0;
int captures = 0;
int total_captures = 0;
int skips = 0;
int extras = 0;
#include "lwip/sockets.h"
static esp_err_t capture_handler(httpd_req_t *req) {

  long start = millis();

  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  char fname[100];
  int file_number = 0;

  char  buf[120];
  size_t buf_len;

  if (capture_timer + 30000 <  millis() ) {
    if  (frame_cnt < 1000 ) {
      jpr("Total captures %5d, Last 30 sec: captures %d, %0.1f per second, skips %d, extras %d\n", total_captures, captures, 1000.0 * captures / (millis() - capture_timer), skips, extras);

      print_mem("capture");

      int sock = httpd_req_to_sockfd(req);
      jpr("Socket: %d\n", httpd_req_to_sockfd(req));
      print_sock(sock);
    }
    
    captures = 1;
    total_captures++;
    skips = 0;
    extras = 0;
    capture_timer = millis();
  } else {
    captures++;
    total_captures++;
  }

  if (millis() - previous_capture < 50) { // limit captures to 20 per second (50) ... make that 13 per second (75)
    //Serial.printf("s");
    skips++;
    res = httpd_resp_send_408(req); // just let the requests be missed rather than rejecting it //61
  } else {
    previous_capture = millis();
    file_number++;
    sprintf(fname, "inline; filename=capture_%d.jpg", file_number);

    xSemaphoreTake( baton, portMAX_DELAY );

    if (fb_record_time > (millis() - 500)) {
      //Serial.printf("-");
      fb_capture_len = fb_record_len;
      fb_capture_time = fb_record_time;
      memcpy(fb_capture, fb_record,  fb_record_len);  // v59.5
      xSemaphoreGive( baton );
      httpd_resp_set_type(req, "image/jpeg");
      httpd_resp_set_hdr(req, "Content-Disposition", fname);
      res = httpd_resp_send(req, (const char *)fb_capture, fb_capture_len);
    } else {
      xSemaphoreGive( baton );
      fb = esp_camera_fb_get(); //get_good_jpeg();
      extras++;
      //Serial.print("N");
      //Serial.printf("millis %d, fb1 %d, fb2 %d\n", millis(), fb_record_time, fb_streaming_time);
      if (!fb) {
        Serial.println("Photos - Camera Capture Failed");
        res = httpd_resp_send_408(req);
        //res = ESP_FAIL;
        //start_streaming = false;
      } else {
        xSemaphoreTake( baton, portMAX_DELAY );
        fb_capture_len = fb->len;
        fb_capture_time = millis();
        memcpy(fb_capture, fb->buf, fb->len);
        xSemaphoreGive( baton );
        esp_camera_fb_return(fb);
        httpd_resp_set_type(req, "image/jpeg");
        httpd_resp_set_hdr(req, "Content-Disposition", fname);
        res = httpd_resp_send(req, (const char *)fb_capture, fb_capture_len);
      }
    }
  }

  time_in_web1 += (millis() - start);

  return res;
}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
//
static esp_err_t index_handler(httpd_req_t *req) {

  long start = millis();

  int buf_len;
  char  buf[120];
  int hdr_len ;

  buf_len = httpd_req_get_hdr_value_len(req, "Host") + 1;

  if (httpd_req_get_hdr_value_str(req, "Host", localip, buf_len) == ESP_OK) {
    //Serial.printf( "Found header => Host: %s\n", localip);
  }

  //sprintf(localip, "%s", buf);
  /*
    buf_len = httpd_req_get_url_query_len(req) + 1;
    if (buf_len > 1) {
      if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
        Serial.printf("Found URL query => %s", buf);
      }
    }
  */
  print_mem("index_handler");

  const char the_message[] = "Status";

  time(&now);
  const char *strdate = ctime(&now);

  int tot = SD_MMC.totalBytes() / (1024 * 1024);
  int use = SD_MMC.usedBytes() / (1024 * 1024);
  long rssi = WiFi.RSSI();

  //const query = `${baseHost}:8080/e?edit=config.txt`

  const char msg[] PROGMEM = R"rawliteral(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%s ESP32-CAM Video Recorder Junior</title>
<script>
function initialize() {
   var baseHost = document.location.origin
   const query = `${baseHost}/time?time=`
   const x = new Date();
   var timing = x.getTime() / 1000;
   const query2 = query + String(timing)
   fetch(query2)
      .then(response => {
         console.log(`request to ${query2} finished, status: ${response.status}`)
      })
}   
   </script>
      </head>
       <body onload="initialize()" style="background-color: white">
        
</head>
<body>
<h1>%s<br>ESP32-CAM Video Recorder Junior %s <br><font color="red">%s</font></h1><br>

 Used / Total SD Space <font color="red"> %d MB / %d MB</font>, Rssi %d<br>

 Filename: %s <br>
 Framesize %d, Quality %d, Frame %d <br>
 Record Interval %dms, Stream Interval %dms <br>
 Avg framesize %d, fps %.1f <br>
 Time left in current video %d seconds<br>

 <h3><a href="http://%s/">http://%s/</a></h3>
 Current Frame:<br>
 <img src="http://%s/capture"/> <br>
 First Frame of Current Recording: (see more in File Management section)<br>
 <img src="http://%s/find?f=/%s&n=0"> <br>
 <h3>Streaming</h3>
 <a href="http://%s:81/stream"><button>Stream 81</button></a>
 <a href="http://%s:82/stream"><button>Stream 82</button></a>
 <h3>Series of pictures</h3>
 <a href="http://%s/photos"><button>10 x 3 sec</button> </a>
 <a href="http://%s/fphotos"><button>10 x 1 sec</button></a>
 <a href="http://%s/sphotos"><button>120 x 15 sec</button></a> 
 <h3>Recording is <font color="red"> %s </font> - overrides hardware pin 12 stop/start</h3>
 <a href="http://%s/start"><button>start</button> </a>
 <a href="http://%s/stop"><button>stop</button></a> 
 <h3>File Management</h3>
 <h4>
 <a href="http://%s:%d/e?edit=config2.txt"><button>edit config2.txt </button></a>
 <a href="http://%s:%d"><button>File Manager - download, delete, view videos </button></a> </h4>

 <h4><a href="http://%s/restart"><button>End recording, and start new video (write the index) </button></a></h4>
 <h4><a href="http://%s/reboot"><button>End recording, and reboot (using new settings)</button> </a></h4>
 <br>
SourceCode:  https://github.com/jameszah/ESP32-CAM-Video-Recorder-junior/<br>
One-Click Installer: https://jameszah.github.io/ESP32-CAM-VideoCam/<br>
James Zahary - Dec 8, 2024 -- May 18, 2022<br>
<a href="https://ko-fi.com/jameszah">Free coffee (not AP mode)</a>

<br>
</body>
</html>)rawliteral";

  int time_left = (- millis() +  (avi_start_time + avi_length * 1000)) / 1000;
  if (start_record == 0) {
    time_left = 0;
  }

  String stopstart = "Stopped";
  if (start_record) {
    stopstart = "Recording";
  }

  sprintf(the_page, msg, devname, devname, vernum, strdate, use, tot, rssi, avi_file_name,
          framesize, quality, frame_cnt, frame_interval, stream_delay,
          most_recent_avg_framesize, most_recent_fps, time_left, localip,  localip,  localip,  localip, avi_file_name,
          localip, localip, localip, localip, localip, stopstart.c_str(), localip, localip, localip, filemanagerport, localip, filemanagerport,
          localip, localip,  localip  );

  httpd_resp_send(req, the_page, strlen(the_page));

  time_in_web1 += (millis() - start);
  return ESP_OK;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
//
static esp_err_t time_handler(httpd_req_t *req) {
  esp_err_t res = ESP_OK;

  char  buf[120];
  size_t buf_len;
  char  new_res[20];
  struct tm timeinfo;
  time_t now;

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      //Serial.printf("Found URL query => %s", buf);
      char param[32];

      if (httpd_query_key_value(buf, "time", param, sizeof(param)) == ESP_OK) {

        now = (time_t)atol(param);
        //Serial.print("new time: "); Serial.println(ctime(&now));
        //Serial.printf(">%i<", now);

        char tzchar[60];
        TIMEZONE.toCharArray(tzchar, TIMEZONE.length() + 1);        // name of your camera for mDNS, Router, and filenames
        setenv("TZ", tzchar, 1);  // mountain time zone from #define at top
        tzset();

        struct timeval tv;
        tv.tv_sec = now;
        tv.tv_usec = 0;
        settimeofday(&tv, NULL);

        //time(&now);
        //Serial.print("\nLocal time: "); Serial.println(ctime(&now));
        /*
                time_t rawtime;
                struct tm * ptm;
                time ( &rawtime );
                ptm = gmtime ( &rawtime );
                Serial.printf ("GMT: %2d:%02d\n", (ptm->tm_hour) % 24, ptm->tm_min);
        */
      }
    }
  }
  const char the_message[] = "Status";

  time(&now);
  const char *strdate = ctime(&now);

  const char msg[] PROGMEM = R"rawliteral(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%s ESP32-CAM Video Recorder Junior</title>
</head>
<body>
<h1>%s<br>ESP32-CAM Video Recorder Junior %s <br><font color="red">%s</font></h1><br>
 <br>
 got a time sync ...
 <br>


</body>
</html>)rawliteral";

  sprintf(the_page, msg, devname, devname, vernum, strdate );

  httpd_resp_send(req, the_page, strlen(the_page));

  return ESP_OK;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
//
static esp_err_t photos_handler(httpd_req_t *req) {

  long start = millis();

  print_mem("photos_handler");

  const char the_message[] = "Status";

  time(&now);
  const char *strdate = ctime(&now);

  const char msg[] PROGMEM = R"rawliteral(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%s ESP32-CAM Video Recorder Junior</title>
</head>
<body>
<h1>%s<br>ESP32-CAM Video Recorder Junior %s <br><font color="red">%s</font></h1><br>
 <br>
 One photo every 3 seconds for 30 seconds - roll forward or back - refresh for more live photos
 <br>

<br><div id="image-container"></div>
<script>
document.addEventListener('DOMContentLoaded', function() {
  var c = document.location.origin;
  const ic = document.getElementById('image-container');  
  var i = 1;
  
  var timing = 3000; // time between snapshots for multiple shots

  function loop() {
    ic.insertAdjacentHTML('beforeend','<img src="'+`${c}/capture?_cb=${Date.now()}`+'">')
    ic.insertAdjacentHTML('beforeend','<br>')
    ic.insertAdjacentHTML('beforeend',Date())
    ic.insertAdjacentHTML('beforeend','<br>')

    i = i + 1;
    if ( i <= 10 ) {             // 10 frames
      window.setTimeout(loop, timing);
    }
  }
  loop();
  
})
</script><br>
</body>
</html>)rawliteral";

  sprintf(the_page, msg, devname, devname, vernum, strdate );

  httpd_resp_send(req, the_page, strlen(the_page));
  time_in_web1 += (millis() - start);
  return ESP_OK;
}

static esp_err_t fphotos_handler(httpd_req_t *req) {

  long start = millis();

  print_mem("fphotos_handler");

  const char the_message[] = "Status";

  time(&now);
  const char *strdate = ctime(&now);

  const char msg[] PROGMEM = R"rawliteral(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%s ESP32-CAM Video Recorder Junior</title>
</head>
<body>
<h1>%s<br>ESP32-CAM Video Recorder Junior %s <br><font color="red">%s</font></h1><br>
 <br>
 One photo every 1 seconds for 10 seconds - roll forward or back - refresh for more live photos
 <br>

<br><div id="image-container"></div>
<script>
document.addEventListener('DOMContentLoaded', function() {
  var c = document.location.origin;
  const ic = document.getElementById('image-container');  
  var i = 1;
  
  var timing = 1000; // time between snapshots for multiple shots

  function loop() {
    ic.insertAdjacentHTML('beforeend','<img src="'+`${c}/capture?_cb=${Date.now()}`+'">')
    ic.insertAdjacentHTML('beforeend','<br>')
    ic.insertAdjacentHTML('beforeend',Date())
    ic.insertAdjacentHTML('beforeend','<br>')

    i = i + 1;
    if ( i <= 10 ) {             // 10 frames
      window.setTimeout(loop, timing);
    }
  }
  loop();
  
})
</script><br>
</body>
</html>)rawliteral";

  sprintf(the_page, msg, devname, devname, vernum, strdate );

  httpd_resp_send(req, the_page, strlen(the_page));
  time_in_web1 += (millis() - start);
  return ESP_OK;
}

static esp_err_t sphotos_handler(httpd_req_t *req) {

  long start = millis();

  print_mem("sphotos_handler");

  const char the_message[] = "Status";

  time(&now);
  const char *strdate = ctime(&now);

  const char msg[] PROGMEM = R"rawliteral(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%s ESP32-CAM Video Recorder Junior</title>
</head>
<body>
<h1>%s<br>ESP32-CAM Video Recorder Junior %s <br><font color="red">%s</font></h1><br>
 <br>
 One photo every 15 seconds for 30 minutes - roll forward or back - refresh for more live photos
 <br>

<br><div id="image-container"></div>
<script>
document.addEventListener('DOMContentLoaded', function() {
  var c = document.location.origin;
  const ic = document.getElementById('image-container');  
  var i = 1;
  
  var timing = 15000; // time between snapshots for multiple shots

  function loop() {
    ic.insertAdjacentHTML('beforeend','<img src="'+`${c}/capture?_cb=${Date.now()}`+'">')
    ic.insertAdjacentHTML('beforeend','<br>')
    ic.insertAdjacentHTML('beforeend',Date())
    ic.insertAdjacentHTML('beforeend','<br>')

    i = i + 1;
    if ( i <= 120 ) {             
      window.setTimeout(loop, timing);
    }
  }
  loop();
  
})
</script><br>
</body>
</html>)rawliteral";

  sprintf(the_page, msg, devname, devname, vernum, strdate );

  httpd_resp_send(req, the_page, strlen(the_page));
  time_in_web1 += (millis() - start);
  return ESP_OK;
}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
//
static esp_err_t reboot_handler(httpd_req_t *req) {

  long start = millis();

  print_mem("reboot_handler");

  start_record = 0;
  web_stop = true;
  reboot_now = true;

  const char the_message[] = "Status";

  time(&now);
  const char *strdate = ctime(&now);

  const char msg[] PROGMEM = R"rawliteral(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%s ESP32-CAM Video Recorder Junior</title>
</head>
<body>
<h1>%s<br>ESP32-CAM Video Recorder Junior %s <br><font color="red">%s</font></h1><br>
 <br>
 Ending current recording, and rebooting ...
 <br>

<br>
</body>
</html>)rawliteral";

  sprintf(the_page, msg, devname, devname, vernum, strdate );

  httpd_resp_send(req, the_page, strlen(the_page));
  time_in_web1 += (millis() - start);

  return ESP_OK;
}

static esp_err_t restart_handler(httpd_req_t *req) {

  long start = millis();

  print_mem("restart_handler");

  restart_now = true;

  const char the_message[] = "Status";

  time(&now);
  const char *strdate = ctime(&now);

  const char msg[] PROGMEM = R"rawliteral(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%s ESP32-CAM Video Recorder Junior</title>
</head>
<body>
<h1>%s<br>ESP32-CAM Video Recorder Junior %s <br><font color="red">%s</font></h1><br>
 <br>
 Ending current recording, and starting next video ...
 <br>
<br>
</body>
</html>)rawliteral";

  sprintf(the_page, msg, devname, devname, vernum, strdate );

  httpd_resp_send(req, the_page, strlen(the_page));
  time_in_web1 += (millis() - start);

  return ESP_OK;
}

static esp_err_t start_handler(httpd_req_t *req) {

  web_stop = false;
  Serial.printf("Web record start, it was %d\n", web_stop);
  delay(500);
  esp_err_t xx = index_handler(req);
  return ESP_OK;

}
static esp_err_t stop_handler(httpd_req_t *req) {

  web_stop = true;
  Serial.printf("Web record stop, it was %d\n", web_stop);
  delay(500);
  esp_err_t xx = index_handler(req);
  return ESP_OK;

}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
//  Streaming stuff based on Random Nerd
//

bool start_streaming = false;
bool stream_82 = false;
bool stream_81 = false;

httpd_req_t *req_82;
httpd_req_t *req_81;

#define PART_BOUNDARY "123456789000000000000987654321"

static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

void the_streaming_loop (void* pvParameter);
int stream_81_frames ;
long stream_81_start ;
int stream_82_frames ;
long stream_82_start ;

static esp_err_t stream_82_handler(httpd_req_t *req) {

  esp_err_t res;
  long start = millis();

  print_mem("stream_82_handler");

  stream_82 = true;
  req_82 = req;
  stream_82_frames = 0;
  stream_82_start = millis();

  if (stream_82) {
    res = httpd_resp_set_type(req_82, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
      stream_82 = false;
    }
  }

  time_in_web1 += (millis() - start);

  while (stream_82 == true) {          // we have to keep the *req alive
    delay(1000);
    //Serial.print("<82>");
  }
  Serial.println(" stream_82 done");
  delay(500);
  httpd_resp_send_408(req_82);
  req_82 = NULL;

  return ESP_OK;
}

static esp_err_t stream_81_handler(httpd_req_t *req) {

  esp_err_t res;
  long start = millis();

  print_mem("stream_81_handler");

  stream_81 = true;
  req_81 = req;
  stream_81_frames = 0;
  stream_81_start = millis();

  time_in_web1 += (millis() - start);

  if (stream_81) {
    res = httpd_resp_set_type(req_81, _STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
      stream_81 = false;
    }
  }

  while (stream_81 == true) {          // we have to keep the *req alive
    delay(1000);
    //Serial.print("<81>");
  }
  Serial.println(" stream_81 done");
  delay(500);
  httpd_resp_send_408(req_81);
  req_81 = NULL;
  return ESP_OK;
}


////////////////////////////////
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
//  Streaming stuff based on Random Nerd
//


void the_streaming_loop (void* pvParameter) {

  camera_fb_t * fb = NULL;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];

  long start = millis();

  print_mem("the_streaming_loop");

  jprln("Starting the streaming");

  while (true) {

    if (!stream_81 && !stream_82) {
      delay(5);
    } else {
      if (stream_81) stream_81_frames++;
      if (stream_82) stream_82_frames++;

      xSemaphoreTake( baton, portMAX_DELAY );

      if (fb_record_time > (millis() - 500)) {
        //Serial.printf("*");
        fb_streaming_len = fb_record_len;
        fb_streaming_time = fb_record_time;
        memcpy(fb_streaming, fb_record,  fb_record_len);  // v59.5
        xSemaphoreGive( baton );
      } else {
        xSemaphoreGive( baton );
        fb = esp_camera_fb_get(); //get_good_jpeg();
        //Serial.println("loop take");
        //Serial.printf("millis %d, fb1 %d, fb2 %d\n", millis(), fb_record_time, fb_streaming_time);
        if (!fb) {
          Serial.println("Photos - Camera Capture Failed");  // i guess we stream the previous contents of fb_streaming //34
          //start_streaming = false;
        } else {
          //34 xSemaphoreTake( baton, portMAX_DELAY );
          fb_streaming_len = fb->len;
          fb_streaming_time = millis();
          memcpy(fb_streaming, fb->buf, fb->len);
          //34 xSemaphoreGive( baton );
          esp_camera_fb_return(fb);
        }
      }

      _jpg_buf_len = fb_streaming_len;
      _jpg_buf = fb_streaming;

      size_t hlen = snprintf((char *)part_buf, 64, _STREAM_PART, _jpg_buf_len);

      long send_time = millis();
      long xx;
      xx = millis();

      if (stream_82) {
        res = httpd_resp_send_chunk(req_82, (const char *)part_buf, hlen);
        if (res != ESP_OK) {
          stream_82 = false;
          Serial.printf("Stream error - 82/1st %d\n", res);
        }
      }
      if (stream_81) {
        res = httpd_resp_send_chunk(req_81, (const char *)part_buf, hlen);
        if (res != ESP_OK) {
          stream_81 = false;
          Serial.printf("Stream error - 81/1st %d\n", res);
        }
      }

      xx = millis();

      if (stream_82) {
        res = httpd_resp_send_chunk(req_82, (const char *)_jpg_buf, _jpg_buf_len);
        if (res != ESP_OK) {
          stream_82 = false;
          Serial.printf("Stream error - 82/2nd %d\n", res);
        }
      }
      if (stream_81) {
        res = httpd_resp_send_chunk(req_81, (const char *)_jpg_buf, _jpg_buf_len);
        if (res != ESP_OK) {
          stream_81 = false;
          Serial.printf("Stream error - 81/2nd %d\n", res);
        }
      }

      xx = millis();

      if (stream_82) {
        res = httpd_resp_send_chunk(req_82, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        if (res != ESP_OK) {
          stream_82 = false;
          Serial.printf("Stream error - 82/3rd %d\n", res);
        }
      }
      if (stream_81) {
        res = httpd_resp_send_chunk(req_81, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        if (res != ESP_OK) {
          stream_81 = false;
          Serial.printf("Stream error - 81/3rd %d\n", res);
        }
      }

      if (stream_81_frames % 100 == 10) {
        if (Lots_of_Stats) {
          jpr("Stream 81 at %3.3f fps\n", (float)1000 * stream_81_frames / (millis() - stream_81_start));
        }
      }
      if (stream_82_frames % 100 == 10) {
        if (Lots_of_Stats) {
          jpr("Stream 82 at %3.3f fps\n", (float)1000 * stream_82_frames / (millis() - stream_82_start));
        }
      }

      int new_delay = stream_delay - (millis() - send_time);
      //Serial.printf(", streamdelay %5d, send_time %5d, newdelay %5d\n", stream_delay, millis() - send_time, new_delay);
      if (millis() - send_time > 5000) {
        new_delay = 1000;
        Serial.printf("wifi slow %d - take a 1s break\n", millis() - send_time);
      }

      if (new_delay < 10) {
        new_delay = 10;
      }

      delay(new_delay) ; //delay(stream_delay);

      start = millis();

    }
  }  // stream forever
}

void start_Stream_81_server() {
  httpd_config_t config2 = HTTPD_DEFAULT_CONFIG();
  config2.server_port = 81;
  config2.ctrl_port = 32123; //         = 32768,
  Serial.print("http Stream task prio: "); Serial.println(config2.task_priority);

  httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_81_handler,
    .user_ctx  = NULL
  };

  if (httpd_start(&stream81_httpd, &config2) == ESP_OK) {
    httpd_register_uri_handler(stream81_httpd, &stream_uri);
  } else {
    Serial.println("Error with stream start 81");
  }

  Serial.println("Stream 81 http started");
}

void start_Stream_82_server() {
  httpd_config_t config2 = HTTPD_DEFAULT_CONFIG();
  config2.server_port = 82;
  config2.ctrl_port = 32124; //         = 32768,
  Serial.print("http Stream task prio: "); Serial.println(config2.task_priority);

  httpd_uri_t stream_uri = {
    .uri       = "/stream",
    .method    = HTTP_GET,
    .handler   = stream_82_handler,
    .user_ctx  = NULL
  };

  if (httpd_start(&stream82_httpd, &config2) == ESP_OK) {
    httpd_register_uri_handler(stream82_httpd, &stream_uri);
  } else {
    Serial.println("Error with stream start 82");
  }

  Serial.println("Stream 82 http started");
}


//////////////////////////////
//61.3 oneframe find_a_frame (char * avi_file_name, int frame_pct) ; // from avi.cpp file

struct oneframe {
  uint8_t* the_frame;
  int the_frame_length;
  long the_frame_number;
  long the_frame_total;
};

//
// Reads an uint32_t in Big Endian at current file position
//
int read_quartet( File fd) {

  uint8_t y[4];
  size_t i1_err = fd.read(y , 4);
  uint32_t value = y[0] | y[1] << 8 | y[2] << 16 | y[3] << 24;
  //Serial.printf("read_quartet %d %d %d %d, %d\n", y[0], y[1], y[2], y[3], value);
  return value;
}
//
// Writes an uint32_t in Big Endian at current file position
//
static void inline print_dc_quartet(unsigned long i, File fd) {

  uint8_t y[8];
  y[0] = 0x30;       // "00dc"
  y[1] = 0x30;
  y[2] = 0x64;
  y[3] = 0x63;

  y[4] = i % 0x100;
  y[5] = (i >> 8) % 0x100;
  y[6] = (i >> 16) % 0x100;
  y[7] = (i >> 24) % 0x100;
  size_t i1_err = fd.write(y , 8);
}

oneframe find_a_frame (char * avi_file_name, long frame_num) {
  File findfile;

  oneframe x;
  findfile = SD_MMC.open(avi_file_name, "r");
  if (!findfile) {
    Serial.printf("Could not open %s file\n", avi_file_name);
    x.the_frame = NULL;
    return x;
  }  else  {
    //Serial.printf("Size %d\n",findfile.size());
    //Serial.printf("Last %d\n",findfile.getLastWrite());
    time_t lastw = findfile.getLastWrite();
    //Serial.printf("Lastw %d\n",lastw);

    time_t current;
    time (&current);

    int age = current - lastw;
    //Serial.printf("Age %d\n", age);

    //Serial.printf("File open: %s\n", avi_file_name);
    findfile.seek( 0x30 , SeekSet);
    long frame_total = read_quartet( findfile);
    //Serial.printf("Frames from file %ld\n", frame_total);

    if (age < 10) {
      //Serial.printf("Frame file %d, current %d\n", frame_total, frame_cnt - 1);
      frame_total = frame_cnt - 1;

      File idxfile =  SD_MMC.open("/idx.tmp", "r");
      if (!idxfile) {
        Serial.printf("Could not open /idx.tmp file\n");
        x.the_frame = NULL;
        return x;
      }

      int the_offset = frame_num * 8;
      idxfile.seek(the_offset, SeekSet);
      //Serial.printf("the frame %d, the offset %d\n", frame_num, the_offset);
      int the_addr = read_quartet (idxfile);
      int the_idx_len = read_quartet (idxfile);
      //Serial.printf("from index, the addr %d, the length %d\n",the_addr,the_idx_len);

      idxfile.close();

      findfile.seek( the_addr  + 236 , SeekSet);

      int the_oodc = read_quartet (findfile);
      //Serial.printf("the oodc %d\n",the_oodc);

      if (the_oodc != 1667510320) {
        Serial.printf("No frame %d, %d, %d\n%s file, num %d\n", frame_num, the_addr, the_idx_len);
        x.the_frame = NULL;
        return x;
      }
      //findfile.seek( the_addr + 8 + 236 , SeekSet);
      int the_len = read_quartet (findfile);
      //Serial.printf("frame len %d \n", the_len);

      //Serial.printf("Your frame is %d bytes, at address %d or %X\n", index_frame_length, index_frame_start, index_frame_start);
      uint8_t* fb_faf;
      fb_faf = (uint8_t*)ps_malloc(the_len + 24);

      // findfile.seek( 4 + 244 , SeekSet);

      size_t err = findfile.read(fb_faf, the_len);

      x.the_frame = fb_faf;
      x.the_frame_length = the_len;
      x.the_frame_number = frame_num;
      x.the_frame_total = frame_total;
      return x;


    }

    //findfile.seek( 0x8c , SeekSet);
    //long frame_cnt8c = read_quartet( findfile);
    //Serial.printf("Frames8c is %ld\n", frame_cnt8c);

    //int frame_num = 0.01 * frame_pct * frame_cnt;
    //Serial.printf("Frames pct %d, Frame num %d \n", frame_pct, frame_num);

    if (frame_total < frame_num) {
      Serial.printf("Only %ld frames, less than %ld frame_num -- start at 0\n", frame_cnt, frame_num);
      frame_num = 0;
    }
    if (frame_total == 3) {
      Serial.printf("Three 3 frames - we dont know how many! -- start at 0\n");

      frame_num = 0;
      findfile.seek( 4 + 236 , SeekSet);
      int the_oodc = read_quartet (findfile);
      if (the_oodc != 1667510320) {
        Serial.printf("No frame %s file, num %d\n", avi_file_name, frame_num);
        x.the_frame = NULL;
        return x;
      }
      int the_len = read_quartet (findfile);
      //Serial.printf("frame len %d \n", the_len);

      //Serial.printf("Your frame is %d bytes, at address %d or %X\n", index_frame_length, index_frame_start, index_frame_start);
      uint8_t* fb_faf;
      fb_faf = (uint8_t*)ps_malloc(the_len + 24);

      findfile.seek( 4 + 244 , SeekSet);

      size_t err = findfile.read(fb_faf, the_len);

      x.the_frame = fb_faf;
      x.the_frame_length = the_len;
      x.the_frame_number = frame_num;
      x.the_frame_total = frame_total;
      return x;
    }

    findfile.seek( 0xe8 , SeekSet);
    long index_start = read_quartet( findfile);
    //Serial.printf("Len of movi / index_start %ld\n", index_start);

    //bool success = findfile.seek(  , SeekEnd);

    //Serial.printf("Len of file %ld\n", findfile.size());
    //Serial.printf("Seek %d\n",  index_start + 244 + frame_num * 16 + 8);

    if (findfile.size() < index_start + 244 + frame_num * 16 + 8 , SeekSet) {
      Serial.printf("File too small / broken %s file\n", avi_file_name);
      x.the_frame = NULL;
      return x;
    }
    bool success = findfile.seek( index_start + 244 + frame_num * 16 + 8 , SeekSet);
    if (!success) {
      Serial.printf("File incomplete %s file\n", avi_file_name);
      x.the_frame = NULL;
      return x;

    }
    long index_frame_start = read_quartet( findfile);
    long index_frame_length = read_quartet( findfile);

    findfile.seek( index_frame_start + 236 , SeekSet);
    int the_oodc = read_quartet (findfile);
    if (the_oodc != 1667510320) {
      Serial.printf("No frame %s file, num %d\n", avi_file_name, frame_num);
      x.the_frame = NULL;
      return x;
    }
    int the_len = read_quartet (findfile);
    //Serial.printf("frame len %d \n", the_len);

    //Serial.printf("Your frame is %d bytes, at address %d or %X\n", index_frame_length, index_frame_start, index_frame_start);
    uint8_t* fb_faf;

    //fb_faf = (uint8_t*)ps_malloc(48 * 1024);  // danger 48kb may not be enough
    fb_faf = (uint8_t*)ps_malloc(the_len + 24);
    findfile.seek( index_frame_start + 244 , SeekSet);

    size_t err = findfile.read(fb_faf, index_frame_length);

    x.the_frame = fb_faf;
    x.the_frame_length = index_frame_length;
    x.the_frame_number = frame_num;
    x.the_frame_total = frame_total;
    return x;

  } // else yes to no avi file
}
char file_to_read[50];
char file_to_write[50];
//int newstart;
//int newend;
//int newskip;
//bool do_the_reparse = false;
bool do_the_reindex = false;
//bool done_the_reparse = false;
bool done_the_reindex = false;

static esp_err_t reindex_handler(httpd_req_t *req) {
  esp_err_t res = ESP_OK;

  print_mem("reindex_handler");

  char  buf[150];
  size_t buf_len;

  buf_len = httpd_req_get_url_query_len(req) + 1;

  if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
    //Serial.printf("Query => %s\n", buf);
    if (httpd_query_key_value(buf, "o", file_to_read, sizeof(file_to_read)) == ESP_OK) {
      //Serial.printf( "Found URL query parameter => file_to_read=>%s<\n", file_to_read);
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      //Serial.printf("Query => %s\n", buf);
      if (httpd_query_key_value(buf, "n", file_to_write, sizeof(file_to_write)) == ESP_OK) {
        //Serial.printf( "Found URL query parameter => file_to_write=>%s<\n", file_to_write);
      }
    }
  }

  do_the_reindex = true;

  while (!done_the_reindex) {
    delay(1000);
  }
  String x = " {\"status\":\"!!!DONE!!!\" }";
  const char* str = x.c_str();
  httpd_resp_send(req, str,  strlen(str));

  return res;
}

void re_index( char * avi_file_name, char * out_file_name) {

  //once++;
  //if (once > 1) return;

  extern uint8_t* fb_faf;
  uint16_t remnant = 0;

  // JamCam0005.0037.avi
  //const char * avi_file_name = "JamCam0090.0001.avi";
  const char * idx_file_name = "/re_idx.tmp"; // "/JamCam0190.0001.idx";
  //const char * out_file_name = "/JamCam0090.0001new.avi";

#define fbs 4 //  how many kb of static ram for psram -> sram buffer for sd write
  uint8_t fb_faf_static[fbs * 1024 + 20];

  File avifile = SD_MMC.open(avi_file_name, "r"); // avifile = SD_MMC.open(avi_file_name, "w");
  File idxfile = SD_MMC.open(idx_file_name, "w"); //idxfile = SD_MMC.open("/idx.tmp", "w");
  File outfile = SD_MMC.open(out_file_name, "w"); //idxfile = SD_MMC.open("/idx.tmp", "w");

  if (avifile) {
    //Serial.printf("File open: %s\n", avi_file_name);
  }  else  {
    Serial.printf("Could not open %s file\n", avi_file_name);
  }

  if (idxfile)  {
    //Serial.printf("File open: %s\n", idx_file_name);
  }  else  {
    Serial.printf("Could not open file %s\n", idx_file_name);
  }

  if (!avifile) {
    return;
  } else {
    //size_t err = avifile.read( fb_faf_static, 240);

    avifile.seek( 0x24 , SeekSet);
    int max_bytes_per_sec  = read_quartet( avifile);
    //Serial.printf("Max bytes per sec %d\n", max_bytes_per_sec);

    avifile.seek( 0x30 , SeekSet);
    int frame_cnt = read_quartet( avifile);
    //Serial.printf("Frames %d\n", frame_cnt);

    /*
        if (frame_cnt < frame_start) {
          Serial.printf("Only %d frames, less than %d frame_start -- start at 0\n", frame_cnt, frame_end);
          frame_start = 0;
        }
        // if frame_end is 0, or too high, it will go to frame_cnt,
        if (frame_cnt < frame_end || frame_end == 0) {
          Serial.printf("Only %d frames, less than %d frame_end -- end at max frames\n", frame_cnt, frame_end);
          frame_end = frame_cnt;
        }
    */
    int num_out_frames = frame_cnt;   // / (skip_frames + 1);
    Serial.printf("Original %d frames, so %d output frames\n", frame_cnt, num_out_frames );


    //avifile.seek( 0x8c , SeekSet);
    //int frame_cnt8c = read_quartet( avifile);

    avifile.seek( 0x84 , SeekSet);
    int iAttainedFPS = read_quartet( avifile);
    //Serial.printf("fps %d\n", iAttainedFPS);

    avifile.seek( 0xe8 , SeekSet);
    int index_start = read_quartet( avifile);
    //Serial.printf("Len of movi %d\n", index_start);

    Serial.printf("-----------------\n");

    avifile.seek( 0 , SeekSet);
    size_t err = avifile.read(fb_faf_static, AVIOFFSET);
    Serial.printf("avi read header %d\n", err);
    size_t err2 = outfile.write(fb_faf_static, AVIOFFSET);
    Serial.printf("avi write header %d\n", err2);
    outfile.seek( 240 , SeekSet);


    Serial.printf("-----------------\n");

    int xx2;
    int flen;
    int prev_frame_length = 0;
    int next_frame_start = 240;
    int new_frame_length;
    int prev_frame_start = 240;
    int index_frame_length;
    int index_frame_start;
    int idx_offset = 4;
    int movi_size = 0;
    int frame_cnt_out = 0;
    int frame_num = 0;

    avifile.seek( next_frame_start  , SeekSet);  //240

    bool one_more_frame = true;

    //for (int frame_num = 0; frame_num < frame_cnt; frame_num = frame_num + 1) {

    while (one_more_frame) {

      //i avifile.seek( index_start + 244 + frame_num * 16 + 8 , SeekSet);
      //i index_frame_start = read_quartet( avifile);
      //i index_frame_length = read_quartet( avifile);

      //avifile.seek( next_frame_start  , SeekSet);   // start at 240, then read everything - 2 quart + frame, then repeat

      int the_oodc = read_quartet (avifile);   //240
      if (the_oodc == 1667510320) {
        //Serial.printf("%d, good frame, num %d\n",the_oodc,frame_num);
      } else {
        Serial.printf("%d, bad frame, num %d\n", the_oodc, frame_num);
        break;
      }


      //avifile.seek( next_frame_start + 4  , SeekSet);
      new_frame_length = read_quartet( avifile); //244

      index_frame_length = new_frame_length; // reuse the variable

      prev_frame_start = next_frame_start;
      index_frame_start = prev_frame_start; // reuse

      next_frame_start = prev_frame_start + new_frame_length + 8;

      //prev_frame_length = new_frame_length;

      if (frame_num < 5 || frame_num % 500 == 0) {
        Serial.printf("Frame %4d, index len %9d, frame len %9d, index start %9d, frame start %9d\n", frame_num, index_frame_length, new_frame_length, index_frame_start + 236 , prev_frame_start);
      }

      if (frame_num < 5 || frame_cnt_out % 100 == 0) {
        Serial.printf("Frame %4d, index len %9d, frame len %9d, index start %9d, frame start %9d\n", frame_num, index_frame_length, new_frame_length, index_frame_start + 236 , prev_frame_start);
        Serial.printf("in %d, out %d\n", frame_num, frame_cnt_out);
      }

      //avifile.seek( index_frame_start + 244 , SeekSet); // already 248

      remnant = (4 - (index_frame_length & 0x00000003)) & 0x00000003;
      int index_frame_length_rem = index_frame_length + remnant;

      int left_to_write = index_frame_length_rem;

      // check next frame start
      int where_now = avifile.position();
      avifile.seek( where_now + left_to_write  , SeekSet);
      the_oodc = read_quartet (avifile);
      if (the_oodc == 1667510320) {
        //Serial.printf("%d, good frame, num %d\n",the_oodc,frame_num);
      } else {
        Serial.printf("%d, next frame is bad frame, num %d\n", the_oodc, frame_num);

        break;
      }
      avifile.seek( where_now  , SeekSet);
      // now write the dc and length

      print_dc_quartet( index_frame_length_rem, outfile);

      while (left_to_write > 0) {
        if (left_to_write > fbs * 1024) {
          size_t err = avifile.read(fb_faf_static, fbs * 1024);
          size_t err2 = outfile.write(fb_faf_static, fbs * 1024);
          left_to_write = left_to_write - fbs * 1024;
        } else {
          size_t err = avifile.read(fb_faf_static, left_to_write);
          size_t err2 = outfile.write(fb_faf_static, left_to_write);
          left_to_write = 0;
        }
      }

      movi_size += index_frame_length;
      movi_size += remnant;

      print_2quartet(idx_offset, index_frame_length, idxfile);

      idx_offset = idx_offset + index_frame_length_rem + 8;

      frame_cnt_out++;
      frame_num++;

    }  // every frame in file


    //Serial.printf("frame %4d, outfile %9d, avifile %9d, idxfile %9d\n", frame_cnt , outfile.position(), avifile.position(), avifile.position());

    idxfile.close();
    size_t i1_err = outfile.write(idx1_buf, 4);
    if (!i1_err) Serial.printf("idx write\n");

    print_quartet(frame_cnt_out * 16, outfile);

    idxfile = SD_MMC.open(idx_file_name, "r");
    if (idxfile)  {
      Serial.printf("File open: %s\n", idx_file_name);
    }  else  {
      Serial.printf("Could not open file %s\n", idx_file_name);
    }

    char * AteBytes;
    AteBytes = (char*) malloc (8);

    for (int i = 0; i < frame_cnt_out; i++) {
      size_t res = idxfile.readBytes( AteBytes, 8);
      if (!res) Serial.printf("idx read\n");
      size_t i1_err = outfile.write(dc_and_zero_buf, 8);
      if (!i1_err) Serial.printf("dc write\n");
      //size_t i2_err = outfile.write(zero_buf, 4);
      //if (!i2_err) Serial.printf("zero write\n");
      size_t i3_err = outfile.write((uint8_t *)AteBytes, 8);
      if (!i3_err) Serial.printf("ate write\n");
    }

    free(AteBytes);

    outfile.seek( 4 , SeekSet);         //shit
    print_quartet(movi_size + 240 + 16 * frame_cnt_out + 8 * frame_cnt_out, outfile);

    avifile.seek( 0xe8 , SeekSet);
    int lom = read_quartet( avifile);
    Serial.printf("Len of movi was %d, now is %d\n", lom, movi_size);

    outfile.seek( 0xe8 , SeekSet);
    // shit print_quartet (movi_size, outfile);
    print_quartet(movi_size + frame_cnt_out * 8 + 4, outfile);

    avifile.seek( 0x30 , SeekSet);
    int fc = read_quartet( avifile);
    Serial.printf("Frames was %d, now is %d\n", fc, frame_cnt_out);
    outfile.seek( 0x30 , SeekSet);
    print_quartet (frame_cnt_out, outfile);
    outfile.seek( 0x8c , SeekSet);
    print_quartet (frame_cnt_out, outfile);

    //avifile.seek( 0x84 , SeekSet);
    //int fps = read_quartet( avifile);
    //Serial.printf("fps was %d\n", fps);

    //float fnewfps = ( 1.0f * fps ) / (1 + skip_frames)   ;
    //int newfps = round(fnewfps);

    //Serial.printf("newfps is %f, %d\n", fnewfps, newfps);
    //avifile.seek( 0x84 , SeekSet);
    //print_quartet(newfps, avifile);

    //avifile.seek( 0x20 , SeekSet);
    //int us_per_frame = read_quartet( avifile);
    //Serial.printf("us_per_frame was %d\n", us_per_frame);

    //float newus = 1000000.0f / fnewfps;
    //uint32_t new_us_per_frame = round (newus);


    //Serial.printf("new_us_per_frame is %f, %d\n", newus, new_us_per_frame);
    //avifile.seek( 0x20 , SeekSet);
    //print_quartet(new_us_per_frame, avifile);


    idxfile.close();
    avifile.close();
    outfile.close();

    int xx = SD_MMC.remove(idx_file_name);
  }
}

void re_index_bad( char * avi_file_name) {

  //once++;
  //if (once > 1) return;

  extern uint8_t* fb_faf;
  uint16_t remnant = 0;

  const char * idx_file_name = "/reidx.tmp"; // "/JamCam0190.0001.idx";

  //#define fbs 4 //  how many kb of static ram for psram -> sram buffer for sd write
  //  uint8_t fb_faf_static[fbs * 1024 + 20];

  File  avifile = SD_MMC.open(avi_file_name, "w"); // avifile = SD_MMC.open(avi_file_name, "w");
  File idxfile = SD_MMC.open(idx_file_name, "w"); //idxfile = SD_MMC.open("/idx.tmp", "w");
  //outfile = SD_MMC.open(out_file_name, "w"); //idxfile = SD_MMC.open("/idx.tmp", "w");

  if (avifile) {
    Serial.printf("File open: %s\n", avi_file_name);
  }  else  {
    Serial.printf("Could not open %s file\n", avi_file_name);
  }

  if (idxfile)  {
    Serial.printf("File open: %s\n", idx_file_name);
  }  else  {
    Serial.printf("Could not open file %s\n", idx_file_name);
  }

  if (!avifile) {
    return;
  } else {
    //size_t err = avifile.read( fb_faf_static, 240);

    avifile.seek( 0x24 , SeekSet);
    int max_bytes_per_sec  = read_quartet( avifile);
    Serial.printf("Max bytes per sec %d\n", max_bytes_per_sec);

    avifile.seek( 0x30 , SeekSet);
    int frame_cnt = read_quartet( avifile);
    Serial.printf("Frames %d\n", frame_cnt);

    /*
        if (frame_cnt < frame_start) {
          Serial.printf("Only %d frames, less than %d frame_start -- start at 0\n", frame_cnt, frame_end);
          frame_start = 0;
        }
        // if frame_end is 0, or too high, it will go to frame_cnt,
        if (frame_cnt < frame_end || frame_end == 0) {
          Serial.printf("Only %d frames, less than %d frame_end -- end at max frames\n", frame_cnt, frame_end);
          frame_end = frame_cnt;
        }
    */
    int num_out_frames = frame_cnt;   // / (skip_frames + 1);
    Serial.printf("Original %d frames, so %d output frames\n", frame_cnt, num_out_frames );


    //avifile.seek( 0x8c , SeekSet);
    //int frame_cnt8c = read_quartet( avifile);

    avifile.seek( 0x84 , SeekSet);
    int iAttainedFPS = read_quartet( avifile);
    Serial.printf("fps %d\n", iAttainedFPS);

    avifile.seek( 0xe8 , SeekSet);
    int index_start = read_quartet( avifile);
    Serial.printf("Len of movi %d\n", index_start);

    Serial.printf("-----------------\n");

    /*
        avifile.seek( 0 , SeekSet);
        size_t err = avifile.read(fb_faf_static, AVIOFFSET);
        Serial.printf("avi read header %d\n", err);
        size_t err2 = outfile.write(fb_faf_static, AVIOFFSET);
        Serial.printf("avi write header %d\n", err2);

        outfile.seek( 240 , SeekSet);
    */

    Serial.printf("-----------------\n");

    int xx2;
    int flen;
    int prev_frame_length = 0;
    int next_frame_start = 240;
    int new_frame_length;
    int prev_frame_start = 240;
    int index_frame_length;
    int index_frame_start;
    int idx_offset = 4;
    int movi_size = 0;
    int frame_cnt_out = 0;
    int frame_num = 0;

    int the;
    next_frame_start = 232;
    avifile.seek(  next_frame_start , SeekSet);
    Serial.printf("addr %d, ", next_frame_start);
    the = read_quartet (avifile);

    next_frame_start = next_frame_start + 4;
    avifile.seek(  next_frame_start , SeekSet);
    Serial.printf("addr %d, ", next_frame_start);
    the = read_quartet (avifile);

    next_frame_start = next_frame_start + 4;
    avifile.seek(  next_frame_start , SeekSet);
    Serial.printf("addr %d, ", next_frame_start);
    the = read_quartet (avifile);

    next_frame_start = next_frame_start + 4;
    avifile.seek(  next_frame_start , SeekSet);
    Serial.printf("addr %d, ", next_frame_start);
    the = read_quartet (avifile);

    next_frame_start = next_frame_start + 4;
    avifile.seek(  next_frame_start , SeekSet);
    Serial.printf("addr %d, ", next_frame_start);
    the = read_quartet (avifile);



    next_frame_start = 240;


    bool one_more_frame = true;

    //for (int frame_num = 0; frame_num < frame_cnt; frame_num = frame_num + 1) {

    size_t start_index_here;

    while (one_more_frame) {

      //i avifile.seek( index_start + 244 + frame_num * 16 + 8 , SeekSet);
      //i index_frame_start = read_quartet( avifile);
      //i index_frame_length = read_quartet( avifile);

      //avifile.seek( next_frame_start  , SeekSet);   // start at 240, then read everything - 2 quart + frame, then repeat

      avifile.seek( next_frame_start  , SeekSet);
      start_index_here = avifile.position();
      int the_oodc = read_quartet (avifile);   //240
      if (the_oodc == 1667510320) {
        //Serial.printf("%d, good frame, num %d\n",the_oodc,frame_num);
      } else {
        Serial.printf("%d, bad frame, num %d\n", the_oodc, frame_num);
        break;
      }




      //avifile.seek( next_frame_start + 4  , SeekSet);
      new_frame_length = read_quartet( avifile); //244

      index_frame_length = new_frame_length; // reuse the variable

      prev_frame_start = next_frame_start;
      index_frame_start = prev_frame_start; // reuse

      next_frame_start = prev_frame_start + new_frame_length + 8;

      //prev_frame_length = new_frame_length;

      if (frame_num < 5 || frame_num % 500 == 0) {
        Serial.printf("Frame %4d, index len %9d, frame len %9d, index start %9d, frame start %9d\n", frame_num, index_frame_length, new_frame_length, index_frame_start + 236 , prev_frame_start);
      }

      if (frame_num < 5 || frame_cnt_out % 100 == 0) {
        Serial.printf("Frame %4d, index len %9d, frame len %9d, index start %9d, frame start %9d\n", frame_num, index_frame_length, new_frame_length, index_frame_start + 236 , prev_frame_start);
        Serial.printf("in %d, out %d\n", frame_num, frame_cnt_out);
      }

      //avifile.seek( index_frame_start + 244 , SeekSet); // already 248

      remnant = (4 - (index_frame_length & 0x00000003)) & 0x00000003;
      int index_frame_length_rem = index_frame_length + remnant;

      int left_to_write = index_frame_length_rem;

      // print_dc_quartet( index_frame_length_rem, outfile);
      /*
            while (left_to_write > 0) {
              if (left_to_write > fbs * 1024) {
                size_t err = avifile.read(fb_faf_static, fbs * 1024);
                size_t err2 = outfile.write(fb_faf_static, fbs * 1024);
                left_to_write = left_to_write - fbs * 1024;
              } else {
                size_t err = avifile.read(fb_faf_static, left_to_write);
                size_t err2 = outfile.write(fb_faf_static, left_to_write);
                left_to_write = 0;
              }
            }
      */
      movi_size += index_frame_length;
      movi_size += remnant;

      print_2quartet(idx_offset, index_frame_length, idxfile);

      idx_offset = idx_offset + index_frame_length_rem + 8;

      frame_cnt_out++;
      frame_num++;

    }  // every frame in file

    //frame_cnt_out--;
    //movi_size = movi_size - remnant;
    //movi_size = movi_size - index_frame_length;

    //Serial.printf("frame %4d, outfile %9d, avifile %9d, idxfile %9d\n", frame_cnt , outfile.position(), avifile.position(), avifile.position());



    idxfile.close();
    //avifile.close();
    //avifile = SD_MMC.open(avi_file_name, "w");

    avifile.seek(  start_index_here  , SeekSet);

    //size_t i1_err = outfile.write(idx1_buf, 4);
    //if (!i1_err) Serial.printf("idx write\n");

    //print_quartet(frame_cnt_out * 16, outfile);

    idxfile = SD_MMC.open(idx_file_name, "r");
    if (idxfile)  {
      Serial.printf("File open: %s\n", idx_file_name);
    }  else  {
      Serial.printf("Could not open file %s\n", idx_file_name);
    }

    char * AteBytes;
    AteBytes = (char*) malloc (8);


    for (int i = 0; i < frame_cnt_out; i++) {
      size_t res = idxfile.readBytes( AteBytes, 8);
      if (!res) Serial.printf("idx read\n");
      size_t i1_err = avifile.write(dc_and_zero_buf, 8);
      if (!i1_err) Serial.printf("dc write\n");
      //size_t i2_err = outfile.write(zero_buf, 4);
      //if (!i2_err) Serial.printf("zero write\n");
      size_t i3_err = avifile.write((uint8_t *)AteBytes, 8);
      if (!i3_err) Serial.printf("ate write\n");
    }

    free(AteBytes);

    avifile.seek( 4 , SeekSet);         //shit
    print_quartet(movi_size + 240 + 16 * frame_cnt_out + 8 * frame_cnt_out, avifile);

    //avifile.seek( 0xe8 , SeekSet);
    //int lom = read_quartet( avifile);
    //Serial.printf("Len of movi was %d, now is %d\n", lom, movi_size);

    avifile.seek( 0xe8 , SeekSet);
    // shit print_quartet (movi_size, outfile);
    print_quartet(movi_size + frame_cnt_out * 8 + 4, avifile);

    //avifile.seek( 0x30 , SeekSet);
    //int fc = read_quartet( avifile);
    //Serial.printf("Frames was %d, now is %d\n", fc, frame_cnt_out);

    avifile.seek( 0x30 , SeekSet);
    print_quartet (frame_cnt_out, avifile);
    avifile.seek( 0x8c , SeekSet);
    print_quartet (frame_cnt_out, avifile);

    //avifile.seek( 0x84 , SeekSet);
    //int fps = read_quartet( avifile);
    //Serial.printf("fps was %d\n", fps);

    //float fnewfps = ( 1.0f * fps ) / (1 + skip_frames)   ;
    //int newfps = round(fnewfps);

    //Serial.printf("newfps is %f, %d\n", fnewfps, newfps);
    //avifile.seek( 0x84 , SeekSet);
    //print_quartet(newfps, avifile);

    //avifile.seek( 0x20 , SeekSet);
    //int us_per_frame = read_quartet( avifile);
    //Serial.printf("us_per_frame was %d\n", us_per_frame);

    //float newus = 1000000.0f / fnewfps;
    //uint32_t new_us_per_frame = round (newus);


    //Serial.printf("new_us_per_frame is %f, %d\n", newus, new_us_per_frame);
    //avifile.seek( 0x20 , SeekSet);
    //print_quartet(new_us_per_frame, avifile);


    idxfile.close();
    avifile.close();
    //outfile.close();

    int xx = SD_MMC.remove(idx_file_name);
  }
}


//61.3up
static esp_err_t status_handler(httpd_req_t *req) {
  esp_err_t res = ESP_OK;
  print_mem("status_handler");

  delay(101);

  int remain = (-millis() + (avi_start_time + avi_length * 1000) ) / 1000;
  //Serial.printf("remain %d\n", remain);

  String x = " {\"OnOff\":\"";

  if (start_record == 1) {
    if (frame_interval == 1000) {
      x = x + "TL";
    } else {
      x = x + "On";
    }
  } else {
    x = x + "Off";
  }
  x = x + "\",\"File\":\"";

  int fnl = strlen(avi_file_name);
  //Serial.printf("fnl %d \n", fnl);

  String fn(avi_file_name);
  //Serial.println(fn);

  x = x + fn + "\", \"Remain\": ";
  x = x + String(remain) ;

  int total =  SD_MMC.totalBytes() / (1024 * 1024);
  int used =  SD_MMC.usedBytes() / (1024 * 1024) ;
  int freesp = total - used;

  x = x + ",\"Size\":" + String(total);
  x = x + ",\"Free\":" + String(freesp);
  if (0) { //                                     if (no_wifi) {
    x = x + ",\"rssi\":" + String(0);
  } else {
    x = x + ",\"rssi\":" + String(WiFi.RSSI());
  }

  x = x + ",\"IP\":" + "\"" + String(localip) + "\"" ;

  x = x + ",\"file_to_edit\":" + "\"" + String(file_to_edit) + "\"" ;

  x = x + ",\"Power\":" + String(99) + "}";  // x = x + ",\"Power\":" + String(power) + "}";

  const char* str = x.c_str();

  httpd_resp_send(req, str,  strlen(str));
  return res;
}

static esp_err_t find_handler(httpd_req_t *req) {
  esp_err_t res = ESP_OK;
  char  buf[120];
  size_t buf_len;
  char  new_res[20];

  oneframe x;

  size_t _jpg_buf_len = 0;
  uint8_t * _jpg_buf = NULL;
  char * part_buf[64];
  int frame_pct;
  char filename[50];

  print_mem("find_handler");

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
    //Serial.printf("Query => %s\n", buf);
    char param[32];
    if (httpd_query_key_value(buf, "f", filename, sizeof(filename)) == ESP_OK) {
      //Serial.printf( "Found URL query parameter => f=>%s<\n", filename);
    }
    if (httpd_query_key_value(buf, "n", param, sizeof(param)) == ESP_OK) {
      int nn = atoi(param);
      if (nn >= 0 && nn <= 30000 ) {
        frame_pct = nn;
        //Serial.printf( "Found URL query parameter => n=%d\n", frame_pct);
      }
    }
  }

  //uint8_t* the_frame = find_a_frame ( "/JamCam0090.0001.avi", 12);

  x = find_a_frame ( filename, frame_pct);
  //the_frame = x.the_frame;

  _jpg_buf_len = x.the_frame_length;
  _jpg_buf = x.the_frame;


  if (x.the_frame == NULL) {
    Serial.printf("no frame\n");
    res = httpd_resp_send_408(req);
    //61.3 httpd_resp_send(req, page_html, strlen(page_html));
  } else {

    res = httpd_resp_set_type(req, "image/jpeg");
    if (res != ESP_OK) {
      return res;
    }

    if (res == ESP_OK) {
      char fname[50];
      char frame_num_char[8];
      char frame_pct_char[8];
      char frame_total_char[8];

      sprintf(fname, "inline; filename=frame_%d.jpg", frame_pct);
      sprintf(frame_num_char, "%d", x.the_frame_number);
      sprintf(frame_total_char, "%d", x.the_frame_total - 1); //61.4

      sprintf(frame_pct_char, "%d", frame_pct);

      httpd_resp_set_hdr(req, "Content-Disposition", fname);
      httpd_resp_set_hdr(req, "FrameNum", frame_num_char);
      httpd_resp_set_hdr(req, "Total", frame_total_char);
      httpd_resp_set_hdr(req, "FramePct", frame_pct_char);
      httpd_resp_set_hdr(req, "File", filename);


    }
    if (res == ESP_OK) {
      res = httpd_resp_send(req, (const char *)_jpg_buf, _jpg_buf_len);
    }

    free (x.the_frame);
  }
  return res;;
}
static esp_err_t edit_handler(httpd_req_t *req) {
  esp_err_t res = ESP_OK;
  char  buf[120];
  size_t buf_len;
  char  new_res[20];

  print_mem("edit_handler");

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
    Serial.printf("Query => %s\n", buf);
    char param[32];

    if (httpd_query_key_value(buf, "f", file_to_edit, sizeof(file_to_edit)) == ESP_OK) {
      Serial.printf( "Found URL query parameter => f=>%s<\n", file_to_edit);

    }
  }

  httpd_resp_send(req, edit_html, strlen(edit_html));

  return res;;
}

static esp_err_t ota_handler(httpd_req_t *req) {
  esp_err_t res = ESP_OK;

  print_mem("ota_handler");

  delay(100);

  start_record = 0;
  web_stop = true;

  ///  ota updates always enabled without password at either softap ip or router ip
  ArduinoOTA.setHostname(ssidota);
  ArduinoOTA.setPassword("mrpeanut");
  ArduinoOTA
  .onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()

    Serial.println("\n\nStop Recording due to OTA ! \n\n" );
    start_record = 0;
    web_stop = true;
    delay(500);
    Serial.println("Start updating " + type);
  })
  .onEnd([]() {
    Serial.println("\nEnd");
  })
  .onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  })
  .onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();

  do_the_ota = true;

  long start = millis();

  Serial.printf("Do the ota %d\n");

  const char the_message[] = "Status";

  time(&now);
  const char *strdate = ctime(&now);

  const char msg[] PROGMEM = R"rawliteral(<!doctype html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%s ESP32-CAM Video Recorder Junior</title>
</head>
<body>
<h1>%s<br>ESP32-CAM Video Recorder Junior %s <br><font color="red">%s</font></h1><br>
 <br>
Do the ota, or reboot ...
 <br>
<br>
</body>
</html>)rawliteral";

  sprintf(the_page, msg, devname, devname, vernum, strdate );

  httpd_resp_send(req, the_page, strlen(the_page));
  time_in_web1 += (millis() - start);

  return ESP_OK;
}

/*static esp_err_t delete_handler(httpd_req_t *req) {
  esp_err_t res = ESP_OK;

  Serial.print("delete_handler, core ");  Serial.print(xPortGetCoreID());
  Serial.print(", priority = "); Serial.println(uxTaskPriorityGet(NULL));


  httpd_resp_send(req, page_html, strlen(page_html));
  delay(100);
  delete_all_files = 1;
  return res;;
  }
*/
//61.3 up
////////////////////////////////

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 17; //61.3 from 12
  config.stack_size = 4096 + 1024 + 1024 + 1024;
  config.lru_purge_enable = true;
  //61 config.enable_so_linger = true;
  //61 config.linger_timeout = 1;
  //61 config.keep_alive_enable = true;
  //config.enable_so_linger = true;
  //61 config.max_open_sockets   = 10;
  //61 config.backlog_conn       = 10; //from def of 5
  //61 config.core_id = 0; // from tskNO_AFFINITY

  Serial.print("http task prio: "); Serial.println(config.task_priority);

  httpd_uri_t index_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = index_handler,
    .user_ctx  = NULL
  };
  httpd_uri_t capture_uri = {
    .uri       = "/capture",
    .method    = HTTP_GET,
    .handler   = capture_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t photos_uri = {
    .uri       = "/photos",
    .method    = HTTP_GET,
    .handler   = photos_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t fphotos_uri = {
    .uri       = "/fphotos",
    .method    = HTTP_GET,
    .handler   = fphotos_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t sphotos_uri = {
    .uri       = "/sphotos",
    .method    = HTTP_GET,
    .handler   = sphotos_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t reboot_uri = {
    .uri       = "/reboot",
    .method    = HTTP_GET,
    .handler   = reboot_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t restart_uri = {
    .uri       = "/restart",
    .method    = HTTP_GET,
    .handler   = restart_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t time_uri = {
    .uri       = "/time",
    .method    = HTTP_GET,
    .handler   = time_handler,
    .user_ctx  = NULL
  };

  httpd_uri_t start_uri = {
    .uri       = "/start",
    .method    = HTTP_GET,
    .handler   = start_handler,
    .user_ctx  = NULL
  };
  httpd_uri_t stop_uri = {
    .uri       = "/stop",
    .method    = HTTP_GET,
    .handler   = stop_handler,
    .user_ctx  = NULL
  };
  httpd_uri_t find_uri = {
    .uri       = "/find",
    .method    = HTTP_GET,
    .handler   = find_handler,
    .user_ctx  = NULL
  };
  httpd_uri_t status_uri = {
    .uri       = "/status",
    .method    = HTTP_GET,
    .handler   = status_handler,
    .user_ctx  = NULL
  };
  /*61.3
     httpd_uri_t delete_uri = {
      .uri       = "/delete",
      .method    = HTTP_GET,
      .handler   = delete_handler,
      .user_ctx  = NULL
    };
    61.3 */
  httpd_uri_t edit_uri = {
    .uri       = "/edit",
    .method    = HTTP_GET,
    .handler   = edit_handler,
    .user_ctx  = NULL
  };
  httpd_uri_t reindex_uri = {
    .uri       = "/reindex",
    .method    = HTTP_GET,
    .handler   = reindex_handler,
    .user_ctx  = NULL
  };
  httpd_uri_t ota_uri = {
    .uri       = "/ota",
    .method    = HTTP_GET,
    .handler   = ota_handler,
    .user_ctx  = NULL
  };
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &photos_uri);
    httpd_register_uri_handler(camera_httpd, &fphotos_uri);
    httpd_register_uri_handler(camera_httpd, &sphotos_uri);
    httpd_register_uri_handler(camera_httpd, &reboot_uri);
    httpd_register_uri_handler(camera_httpd, &restart_uri);
    httpd_register_uri_handler(camera_httpd, &time_uri);
    httpd_register_uri_handler(camera_httpd, &start_uri);
    httpd_register_uri_handler(camera_httpd, &stop_uri);
    httpd_register_uri_handler(camera_httpd, &edit_uri); //61.3 index->camera
    httpd_register_uri_handler(camera_httpd, &find_uri);
    httpd_register_uri_handler(camera_httpd, &status_uri);
    httpd_register_uri_handler(camera_httpd, &reindex_uri);
    httpd_register_uri_handler(camera_httpd, &ota_uri);
  }

  Serial.println("Camera http started");
}

void stopCameraServer() {
  httpd_stop(camera_httpd);
}

void the_camera_loop (void* pvParameter);
void the_sd_loop (void* pvParameter);
void delete_old_stuff();


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void setup() {

  Serial.begin(115200);
  Serial.println("\n\n---");

  pinMode(33, OUTPUT);             // little red led on back of chip
  digitalWrite(33, LOW);           // turn on the red LED on the back of chip

  pinMode(4, OUTPUT);               // Blinding Disk-Avtive Light
  digitalWrite(4, LOW);             // turn off

  pinMode(12, INPUT_PULLUP);        // pull this down to stop recording
  pinMode(13, INPUT_PULLUP);        // pull this down switch wifi

  //Serial.setDebugOutput(true);


  // SD camera init
  Serial.println("Mounting the SD card ...");
  esp_err_t card_err = init_sdcard();
  if (card_err != ESP_OK) {
    Serial.printf("SD Card init failed with error 0x%x", card_err);
    major_fail();
    return;
  } else {
    logfile = SD_MMC.open("/boot.txt", FILE_WRITE);
  }

  jprln("                                    ");
  jprln("---------------------------------------");
  jprln("ESP32-CAM-Video-Recorder-junior %s", vernum);
  jprln("---------------------------------------");

  print_mem("setup");

  esp_reset_reason_t reason = esp_reset_reason();

  //logfile.print("--- reboot ------ because: ");
  jpr("--- reboot ------ because: ");

  switch (reason) {
    case ESP_RST_UNKNOWN : jprln("ESP_RST_UNKNOWN");  break;
    case ESP_RST_POWERON : jprln("ESP_RST_POWERON"); break;
    case ESP_RST_EXT : jprln("ESP_RST_EXT");  break;
    case ESP_RST_SW : jprln("ESP_RST_SW");  break;
    case ESP_RST_PANIC : jprln("ESP_RST_PANIC");  break;
    case ESP_RST_INT_WDT : jprln("ESP_RST_INT_WDT");  break;
    case ESP_RST_TASK_WDT : jprln("ESP_RST_TASK_WDT");  break;
    case ESP_RST_WDT : jprln("ESP_RST_WDT");  break;
    case ESP_RST_DEEPSLEEP : jprln("ESP_RST_DEEPSLEEP");  break;
    case ESP_RST_BROWNOUT : jprln("ESP_RST_BROWNOUT");  break;
    case ESP_RST_SDIO : jprln("ESP_RST_SDIO");  break;
    default  : jprln("Reset resaon"); break;
  }

  do_eprom_read();

  jprln("Try to get parameters from config2.txt ...");

  read_config_file();

  jprln("Setting up the camera ...");
  config_camera();

  //fb_record = (uint8_t*)ps_malloc(512 * 1024); // buffer to store a jpg in motion // needs to be larger for big frames from ov5640

  // frame_buffer_size set by config_camera
  fb_record = (uint8_t*)ps_malloc(frame_buffer_size); // buffer to store a jpg in motion // needs to be larger for big frames from ov5640
  fb_curr_record_buf = (uint8_t*)ps_malloc(frame_buffer_size);
  fb_streaming = (uint8_t*)ps_malloc(frame_buffer_size); // buffer to store a jpg in motion // needs to be larger for big frames from ov5640
  fb_capture = (uint8_t*)ps_malloc(frame_buffer_size); // buffer to store a jpg in motion // needs to be larger for big frames from ov5640

  print_mem("setup - after malloc");

  jprln("Creating the_camera_loop_task");

  baton = xSemaphoreCreateMutex();


  xTaskCreatePinnedToCore( the_camera_loop, "the_camera_loop", 5000, NULL, 4, &the_camera_loop_task, 0); //soc14
  delay(100);

  xTaskCreate( the_streaming_loop, "the_streaming_loop", 8000, NULL, 2, &the_streaming_loop_task);
  if ( the_streaming_loop_task == NULL ) {
    //vTaskDelete( xHandle );
    Serial.printf("do_the_steaming_task failed to start! %d\n", the_streaming_loop_task);
  }


  if (InternetOff) {
    print_mem("Starting the wifi ...");
    init_wifi();
    print_mem("Starting the fileman ...");

    jprln("");
    filemgr.begin();
    filemgr.setBackGroundColor("Gray");
    jpr("Open Filemanager with http://");
    Serial.print(WiFi.softAPIP()); logfile.print(WiFi.softAPIP());
    jprln(":%d/", filemanagerport);
    jpr("Open Filemanager with http://");
    Serial.print(WiFi.localIP()); logfile.print(WiFi.localIP());
    jprln(":%d/", filemanagerport);

    print_mem("Starting Web Services ...");
    startCameraServer();
    start_Stream_81_server();
    start_Stream_82_server();

    InternetOff = false;
    print_mem("After the WiFi");
  }

  jprln("Checking SD for available space ...");
  delete_old_stuff();

  char logname[60];
  char the_directory[50];

  sprintf(the_directory, "/%s%03d",  devname, file_group);
  SD_MMC.mkdir(the_directory);

  sprintf(logname, "/%s%03d/%s%03d.999.txt",  devname, file_group, devname, file_group);
  jprln("Creating logfile %s\n",  logname);
  if (logfile) {
    logfile.close();
  }
  logfile = SD_MMC.open(logname, FILE_WRITE);
  if (!logfile) {
    Serial.println("Failed to open logfile for writing");
  }

  boot_time = millis();

  const char *strdate = ctime(&now);
  //logfile.println(strdate);

  digitalWrite(33, HIGH);         // red light turns off when setup is complete

  print_mem("End of setup");
  jpr("\n---  End of setup()  ---\n\n");

}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// the_sd_loop()
//
/* //8
  void the_sd_loop (void* pvParameter) {

  Serial.print("the_sd_loop, core ");  Serial.print(xPortGetCoreID());
  Serial.print(", priority = "); Serial.println(uxTaskPriorityGet(NULL));

  while (1) {
    xSemaphoreTake( sd_go, portMAX_DELAY );            // we wait for camera loop to tell us to go
    another_save_avi( fb_curr);                        // do the actual sd wrte
    xSemaphoreGive( wait_for_sd );                     // tell camera loop we are done
  }
  }
*/
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// the_camera_loop()
int delete_old_stuff_flag = 0;

void the_camera_loop (void* pvParameter) {

  print_mem("the_camera_loop");

  frame_cnt = 0;
  start_record_2nd_opinion = digitalRead(12);
  start_record_1st_opinion = digitalRead(12);
  start_record = 0;

  delay(1000);

  while (1) {
    delay(1);

    // if (frame_cnt == 0 && start_record == 0)  // do nothing
    // if (frame_cnt == 0 && start_record == 1)  // start a movie
    // if (frame_cnt > 0 && start_record == 0)   // stop the movie
    // if (frame_cnt > 0 && start_record != 0)   // another frame

    ///////////////////  NOTHING TO DO //////////////////
    if ( (frame_cnt == 0 && start_record == 0)) {

      // Serial.println("Do nothing");
      if (we_are_already_stopped == 0) jpr("\n\nDisconnect Pin 12 from GND to start recording or http://192.168.1.100/start \n\n");
      we_are_already_stopped = 1;
      delay(100);

      ///////////////////  START A MOVIE  //////////////////
    } else if (frame_cnt == 0 && start_record == 1) {

      //Serial.println("Ready to start");

      we_are_already_stopped = 0;

      avi_start_time = millis();

      jpr("\nStart the avi ... at %d\n", avi_start_time);
      jpr("Framesize %d, quality %d, length %d seconds\n\n", framesize, quality, avi_length);
      logfile.flush();

      //88 frame_cnt++;

      long wait_for_cam_start = millis();
      wait_for_cam += millis() - wait_for_cam_start;

      start_avi();

      wait_for_cam_start = millis();

      ///
      frame_cnt++;

      long delay_wait_for_sd_start = millis();

      delay_wait_for_sd += millis() - delay_wait_for_sd_start;

      fb_curr = get_good_jpeg();    //7

      fb_curr_record_len = fb_curr->len;
      memcpy(fb_curr_record_buf, fb_curr->buf, fb_curr->len);
      fb_curr_record_time = millis();

      xSemaphoreTake( baton, portMAX_DELAY );

      fb_record_len = fb_curr_record_len;
      memcpy(fb_record, fb_curr_record_buf, fb_curr_record_len);   // v59.5
      fb_record_time = fb_curr_record_time;
      xSemaphoreGive( baton );

      esp_camera_fb_return(fb_curr);  //7

      another_save_avi( fb_curr_record_buf, fb_curr_record_len );

      ///
      wait_for_cam += millis() - wait_for_cam_start;
      if (blinking) digitalWrite(33, frame_cnt % 2);                // blink

      ///////////////////  END THE MOVIE //////////////////
    } else if ( restart_now || reboot_now || (frame_cnt > 0 && start_record == 0) ||  millis() > (avi_start_time + avi_length * 1000)) { // end the avi

      jpr("End the Avi");
      restart_now = false;

      if (blinking)  digitalWrite(33, frame_cnt % 2);

      end_avi();                                // end the movie

      if (blinking) digitalWrite(33, HIGH);          // light off

      delete_old_stuff_flag = 1;
      delay(50);

      avi_end_time = millis();

      float fps = 1.0 * frame_cnt / ((avi_end_time - avi_start_time) / 1000) ;

      jpr("End the avi at %d.  It was %d frames, %d ms at %.2f fps...\n", millis(), frame_cnt, avi_end_time, avi_end_time - avi_start_time, fps);

      if (!reboot_now) frame_cnt = 0;             // start recording again on the next loop

      ///////////////////  ANOTHER FRAME  //////////////////
    } else if (frame_cnt > 0 && start_record != 0) {  // another frame of the avi

      //Serial.println("Another frame");

      current_frame_time = millis();
      if (current_frame_time - last_frame_time < frame_interval) {
        delay(frame_interval - (current_frame_time - last_frame_time));             // delay for timelapse
      }
      last_frame_time = millis();

      frame_cnt++;

      long delay_wait_for_sd_start = millis();
      delay_wait_for_sd += millis() - delay_wait_for_sd_start;

      fb_curr = get_good_jpeg();    //7

      fb_curr_record_len = fb_curr->len;
      memcpy(fb_curr_record_buf, fb_curr->buf, fb_curr->len);
      fb_curr_record_time = millis();

      xSemaphoreTake( baton, portMAX_DELAY );

      fb_record_len = fb_curr_record_len;
      memcpy(fb_record, fb_curr_record_buf, fb_curr_record_len);   // v59.5
      fb_record_time = fb_curr_record_time;
      xSemaphoreGive( baton );

      esp_camera_fb_return(fb_curr);  //7

      another_save_avi( fb_curr_record_buf, fb_curr_record_len );

      long wait_for_cam_start = millis();

      wait_for_cam += millis() - wait_for_cam_start;

      if (blinking) digitalWrite(33, frame_cnt % 2);

      if (frame_cnt % 100 == 10 ) {     // print some status every 100 frames
        if (frame_cnt == 10) {
          bytes_before_last_100_frames = movi_size;
          time_before_last_100_frames = millis();
          most_recent_fps = 0;
          most_recent_avg_framesize = 0;
        } else {

          most_recent_fps = 100.0 / ((millis() - time_before_last_100_frames) / 1000.0) ;
          most_recent_avg_framesize = (movi_size - bytes_before_last_100_frames) / 100;

          if ( (Lots_of_Stats && frame_cnt < 1011) || (Lots_of_Stats && frame_cnt % 1000 == 10)) {
            jpr("So far: %04d frames, in %6.1f seconds, for last 100 frames: avg frame size %6.1f kb, %.2f fps ...\n", frame_cnt, 0.001 * (millis() - avi_start_time), 1.0 / 1024  * most_recent_avg_framesize, most_recent_fps);
          }

          total_delay = 0;

          bytes_before_last_100_frames = movi_size;
          time_before_last_100_frames = millis();
        }
      }
    }
  }
}

//


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// loop() - loop runs at low prio, so I had to move it to the task the_camera_loop at higher priority
#include <ESPping.h>
long wakeup;
long last_wakeup = 0;
int loops = 0;
void loop() {
  long run_time = millis() - boot_time;
  loops++;
  if (loops % 10000 == 17) {
    //Serial.printf("loops %10d\n",loops);
  }

  for (int x = 0; x < 1; x++) {
    filemgr.handleClient();  //soc.6
  }

  if (do_the_ota) {
    ArduinoOTA.handle();
  }

  if (delete_old_stuff_flag == 1) {
    delete_old_stuff_flag = 0;
    delete_old_stuff();
  }
  start_record_2nd_opinion = start_record_1st_opinion;
  start_record_1st_opinion = digitalRead(12);

  if (do_the_reindex) {
    done_the_reindex = false;
    do_the_reindex = false;
    re_index ( file_to_read, file_to_write );
    //re_index_bad ( file_to_read );
    done_the_reindex = true;
  }

  wakeup = millis();
  if (wakeup - last_wakeup > (10  * 60 * 1000) ) {
    last_wakeup = millis();
    print_mem("---------- 10 Minute Internet Check -----------\n");
    time(&now);
    jpr("Local time: "); jpr(ctime(&now));
    if (!InternetOff ) {

      esp_err_t client_err;
      struct sockaddr_in *client_list;
      size_t clients = 10;
      size_t client_count = 10;
      int    client_fds[10];

      client_err = httpd_get_client_list(camera_httpd, &client_count, client_fds);
      jpr("camera_httpd Sockets , Num = %d\n", client_count);
      for (size_t i = 0; i < client_count; i++) {
        int sock = client_fds[i];
        int x = httpd_ws_get_fd_info(camera_httpd, sock) ;
        jpr("Socket %d, fd=%d, info=%d \n", i, sock, x);
        print_sock(sock);
      }

      client_err = httpd_get_client_list(stream81_httpd, &client_count, client_fds);
      jpr("stream81_httpd Sockets , Num = %d\n", client_count);
      for (size_t i = 0; i < client_count; i++) {
        int sock = client_fds[i];
        //Serial.printf("%d, sock %d\n", i, sock);
        int x = httpd_ws_get_fd_info(camera_httpd, sock) ;
        jpr("Socket %d, fd=%d, info=%d \n", i, sock, x);
        print_sock(sock);
      }
      client_err = httpd_get_client_list(stream82_httpd, &client_count, client_fds);
      jpr("stream82_httpd Sockets , Num = %d\n", client_count);
      for (size_t i = 0; i < client_count; i++) {
        int sock = client_fds[i];
        //Serial.printf("%d, sock %d\n", i, sock);
        int x = httpd_ws_get_fd_info(camera_httpd, sock) ;
        jpr("Socket %d, fd=%d, info=%d \n", i, sock, x);
        print_sock(sock);
      }

      if (found_router) {
        // Ping local IP
        Serial.println(WiFi.gatewayIP());
        if (Ping.ping(WiFi.gatewayIP()) > 0) {
          jpr(" -- response time : %d/%.2f/%d ms\n", Ping.minTime(), Ping.averageTime(), Ping.maxTime());
        } else {

          jprln("\n\nCannot Ping the gateway - REBOOT");
          jprln("***** WiFi reconnect *****");
          WiFi.reconnect();
          delay(8000);
          if (WiFi.status() != WL_CONNECTED) {
            jprln("***** WiFi restart *****");
            init_wifi();
          }
          delay(15000);
          if (WiFi.status() != WL_CONNECTED) {
            jprln("***** Reboot *****");
            reboot_now = true;
          }

        }
        delay(1000);

        // Ping Host
        const char* remote_host = "google.com";
        jpr(remote_host);
        if (Ping.ping(remote_host) > 0) {
          jpr(" -- response time : %d/%.2f/%d ms\n", Ping.minTime(), Ping.averageTime(), Ping.maxTime());
        } else {
          jprln(" Ping Error !");
        }
        delay(1000);


        if (WiFi.status() != WL_CONNECTED) {

          jprln("***** WiFi reconnect *****");
          WiFi.reconnect();
          delay(8000);

          if (WiFi.status() != WL_CONNECTED) {
            jprln("***** WiFi restart *****");
            init_wifi();
          }
        }
      }

      Serial.println(WiFi.softAPIP());  logfile.println(WiFi.softAPIP());
      Serial.println(WiFi.localIP()); logfile.println(WiFi.localIP());

      if (!MDNS.begin(devname)) {
        jprln("Error setting up MDNS responder!");
      } else {
        jpr("mDNS responder started '%s'\n", devname);
      }
    }  // not internet off
  }  // wakeup

  if (reboot_now == true) {
    jprln(" \n\n\n Rebooting in 5 seconds... \n\n\n");
    delay(5000);
    ESP.restart();
  }

  if (web_stop == true) {
    if (start_record == 1) {
      start_record = 0;
      jprln("web_stop web_stop code");
    }
  } else {
    //jpr("first %d, second %d, web %d\n", start_record_1st_opinion, start_record_2nd_opinion, web_stop);
    if (start_record == 1) {
      if (start_record_1st_opinion == 0 && start_record_2nd_opinion == 0) {
        start_record = 0;
        jprln("stopping in web_stop code");
      }
    } else {
      if (start_record_1st_opinion == 1 && start_record_2nd_opinion == 1) {
        start_record = 1;
        jprln("starting in web_stop code");
      }
    }
  }
}
