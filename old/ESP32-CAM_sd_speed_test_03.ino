/*


  ESP32-CAM_sd_speed_test

  This program tests the speed of the ESP32-CAM SD Writer.
  Take 2 picture per avi, and reuses the 2nd picture for entire movie without running the camera.
  You can set framezie and quality for you situation.
  
  edit changes to
  -  config.frame_size = FRAMESIZE_CIF;  // UXGA, SVGA, CIF, etc
  -  config.jpeg_quality = 1;            // 1 to 63 - smaller number is higher quality and more data

  ... and then the results on the serial monitor, every 100 frames - with avg framesize for last 100.

  Small red led on the back blinks with every frame.

  
  by James Zahary Sep 11, 2020
     jamzah.plc@gmail.com

  https://github.com/jameszah/ESP32-CAM-Video-Recorder

    jameszah/ESP32-CAM-Video-Recorder is licensed under the
    GNU General Public License v3.0

  The is Arduino code, with standard setup for ESP32-CAM
    - Board ESP32 Wrover Module
    - Partition Scheme Huge APP (3MB No OTA)

*/

static const char vernum[] = "v03";
static const char devname[] = "blur";         // name of your camera for mDNS, Router, and filenames

int  framesize = 7;                //  10 UXGA, 7 SVGA, 6 VGA, 5 CIF
int  gray = 0;                     //  not gray
int  quality = 20;                 //  quality on the 1..63 scale  - lower is better quality and bigger files - must be higher than the jpeg_quality in camera_config
int avi_length = 60;               // how long a movie in seconds

//int  xspeed = 1;                   //  playback speed - realtime is 1, or 300 means playpack 30 fps of frames at 10 second per frame ( 30 fps / 0.1 fps )

int MagicNumber = 11;                // change this number to reset the epron in your esp32

int file_number = 0;
int file_group = 0;
long boot_time = 0;

#define BlinkWithWrite 1

long total_bytes_written = 0;
long total_big_writes = 0;
long total_little_writes = 0;

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

camera_fb_t * fb_curr = NULL;
camera_fb_t * fb_next = NULL;

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


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//  Avi Writer Stuff here

// Time
#include "time.h"

struct tm timeinfo;
time_t now;

// MicroSD
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"
#include "sdmmc_cmd.h"
#include "esp_vfs_fat.h"
#include <SD_MMC.h>

FILE *avifile = NULL;
FILE *idxfile = NULL;

int frames_so_far = 0;
long bp;
long ap;
long bw;
long aw;

int diskspeed = 0;
char fname[100];

static int i = 0;
uint8_t temp = 0, temp_last = 0;
unsigned long fileposition = 0;
uint16_t frame_cnt = 0;
uint16_t remnant = 0;
uint32_t length = 0;
uint32_t startms;
uint32_t elapsedms;
uint32_t uVideoLen = 0;
bool is_header = false;
int bad_jpg = 0;
int extend_jpg = 0;
int normal_jpg = 0;


long totalp;
long totalw;
float avgp;
float avgw;

#define BUFFSIZE 512

char str[20];
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

uint8_t  vga_w[2] = {0x80, 0x02}; // 640
uint8_t  vga_h[2] = {0xE0, 0x01}; // 480
uint8_t  cif_w[2] = {0x90, 0x01}; // 400
uint8_t  cif_h[2] = {0x28, 0x01}; // 296
uint8_t svga_w[2] = {0x20, 0x03}; // 800
uint8_t svga_h[2] = {0x58, 0x02}; // 600
uint8_t uxga_w[2] = {0x40, 0x06}; // 1600
uint8_t uxga_h[2] = {0xB0, 0x04}; // 1200


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
  0x76, 0x30, 0x31, 0x20, 0x4C, 0x49, 0x53, 0x54, 0x00, 0x01, 0x0E, 0x00, 0x6D, 0x6F, 0x76, 0x69,
};



//
// Writes an uint32_t in Big Endian at current file position
//
static void inline print_quartet(unsigned long i, FILE * fd)
{
  uint8_t y[4];

  y[0] = i % 0x100;
  y[1] = (i >> 8) % 0x100;
  y[2] = (i >> 16) % 0x100;
  y[3] = (i >> 24) % 0x100;

  size_t i1_err = fwrite(y , 1, 4, fd);
  total_little_writes++;
  total_bytes_written+= 4;
}

//
// Writes 2 uint32_t in Big Endian at current file position
//
static void inline print_2quartet(unsigned long i, unsigned long j, FILE * fd)
{
  uint8_t y[8];

  y[0] = i % 0x100;
  y[1] = (i >> 8) % 0x100;
  y[2] = (i >> 16) % 0x100;
  y[3] = (i >> 24) % 0x100;
  y[4] = j % 0x100;
  y[5] = (j >> 8) % 0x100;
  y[6] = (j >> 16) % 0x100;
  y[7] = (j >> 24) % 0x100;

  size_t i1_err = fwrite(y , 1, 8, fd);
  total_little_writes++;
  total_bytes_written+= 8;
}

//
// if we have no camera, or sd card, then flash rear led on and off to warn the human SOS - SOS
//
void major_fail() {

  Serial.println(" ");

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

  config.frame_size = FRAMESIZE_UXGA; // edit in framesizes below
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

  config.jpeg_quality = 6;  // 1 to 63 - smaller number is higher quality and more data

  config.fb_count = 7;

  // camera init
  cam_err = esp_camera_init(&config);
  if (cam_err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", cam_err);
  }


  sensor_t * ss = esp_camera_sensor_get();
  ss->set_quality(ss, quality);
  ss->set_framesize(ss, (framesize_t)framesize);
  if (gray == 1) {
    ss->set_special_effect(ss, 2);  // 0 regular, 2 grayscale
  } else {
    ss->set_special_effect(ss, 0);  // 0 regular, 2 grayscale
  }
  ss->set_brightness(ss, 1);  //up the blightness just a bit
  ss->set_saturation(ss, -2); //lower the saturation

  delay(500);
  for (int j = 0; j < 5; j++) {
    camera_fb_t * fb = esp_camera_fb_get();
    //Serial.print("Pic, len="); Serial.println(fb->len);
    esp_camera_fb_return(fb);
    delay(50);
  }

}

static esp_err_t init_sdcard()
{

  //pinMode(12, PULLUP);
  pinMode(13, PULLUP);
  //pinMode(4, OUTPUT);

  esp_err_t ret = ESP_FAIL;
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.flags = SDMMC_HOST_FLAG_1BIT;                       // using 1 bit mode
  host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;
  diskspeed = host.max_freq_khz;
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  slot_config.width = 1;                                   // using 1 bit mode
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = 8,
  };

  sdmmc_card_t *card;

  ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

  if (ret == ESP_OK) {
    Serial.println("SD card mount successfully!");
  }  else  {
    Serial.printf("Failed to mount SD card VFAT filesystem. Error: %s", esp_err_to_name(ret));
    Serial.println("Try again...");
    delay(5000);
    diskspeed = 400;
    host.max_freq_khz = SDMMC_FREQ_PROBING;
    ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret == ESP_OK) {
      Serial.println("SD card mount successfully SLOW SLOW SLOW");
    } else {
      Serial.printf("Failed to mount SD card VFAT filesystem. Error: %s", esp_err_to_name(ret));
      major_fail();
    }
  }
  sdmmc_card_print_info(stdout, card);
  Serial.print("SD_MMC Begin: "); Serial.println(SD_MMC.begin());   // required by ftp system ??

  //pinMode(13, PULLDOWN);
  //pinMode(13, INPUT_PULLDOWN);
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
//  get_good_jpeg()  - take a picture and make sure it has a good jpeg
//
camera_fb_t *  get_good_jpeg() {
  
  camera_fb_t * fb;
  
  do {
    fb = esp_camera_fb_get();
    int x = fb->len;
    int foundffd9 = 0;

    for (int j = 1; j <= 1025; j++) {
      if (fb->buf[x - j] != 0xD9) {
                             // no d9, try next for
      } else {

        //Serial.println("Found a D9");
        if (fb->buf[x - j - 1] == 0xFF ) {
          //Serial.print("Found the FFD9, junk is "); Serial.println(j);
          if (j == 1) {
            normal_jpg++;
          } else {
            extend_jpg++;
          }
          if (j > 1000) { //  never happens. but > 1 does, usually 400-500
            Serial.print("Frame "); Serial.print(frames_so_far);
            Serial.print(", Len = "); Serial.print(x);
            Serial.print(", Corrent Len = "); Serial.print(x - j + 1);
            Serial.print(", Extra Bytes = "); Serial.println( j - 1);
          }
          foundffd9 = 1;
          break;
        }
      }
    }

    if (!foundffd9) {
      bad_jpg++;
      Serial.print("Bad jpeg, Len = "); Serial.println(x);
      esp_camera_fb_return(fb);

    } else {
      break;
      // count up the useless bytes
    }

  } while (1);
  
  return fb;

}

#include <EEPROM.h>

struct eprom_data {
  int eprom_good;
  int file_group;
};

void do_eprom_read() {

  eprom_data ed;

  long x = millis();
  EEPROM.begin(200);
  EEPROM.get(0, ed);
  Serial.println("Get took " + String(millis() - x));

  if (ed.eprom_good == MagicNumber) {
    Serial.println("Good settings in the EPROM ");
    file_group = ed.file_group;
    file_group++;
    Serial.print("New File Group "); Serial.println(file_group );
  } else {
    Serial.println("No settings in EPROM - Starting with File Group 1 ");
    file_group = 1;
  }
  do_eprom_write();
  file_number = 1;
}


void do_eprom_write() {

  eprom_data ed;

  Serial.println("Write settings in the EPROM ");
  ed.eprom_good = MagicNumber;
  ed.file_group  = file_group;
  
  Serial.println("Writing to EPROM ...");

  long x = millis();
  EEPROM.begin(200);
  EEPROM.put(0, ed);
  EEPROM.commit();
  EEPROM.end();

  Serial.println("Put took " + String(millis() - x) + " ms, bytes = " + String(sizeof(ed)));
}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// start_avi - open the files and write in headers
//


static esp_err_t start_avi() {

  Serial.println("Starting an avi ");

  sprintf(fname, "/sdcard/%s %d.%d + %ds.avi",  devname, file_group, file_number, (millis()-boot_time)/1000);
  
  file_number++;

  avifile = fopen(fname, "w");
  idxfile = fopen("/sdcard/idx.tmp", "w");

  if (avifile != NULL)  {
    Serial.printf("File open: %s\n", fname);
  }  else  {
    Serial.println("Could not open file");
    major_fail();
  }

  if (idxfile != NULL)  {
    Serial.printf("File open: %s\n", "/sdcard/idx.tmp");
  }  else  {
    Serial.println("Could not open file");
    major_fail();
  }

  for ( i = 0; i < AVIOFFSET; i++)
  {
    char ch = pgm_read_byte(&avi_header[i]);
    buf[i] = ch;
  }

  size_t err = fwrite(buf, 1, AVIOFFSET, avifile);

    total_big_writes++;
  total_bytes_written+= AVIOFFSET;
  
  if (framesize == 6) {

    fseek(avifile, 0x40, SEEK_SET);
    err = fwrite(vga_w, 1, 2, avifile);
    fseek(avifile, 0xA8, SEEK_SET);
    err = fwrite(vga_w, 1, 2, avifile);
    fseek(avifile, 0x44, SEEK_SET);
    err = fwrite(vga_h, 1, 2, avifile);
    fseek(avifile, 0xAC, SEEK_SET);
    err = fwrite(vga_h, 1, 2, avifile);

  } else if (framesize == 10) {

    fseek(avifile, 0x40, SEEK_SET);
    err = fwrite(uxga_w, 1, 2, avifile);
    fseek(avifile, 0xA8, SEEK_SET);
    err = fwrite(uxga_w, 1, 2, avifile);
    fseek(avifile, 0x44, SEEK_SET);
    err = fwrite(uxga_h, 1, 2, avifile);
    fseek(avifile, 0xAC, SEEK_SET);
    err = fwrite(uxga_h, 1, 2, avifile);

  } else if (framesize == 7) {

    fseek(avifile, 0x40, SEEK_SET);
    err = fwrite(svga_w, 1, 2, avifile);
    fseek(avifile, 0xA8, SEEK_SET);
    err = fwrite(svga_w, 1, 2, avifile);
    fseek(avifile, 0x44, SEEK_SET);
    err = fwrite(svga_h, 1, 2, avifile);
    fseek(avifile, 0xAC, SEEK_SET);
    err = fwrite(svga_h, 1, 2, avifile);

  }  else if (framesize == 5) {

    fseek(avifile, 0x40, SEEK_SET);
    err = fwrite(cif_w, 1, 2, avifile);
    fseek(avifile, 0xA8, SEEK_SET);
    err = fwrite(cif_w, 1, 2, avifile);
    fseek(avifile, 0x44, SEEK_SET);
    err = fwrite(cif_h, 1, 2, avifile);
    fseek(avifile, 0xAC, SEEK_SET);
    err = fwrite(cif_h, 1, 2, avifile);
  }

  total_little_writes+=4;
  total_bytes_written+= 8;
  
  fseek(avifile, AVIOFFSET, SEEK_SET);

  Serial.print(F("\nRecording "));
  Serial.print(avi_length);
  Serial.println(" seconds.");
  
  startms = millis();
 
  totalp = 0;
  totalw = 0;
 
  jpeg_size = 0;
  movi_size = 0;
  uVideoLen = 0;
  idx_offset = 4;

  frame_cnt = 0;
  
  bad_jpg = 0;
  extend_jpg = 0;
  normal_jpg = 0;

} // end of start avi

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
//  another_save_avi runs on cpu 1, saves another frame to the avi file
//
//

static esp_err_t another_save_avi(camera_fb_t * fb ) {

  int fblen;
  fblen = fb->len;

  jpeg_size = fblen;
  movi_size += jpeg_size;
  uVideoLen += jpeg_size;

  bw = millis();
  
  size_t dc_err = fwrite(dc_and_zero_buf, 1, 8, avifile);
  total_little_writes++;
  total_bytes_written+= 8;

  size_t err = fwrite(fb->buf, 1, fb->len, avifile);
  total_big_writes++;
  total_bytes_written+= fb->len;
  if (err != fb->len) {
    Serial.print("Error on avi write: err = "); Serial.print(err);
    Serial.print(" len = "); Serial.println(fb->len);
  }

  remnant = (4 - (jpeg_size & 0x00000003)) & 0x00000003;

  print_2quartet(idx_offset, jpeg_size, idxfile);
  
  idx_offset = idx_offset + jpeg_size + remnant + 8;

  jpeg_size = jpeg_size + remnant;
  movi_size = movi_size + remnant;
  if (remnant > 0) {
    size_t rem_err = fwrite(zero_buf, 1, remnant, avifile);
      total_little_writes++;
  total_bytes_written+= remnant;
  }

  fileposition = ftell (avifile);       // Here, we are at end of chunk (after padding)
  fseek(avifile, fileposition - jpeg_size - 4, SEEK_SET);    // Here we are the the 4-bytes blank placeholder

  print_quartet(jpeg_size, avifile);    // Overwrite placeholder with actual frame size (without padding)

  fileposition = ftell (avifile);

  fseek(avifile, fileposition + jpeg_size  , SEEK_SET);

  totalw = totalw + millis() - bw;

} // end of another_pic_avi

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
//  end_avi writes the index, and closes the files
//

static esp_err_t end_avi() {

  unsigned long current_end = 0;

  current_end = ftell (avifile);

  Serial.println("End of avi - closing the files");

  elapsedms = millis() - startms;
  //float fRealFPS = (1000.0f * (float)frame_cnt) / ((float)elapsedms) * xspeed;
  float fRealFPS = (1000.0f * (float)frame_cnt) / ((float)elapsedms);
  
  float fmicroseconds_per_frame = 1000000.0f / fRealFPS;
  uint8_t iAttainedFPS = round(fRealFPS);
  uint32_t us_per_frame = round(fmicroseconds_per_frame);

  //Modify the MJPEG header from the beginning of the file, overwriting various placeholders

  fseek(avifile, 4 , SEEK_SET);
  print_quartet(movi_size + 240 + 16 * frame_cnt + 8 * frame_cnt, avifile);

  fseek(avifile, 0x20 , SEEK_SET);
  print_quartet(us_per_frame, avifile);

  unsigned long max_bytes_per_sec = movi_size * iAttainedFPS / frame_cnt;

  fseek(avifile, 0x24 , SEEK_SET);
  print_quartet(max_bytes_per_sec, avifile);

  fseek(avifile, 0x30 , SEEK_SET);
  print_quartet(frame_cnt, avifile);

  fseek(avifile, 0x8c , SEEK_SET);
  print_quartet(frame_cnt, avifile);

  fseek(avifile, 0x84 , SEEK_SET);
  print_quartet((int)iAttainedFPS, avifile);

  fseek(avifile, 0xe8 , SEEK_SET);
  print_quartet(movi_size + frame_cnt * 8 + 4, avifile);

  Serial.println(F("\n*** Video recorded and saved ***\n"));
  Serial.print(F("Recorded "));
  Serial.print(elapsedms / 1000);
  Serial.print(F("s in "));
  Serial.print(frame_cnt);
  Serial.print(F(" frames\nFile size is "));
  Serial.print(movi_size + 12 * frame_cnt + 4);
  Serial.print(F(" bytes\nActual FPS is "));
  Serial.print(fRealFPS, 2);
  Serial.print(F("\nMax data rate is "));
  Serial.print(max_bytes_per_sec);
  Serial.print(F(" byte/s\nFrame duration is "));  Serial.print(us_per_frame);  Serial.println(F(" us"));
  Serial.print(F("Average frame length is "));  Serial.print(uVideoLen / frame_cnt);  Serial.println(F(" bytes"));
  Serial.print("Average picture time (ms) "); Serial.println( 1.0 * totalp / frame_cnt);
  Serial.print("Average write time (ms)   "); Serial.println( totalw / frame_cnt );
  Serial.print("Normal jpg % ");  Serial.println( 100.0 * normal_jpg / frame_cnt, 1 );
  Serial.print("Extend jpg % ");  Serial.println( 100.0 * extend_jpg / frame_cnt, 1 );
  Serial.print("Bad    jpg % ");  Serial.println( 100.0 * bad_jpg / frame_cnt, 1 );

 
  Serial.printf("Writng the index, %d frames\n", frame_cnt);
  fseek(avifile, current_end, SEEK_SET);

  fclose(idxfile);

  size_t i1_err = fwrite(idx1_buf, 1, 4, avifile);

  print_quartet(frame_cnt * 16, avifile);

  idxfile = fopen("/sdcard/idx.tmp", "r");

  if (idxfile != NULL)  {
    Serial.printf("File open: %s\n", "/sdcard/idx.tmp");
  }  else  {
    Serial.println("Could not open index file");
    major_fail();
  }

  char * AteBytes;
  AteBytes = (char*) malloc (8);

  for (int i = 0; i < frame_cnt; i++) {
    size_t res = fread ( AteBytes, 1, 8, idxfile);
    size_t i1_err = fwrite(dc_buf, 1, 4, avifile);
    size_t i2_err = fwrite(zero_buf, 1, 4, avifile);
    size_t i3_err = fwrite(AteBytes, 1, 8, avifile);
      total_little_writes+= 3;
  total_bytes_written+= 16;
  }

  free(AteBytes);
  fclose(idxfile);
  fclose(avifile);
  int xx = remove("/sdcard/idx.tmp");

  Serial.println("---");

}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n---");

  pinMode(33, OUTPUT);             // little red led on back of chip
  digitalWrite(33, LOW);           // turn on the red LED on the back of chip

  pinMode(4, OUTPUT);               // Blinding Disk-Avtive Light
  digitalWrite(4, LOW);             // turn off

  //Serial.setDebugOutput(true);

  // zzz
  Serial.println("                                    ");
  Serial.println("-------------------------------------");
  Serial.printf("ESP32-CAM sd_speed_test %s\n", vernum);
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

  // SD camera init
  esp_err_t card_err = init_sdcard();
  if (card_err != ESP_OK) {
    Serial.printf("SD Card init failed with error 0x%x", card_err);
    major_fail();
    return;
  }

  Serial.printf("Internal Total heap %d, internal Free Heap %d\n", ESP.getHeapSize(), ESP.getFreeHeap());
  Serial.printf("SPIRam Total heap   %d, SPIRam Free Heap   %d\n", ESP.getPsramSize(), ESP.getFreePsram());

  digitalWrite(33, HIGH);         // red light turns off when setup is complete

  Serial.println("Warming up the camera ...");

  delay(100); // give camera warm up time

  for (int i = 0; i < 10; i++) {
    camera_fb_t * fb = esp_camera_fb_get();
    Serial.printf("frame %d, len %d\n", i, fb->len);
    esp_camera_fb_return(fb);
    delay(100);
  }

  do_eprom_read();
  
  Serial.println("  End of setup()\n\n");

  boot_time = millis();
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
int done = 0;
long avi_start_time = 0;
long avi_end_time = 0;

void loop() {
  if (first) {
    Serial.print("the loop, core ");  Serial.print(xPortGetCoreID());
    Serial.print(", priority = "); Serial.println(uxTaskPriorityGet(NULL));
    first = 0;
  }

  frames++;          // avoid the zero divide problem
  frame_cnt = frames;

  if (frames == 1) {                              // start the avi
    
    avi_start_time = millis();
    Serial.printf("Start the avi ... at %d\n", avi_start_time);
    
    fb_curr = get_good_jpeg();                     // should take zero time
    
    start_avi();
    
    fb_next = get_good_jpeg();                    // should take nearly zero time due to time spent writing header
    Serial.printf("\nFramesize is %d, quality %d, at %d bytes, we will use it for entire avi without running the camera!\n\n", framesize, quality, fb_next->len);
    
    another_save_avi( fb_curr);
    
    digitalWrite(33, frames % 2);
    esp_camera_fb_return(fb_curr);

  } else if ( millis() > (avi_start_time + avi_length * 1000)) { // end the avi

    fb_curr = fb_next;
    another_save_avi(fb_curr);
    digitalWrite(33, frames % 2);
    esp_camera_fb_return(fb_curr);

    end_avi();
    
    digitalWrite(33, HIGH);          // light off
    avi_end_time = millis();
    
    float fps = frames / ((avi_end_time - avi_start_time) / 1000) ;
    Serial.printf("End the avi at %d.  It was %d frames, took %d ms at %.1f fps...\n", millis(), frames, avi_end_time, avi_end_time - avi_start_time, fps);

    frames = 0;             // start recording again on the next loop

    total_bytes_written = 0;
    total_big_writes = 0;
    total_little_writes = 0;
    
  } else {  // another frame of the avi
    

    fb_curr = fb_next;           // we will write this frame, and get a new one, which will get the camera going agin
  
    //sd_test don't run the camera //fb_next = get_good_jpeg();    // should take near zero, unless the sd is faster than the camera, when we will have to wait for the camera
    
    another_save_avi(fb_curr);
    
    digitalWrite(33, frames % 2);
    
    //sd_test don't run the camera //esp_camera_fb_return(fb_curr);

    if (frames % 100 == 0 ) {     // print some status every 100 frames
      
      float fps =   1.0 * frames / ((millis() - avi_start_time) / 1000) ;
      float bytes_per_sec = 1.0 * total_bytes_written / ((millis() - avi_start_time) / 1000);
      float writes_per_sec = 1.0 * (total_big_writes + total_little_writes) / ((millis() - avi_start_time) / 1000);
            
      Serial.printf("So far: %d frames, took %d ms at %.2f fps... bytes_per_sec %.2f writes per sec %.2f \n", frames, millis() - avi_start_time, fps, bytes_per_sec, writes_per_sec);
    }
  } 
}
