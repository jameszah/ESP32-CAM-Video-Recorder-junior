// mods by James Zahary Dec 28, 2021 https://github.com/jameszah/ESPxWebFlMgr
//                      Jan 12, 2022 - adds dates/times to display
// based on https://github.com/holgerlembke/ESPxWebFlMgr

#include <Arduino.h>
#include <inttypes.h>
#include "ESPxWebFlMgr.h"
#include "ESPxWebFlMgrWp.h"
#include "ESPxWebFlMgrWpF.h"

#include "CRC32.h"

#include <time.h> //jz


#include <WebServer.h>
#include <FS.h>
#include <SD_MMC.h> //jz #include <SPIFFS.h>

#include <detail/mimetable.h>
String getContentType(const String &path) {
  for (const auto &e : mime::mimeTable) {
    if (path.endsWith(e.endsWith)) return String(e.mimeType);
  }
  return "application/octet-stream";
}

/*
  esp32-arduino 3.11 hides getContenttype
  feb 6, 2025

  #include <detail/RequestHandlersImpl.h>
  String getContentType(const String& path) {
  return StaticRequestHandler::getContentType(path);
  }
*/

//*****************************************************************************************************
ESPxWebFlMgr::ESPxWebFlMgr(word port) {
  _Port = port;
}

//*****************************************************************************************************
ESPxWebFlMgr::~ESPxWebFlMgr() {
  end();
}

//*****************************************************************************************************
void ESPxWebFlMgr::begin() {

  Serial.print("fm begin, core ");  Serial.print(xPortGetCoreID());
  Serial.print(", priority = "); Serial.println(uxTaskPriorityGet(NULL));

  fileManager = new WebServer(_Port);
  Serial.println("start fm bindings");
  //#ifdef fileManagerServerStaticsInternally
  fileManager->on("/", HTTP_GET, std::bind(&ESPxWebFlMgr::fileManagerIndexpage, this));
  fileManager->on("/fm.css", HTTP_GET, std::bind(&ESPxWebFlMgr::fileManagerCSS, this));
  fileManager->on("/fm.js", HTTP_GET, std::bind(&ESPxWebFlMgr::fileManagerJS, this));
  //#endif
  fileManager->on("/bg.css", HTTP_GET, std::bind(&ESPxWebFlMgr::fileManagerGetBackGround, this));

  fileManager->on("/i", HTTP_GET, std::bind(&ESPxWebFlMgr::fileManagerFileListInsert, this));
  fileManager->on("/c", HTTP_GET, std::bind(&ESPxWebFlMgr::fileManagerCommandExecutor, this));
  fileManager->on("/e", HTTP_GET, std::bind(&ESPxWebFlMgr::fileManagerFileEditorInsert, this));
  // file receiver with attached file to form
  fileManager->on("/r", HTTP_POST, std::bind(&ESPxWebFlMgr::fileManagerReceiverOK, this),
                  std::bind(&ESPxWebFlMgr::fileManagerReceiver, this));

  fileManager->onNotFound(std::bind(&ESPxWebFlMgr::fileManagerNotFound, this));

  fileManager->begin();
}

//*****************************************************************************************************
void ESPxWebFlMgr::end() {
  if (fileManager) {
    Serial.print("fm end, core ");  Serial.print(xPortGetCoreID());
    Serial.print(", priority = "); Serial.println(uxTaskPriorityGet(NULL));

    delete fileManager;
    fileManager = NULL;
  }
}

//*****************************************************************************************************
void ESPxWebFlMgr::handleClient() {
  if (fileManager) {
    //Serial.print("h");
    fileManager->handleClient();
  }
}

//*****************************************************************************************************
void ESPxWebFlMgr::setViewSysFiles(bool vsf) {
  _ViewSysFiles = vsf;
}

//*****************************************************************************************************
bool ESPxWebFlMgr::getViewSysFiles(void) {
  return _ViewSysFiles;
}

//*****************************************************************************************************
void ESPxWebFlMgr::setSysFileStartPattern(String sfsp) {
  _SysFileStartPattern = sfsp;
}

//*****************************************************************************************************
String ESPxWebFlMgr::getSysFileStartPattern(void) {
  return _SysFileStartPattern;
}


//*****************************************************************************************************
// privates start here
//*****************************************************************************************************
//*****************************************************************************************************
//*****************************************************************************************************
void ESPxWebFlMgr::fileManagerGetBackGround(void) {
  fileManager->send(200, F("text/css"), ".background {background-color: " + _backgroundColor + ";}");
}

//*****************************************************************************************************
void ESPxWebFlMgr::setBackGroundColor(const String backgroundColor) {
  _backgroundColor = backgroundColor;
}

//*****************************************************************************************************
void ESPxWebFlMgr::fileManagerNotFound(void) {
  String uri = fileManager->uri();

  if (uri == "/") {
    uri = "/fm.html";
  }

  String contentTyp = getContentType(uri);

  if (ESPxWebFlMgr_FileSystem.exists(uri)) {
    File f = ESPxWebFlMgr_FileSystem.open(uri, "r");
    if (f) {
      if (fileManager->streamFile(f, contentTyp) != f.size()) {
        // Serial.println(F("Sent less data than expected!"));
        // We should panic a little bit.
      }
      f.close();
    }
  } else
  {
    fileManager->send(404, F("text/plain"), F("URI not found."));
  }
}

//*****************************************************************************************************
String ESPxWebFlMgr::dispIntDotted(size_t i) {
  String res = "";
  while (i != 0) {
    int r = i % 1000;
    res = String(i % 1000) + res;
    i /= 1000;
    if ( (r < 100) && (i > 0) ) {
      res = "0" + res;
      if (r < 10) {
        res = "0" + res;
      }
    }
    if (i != 0) {
      res = "," + res;  //jz dot to comma ;-)
    }
  }
  return res;
}

//*****************************************************************************************************
size_t ESPxWebFlMgr::totalBytes(void) {
#ifdef ESP8266
  FSInfo info;
  ESPxWebFlMgr_FileSystem.info(info);
  return info.totalBytes;
#endif

#ifdef ESP32
  return (ESPxWebFlMgr_FileSystem.totalBytes() / 1024);
#endif
}

//*****************************************************************************************************
size_t ESPxWebFlMgr::usedBytes(void) {
#ifdef ESP8266
  FSInfo info;
  ESPxWebFlMgr_FileSystem.info(info);
  return info.usedBytes;
#endif

#ifdef ESP32
  return (ESPxWebFlMgr_FileSystem.usedBytes() / 1024);
#endif
}

//*****************************************************************************************************
String ESPxWebFlMgr::dispFileString(size_t fs) {
  if (fs < 0) {
    return "-0";
  }

  if (fs == 0) {
    return "0 kB";
  }

  if (fs < 1000) {
    return String(fs) + " kB";
  }
  // switch from bytes to kilobytes due to 4gb+ sd cards //jz
  //String units[] = { "B", "kB", "MB", "GB", "TB" };
  String units[] = {  "kB", "MB", "GB", "TB" };
  int digitGroups = (int) (log10(fs) / log10(1024));

  //return String(fs / pow(1024, digitGroups)) + " " + units[digitGroups] + " <small>(" + dispIntDotted(fs) + " kB)</small>";
  return String(fs / pow(1024, digitGroups)) + " " + units[digitGroups] ;
}

//*****************************************************************************************************
void ESPxWebFlMgr::fileManagerIndexpage(void) {
  Serial.print("fm index, core ");  Serial.print(xPortGetCoreID());
  Serial.print(", priority = "); Serial.println(uxTaskPriorityGet(NULL));
  fileManager->send(200, F("text/html"), FPSTR(ESPxWebFlMgrWpindexpage));
  delay(1);
}

//*****************************************************************************************************
void ESPxWebFlMgr::fileManagerJS(void) {
  fileManager->send(200, F("text/javascript"), FPSTR(ESPxWebFlMgrWpjavascript));
  delay(1);
}

//*****************************************************************************************************
void ESPxWebFlMgr::fileManagerCSS(void) {
  fileManager->send(200, F("text/css"), FPSTR(ESPxWebFlMgrWpcss));
  delay(1);
}

//*****************************************************************************************************
String ESPxWebFlMgr::CheckFileNameLengthLimit(String fn) {
  // SPIFFS file name limit. Is there a way to get the max length from SPIFFS/LittleFS?
  //                                      SPIFFS_OBJ_NAME_LEN is spiffs.... but not very clean.
  if (fn.length() > 32) {
    int len = fn.length();
    fn.remove(29);
    fn += String(len);
  }

  return fn;
}

//*****************************************************************************************************
String ESPxWebFlMgr::colorline(int i) {
  if (i % 2 == 0) {
    return "ccu";
  } else {
    return "ccg";
  }
}

//*****************************************************************************************************
boolean ESPxWebFlMgr::allowAccessToThisFile(const String filename) {
  return ! filename.startsWith(_SysFileStartPattern);
}


//*****************************************************************************************************
// jz kludge for switching folders on the sd card
String subdir = "/";

//*****************************************************************************************************
//[make FS from esp8266 and esp32 compatible]**********************************************************
// this is the way MS DOS 3.x (?) did it with Int21 findfirst/findnext/findclose
#ifdef ESP8266
File ESPxWebFlMgr::nextFile(Dir &dir) {
  dir.next();
  return dir.openFile("r");
}
File ESPxWebFlMgr::firstFile(Dir &dir) {
  dir = ESPxWebFlMgr_FileSystem.openDir("/");
  return nextFile(dir);
}
#endif
#ifdef ESP32
#define Dir File
File ESPxWebFlMgr::nextFile(Dir &dir) {
  return dir.openNextFile();
}
File ESPxWebFlMgr::firstFile(Dir &dir) {
  dir = ESPxWebFlMgr_FileSystem.open(subdir, "r"); //jz dir = ESPxWebFlMgr_FileSystem.open("/", "r");
  return nextFile(dir);
}
#endif

#include <detail/mimetable.h>
#include <list>
#include <tuple>

//*****************************************************************************************************
void ESPxWebFlMgr::fileManagerFileListInsert(void) {  // must get arg with /i to list that folder

  //  File root = LittleFS.open("/");
  using namespace std;
  using records = tuple<String, String, size_t, time_t, int>;
  list<records> dirList;
  Serial.printf("Internal Total heap %d, internal Free Heap %d, ", ESP.getHeapSize(), ESP.getFreeHeap());
  Serial.printf("SPIRam Total heap   %d, SPIRam Free Heap   %d\n", ESP.getPsramSize(), ESP.getFreePsram());
  int xi = 0;
  Dir xdir;


  if ( (fileManager->args() == 1) && (fileManager->argName(0) == "subdir") ) {
    Serial.print("subdir "); Serial.println(fileManager->arg(0));
    subdir =  fileManager->arg(0);
  }
  else {
    subdir = "/";
  }
  String nsd;
  if (subdir == "/") {
    nsd = "/";
  } else {
    nsd = subdir + "/";
  }

  /*
    File ESPxWebFlMgr::nextFile(Dir &dir) {
    return dir.openNextFile();
    }
    File ESPxWebFlMgr::firstFile(Dir &dir) {
    dir = ESPxWebFlMgr_FileSystem.open(subdir, "r"); //jz dir = ESPxWebFlMgr_FileSystem.open("/", "r");
    return nextFile(dir);
    }
  */
  Serial.printf("Starting the tutples\n");
  File xf = firstFile(xdir);
  fileManager->setContentLength(CONTENT_LENGTH_UNKNOWN);
  fileManager->send(200, F("text/html"), String());

  fileManager->sendContent(F("<div class=\"cc\"><div class=\"gc\">"));


  while (xf) {
    String fn = xf.name();
    //  if (SD_MMC.rmdir("/" + String(val))) {
    if (xf.isDirectory()) {
      if (ESPxWebFlMgr_FileSystem.rmdir(nsd + fn)) {                      // remove empty dir
        Serial.printf("Dir removed\n"); Serial.println(nsd + fn);
      } else {
        //Serial.print("Remove dir failed"); Serial.println(nsd + fn);
        dirList.emplace_back("", xf.name(), xf.size(), xf.getLastWrite(), 1);
        Serial.printf("Added Sub: "); Serial.println(xf.name());
      }

    } else {
      dirList.emplace_back("", xf.name(), xf.size(), xf.getLastWrite(), 0);
      Serial.printf("Added: "); Serial.println(xf.name());
    }

    xf = xdir.openNextFile();
  }
  Serial.println();

  Serial.printf("Done the tutples\n");
  Serial.printf("Internal Total heap %d, internal Free Heap %d, ", ESP.getHeapSize(), ESP.getFreeHeap());
  Serial.printf("SPIRam Total heap   %d, SPIRam Free Heap   %d\n", ESP.getPsramSize(), ESP.getFreePsram());

  dirList.sort([](const records & f, const records & l) {
    return get<3>(f) > get<3>(l);
    return false;
  });

  Serial.printf("Done the sort\n");
  Serial.printf("Internal Total heap %d, internal Free Heap %d, ", ESP.getHeapSize(), ESP.getFreeHeap());
  Serial.printf("SPIRam Total heap   %d, SPIRam Free Heap   %d\n", ESP.getPsramSize(), ESP.getFreePsram());


  while (dirList.size() > 200) {
    auto iter2 = dirList.back();
    String fn = get<1>(iter2);
    if  (!fn.endsWith("2.txt")) {
      Serial.printf("Deleting ... "); Serial.println(fn);
      ESPxWebFlMgr_FileSystem.remove( "/" + fn);
      delay(50);
    }
    dirList.pop_back();
  }

  Serial.printf("Done the shrink\n");
  Serial.printf("Internal Total heap %d, internal Free Heap %d, ", ESP.getHeapSize(), ESP.getFreeHeap());
  Serial.printf("SPIRam Total heap   %d, SPIRam Free Heap   %d\n", ESP.getPsramSize(), ESP.getFreePsram());

  String fcd;
  String direct = "ccd";   //jz bland color for directory

  /*
    fcd = "<div class=\"ccl ccd\">&nbsp;&nbsp; - SORTED 200 max -</div>";
    fcd += "<div class=\"ccz ccd\">&nbsp; &nbsp;</div>";
    fcd += "<div class=\"cct ccd\">&nbsp; &nbsp;</div>";
    fcd += "<div class=\"ccr ccd\">&nbsp;";
    fcd += "&nbsp;&nbsp;</div>";

    fileManager->sendContent(fcd);
  */

  // first file is "go to root"
  String fn = "/";
  fcd = "<div "
        "class=\"ccl " + direct + "\""
        "onclick=\"opendirectory('" + fn + "')\""
        ">&nbsp;&nbsp;" + fn + " - GOTO ROOT DIR -" + "</div>";
  fcd += "<div class=\"ccz " + direct + "\">&nbsp;" + " "  + "&nbsp;</div>";
  fcd += "<div class=\"cct " + direct + "\">&nbsp;" + dispIntDotted(0) + "&nbsp;</div>";
  fcd += "<div class=\"ccr " + direct + "\">&nbsp;";
  fcd += "&nbsp;&nbsp;</div>";

  fileManager->sendContent(fcd);

  // List files
  int i = 0;

  Serial.printf("Internal Total heap %d, internal Free Heap %d, ", ESP.getHeapSize(), ESP.getFreeHeap());
  Serial.printf("SPIRam Total heap   %d, SPIRam Free Heap   %d\n", ESP.getPsramSize(), ESP.getFreePsram());

  //std::list<records>::iterator &iter2;
  while (dirList.size() > 0) {

    auto iter2 = dirList.begin();
    String fn = get<1>(*iter2);

    String fc;

    if (get<4>(*iter2) == 1) {  // directory

      time_t t = get<3>(*iter2) ;
      struct tm * tmstruct = localtime(&t);
      char ccz[30];
      sprintf(ccz, " %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, ( tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour , tmstruct->tm_min, tmstruct->tm_sec);

      char char_fc[400];
      sprintf(char_fc, "<div class=\"ccl ccd  \"onclick=\"opendirectory('%s%s')\">&nbsp;&nbsp;%s - DIR -</div><div class=\"ccz ccd\">&nbsp;%s&nbsp;</div><div class=\"cct ccd\">&nbsp;&nbsp;</div><div class=\"ccr ccd\">&nbsp;<button title=\"Delete\" onclick=\"deletefile('%s')\" class=\"b\">Del</button> &nbsp;&nbsp;</div>",
              nsd.c_str(), fn.c_str(), fn.c_str(), ccz, fn.c_str());
      fileManager->sendContent(char_fc, strlen(char_fc));

      dirList.pop_front();

    } else { // dile

      time_t t = get<3>(*iter2) ;
      struct tm * tmstruct = localtime(&t);
      char ccz[30];
      sprintf(ccz, " %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, ( tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour , tmstruct->tm_min, tmstruct->tm_sec);
      String get2 = dispIntDotted(get<2>(*iter2));

      if ( (fn.endsWith(".avi") ) ) {
        char char_fc[500];
        sprintf(char_fc, "<div class=\"ccl ccu\"onclick=\"downloadfile('%s%s')\">&nbsp;&nbsp;%s</div><div class=\"ccz ccu\">&nbsp;%s&nbsp;</div><div class=\"cct ccu\">&nbsp;%s&nbsp;</div><div class=\"ccr ccu\">&nbsp;<button title=\"Delete\" onclick=\"deletefile('%s')\" class=\"b\">Del</button> &nbsp; <button title=\"Edit\" onclick=\"editavi('%s%s')\" class=\"b\">View</button>&nbsp;&nbsp;</div> ",
                nsd.c_str(), fn.c_str(), fn.c_str(), ccz, get2.c_str(), fn.c_str(), nsd.c_str(), fn.c_str());
        fileManager->sendContent(char_fc, strlen(char_fc));

      } else { // not an avi
        String contentTyp = getContentType(fn);
        if ( (contentTyp.startsWith("text/")) || (contentTyp.startsWith("application/j"))  ) {

          char char_fc[500];
          sprintf(char_fc, "<div class=\"ccl ccu\"onclick=\"downloadfile('%s%s')\">&nbsp;&nbsp;%s</div><div class=\"ccz ccu\">&nbsp;%s&nbsp;</div><div class=\"cct ccu\">&nbsp;%s&nbsp;</div><div class=\"ccr ccu\">&nbsp;<button title=\"Delete\" onclick=\"deletefile('%s')\" class=\"b\">Del</button> &nbsp; <button title=\"Edit\" onclick=\"editfile('%s')\" class=\"b\">Edit</button> &nbsp;&nbsp;</div> ",
                  nsd.c_str(), fn.c_str(), fn.c_str(), ccz, get2.c_str(), fn.c_str(), fn.c_str());
          fileManager->sendContent(char_fc, strlen(char_fc));
        } else {
          char char_fc[500];
          sprintf(char_fc, "<div class=\"ccl ccu\"onclick=\"downloadfile('%s%s')\">&nbsp;&nbsp;%s</div><div class=\"ccz ccu\">&nbsp;%s&nbsp;</div><div class=\"cct ccu\">&nbsp;%s&nbsp;</div><div class=\"ccr ccu\">&nbsp;<button title=\"Delete\" onclick=\"deletefile('%s')\" class=\"b\">Del</button>  &nbsp;&nbsp;</div> ",
                  nsd.c_str(), fn.c_str(), fn.c_str(), ccz, get2.c_str(), fn.c_str());
          fileManager->sendContent(char_fc, strlen(char_fc));
        }
      }

      dirList.pop_front();

      delay(5);
      i++;
    }


    if (ESP.getFreeHeap() < 60000) {
      delay(50);
      Serial.printf("Heap is low %d\n", ESP.getFreeHeap());
      delay(50);
    }

    while (ESP.getFreeHeap() < 50000) {
      delay(1000);
      Serial.printf("Heap is very low %d -- dumping some files -- try again and STOP recording\n", ESP.getFreeHeap());
      while (ESP.getFreeHeap() < 50000) {
        dirList.pop_front();
      }
    }

    if (i % 10 == 8 ) {
      Serial.printf("file %3d ---", i);
      Serial.printf("Internal Total heap %d, internal Free Heap %d, ", ESP.getHeapSize(), ESP.getFreeHeap());
      Serial.printf("SPIRam Total heap   %d, SPIRam Free Heap   %d\n", ESP.getPsramSize(), ESP.getFreePsram());
    }

    if (i % 100 == 50 ) {
      delay(50);
      fileManager->sendContent(F("</div></div>"));
      fileManager->sendContent(F("<div class=\"cc\"><div class=\"gc\">"));
      // first file is "go to root"
      String fn = "/";
      fcd = "<div "
            "class=\"ccl " + direct + "\""
            "onclick=\"opendirectory('" + fn + "')\""
            ">&nbsp;&nbsp;" + fn + " - GOTO ROOT DIR -" + "</div>";
      fcd += "<div class=\"ccz " + direct + "\">&nbsp;" + " "  + "&nbsp;</div>";
      fcd += "<div class=\"cct " + direct + "\">&nbsp;" + dispIntDotted(0) + "&nbsp;</div>";
      fcd += "<div class=\"ccr " + direct + "\">&nbsp;";
      fcd += "&nbsp;&nbsp;</div>";

      fileManager->sendContent(fcd);
    }
  }

  /*
      if (0) {

        String fcd;
        String direct = "ccd";   //jz bland color for directory
        String fn = "/";

        for ( auto& iter : dirList) {
          String fn = get<1>(iter);
          if (i == 80) break;
          //   if ( (_ViewSysFiles) || (allowAccessToThisFile(fn)) ) {

          String fc;
          String nsd;
          if (subdir == "/") {
            nsd = "/";
          } else {
            nsd = subdir + "/";
          }

          if (file.isDirectory()) {
            String direct = "ccd";   //jz bland color for directory
            fc = "<div "
                 "class=\"ccl " + direct + "\""
                 "onclick=\"opendirectory('" + nsd  + fn + "')\""
                 ">&nbsp;&nbsp;" + fn + " - DIR -" + "</div>";

          } else {

            fc = "<div class=\"ccl ccu\"onclick=\"downloadfile('" + nsd + fn + "')\">&nbsp;&nbsp;" + fn + "</div>";

            time_t t = get<3>(iter) ;
            struct tm * tmstruct = localtime(&t);

            char ccz[100];
            sprintf(ccz, " %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, ( tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour , tmstruct->tm_min, tmstruct->tm_sec);

            fc += "<div class=\"ccz ccu\">&nbsp;" + String(ccz) + "&nbsp;</div>"; //jz
            fc += "<div class=\"cct ccu\">&nbsp;" + dispIntDotted(get<2>(iter)) + "&nbsp;</div>";
            fc += "<div class=\"ccr ccu\">&nbsp;<button title=\"Delete\" onclick=\"deletefile('" + fn + "')\" class=\"b\">D</button> " ;

            //61.3
            if ( (fn.endsWith(".avi") ) )
            {
              fc += "<button title=\"Edit\" onclick=\"editavi('" + fn + "')\" class=\"b\">E</button> ";
            } else {
              String contentTyp = getContentType(fn);
              if ( (contentTyp.startsWith("text/")) ||
                   (contentTyp.startsWith("application/j"))  ) // boldly assume: json, javascript and everything else is edible....
              {
                fc += "<button title=\"Edit\" onclick=\"editfile('" + fn + "')\" class=\"b\">E</button> ";
              }
            }

            fc += "&nbsp;&nbsp;</div>";
          }
          fileManager->sendContent(fc);
          i++;
          delay(3);

          while (ESP.getFreeHeap() < 50000) {
            delay(500);
            Serial.printf("Heap is low %d\n", ESP.getFreeHeap());
          }

          if (i % 10 == 8 ) {
            Serial.printf("Internal Total heap %d, internal Free Heap %d, ", ESP.getHeapSize(), ESP.getFreeHeap());
            Serial.printf("SPIRam Total heap   %d, SPIRam Free Heap   %d\n", ESP.getPsramSize(), ESP.getFreePsram());
            Serial.printf("file %d\n", i);
          }
          // }
          //dirList.erase(*iter);
        }
        dirList.clear();

      }

  */


  // fileManager->sendContent("<span id=\"filecount\" data-count=\""+String(i)+"\"></span>");


  /*
    fileManager->sendContent(F(" FS blocksize: "));
    fileManager->sendContent(String(info.blockSize));
    fileManager->sendContent(F(", pageSize: "));
    fileManager->sendContent(String(info.pageSize));
  */
  Serial.printf("Done the send, maybe\n");
  fileManager->sendContent(F("</div></div>"));
  String sinfo = "&nbsp; Size: " +
                 dispFileString(totalBytes()) +
                 ", used: " +
                 dispFileString(usedBytes());
  fileManager->sendContent(F("##"));
  fileManager->sendContent(sinfo);

  fileManager->sendContent("");
  Serial.printf("Done the send\n");
  delay(1);
}

//*****************************************************************************************************
String ESPxWebFlMgr::escapeHTMLcontent(String html) {
  //html.replace("<","&lt;");
  //html.replace(">","&gt;");
  html.replace("&", "&amp;");

  return html;
}

// in place editor
//*****************************************************************************************************
void ESPxWebFlMgr::fileManagerFileEditorInsert(void) {
  //Serial.println("Edit");

  if ( (fileManager->args() == 1) && (fileManager->argName(0) == "edit") ) {

    //String fn = "/" + fileManager->arg(0);
    String fn =  fileManager->arg(0);
    //if ( (! _ViewSysFiles) && (!allowAccessToThisFile(fn)) ) {
    //  fileManager->send(404, F("text/plain"), F("Illegal."));
    //  return;
    //}


    fileManager->setContentLength(CONTENT_LENGTH_UNKNOWN);

    fileManager->send(200, F("text/html"), String());


    fileManager->sendContent(ESPxWebFlMgrWpFormIntro);

    if (ESPxWebFlMgr_FileSystem.exists(subdir + "/" + fn)) {
      File f = ESPxWebFlMgr_FileSystem.open(subdir + "/" + fn, "r");
      if (f) {
        do {
          String l = f.readStringUntil('\n') + '\n';
          l = escapeHTMLcontent(l);
          fileManager->sendContent(l);
        } while (f.available());
        f.close();
      }
    }



    fileManager->sendContent(ESPxWebFlMgrWpFormExtro1);
    fileManager->sendContent(fn);
    fileManager->sendContent(ESPxWebFlMgrWpFormExtro2);

    fileManager->sendContent("");
  } else {
    fileManager->send(404, F("text/plain"), F("Illegal."));
  }

  delay(1);
}

// Drag and Drop
//   https://developer.mozilla.org/en-US/docs/Web/API/HTML_Drag_and_Drop_API/File_drag_and_drop
//   https://www.ab-heute-programmieren.de/drag-and-drop-upload-mit-html5/#Schritt_3_Eine_Datei_hochladen
//*****************************************************************************************************
void ESPxWebFlMgr::fileManagerReceiverOK(void) {
  // Serial.println("fileManagerReceiverOK");
  fileManager->send(200);
  delay(1);
}

//*****************************************************************************************************
void ESPxWebFlMgr::fileManagerReceiver(void) {
  //  Serial.println("fileManagerReceiver");

  HTTPUpload& upload = fileManager->upload();
  //  Serial.println("Server upload Status: " + String(upload.status));

  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    //    Serial.print("handleFileUpload Name: ");
    //    Serial.println(filename);

    if (! ( (_ViewSysFiles) || (allowAccessToThisFile(filename)) ) ) {
      filename = "/illegalfilename";
    }

    // cut length
    filename = CheckFileNameLengthLimit(filename);

    fsUploadFile = ESPxWebFlMgr_FileSystem.open(subdir + filename, "w");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    //    Serial.print("handleFileUpload Data: ");
    //    Serial.println(upload.currentSize);
    if (fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) {
      fsUploadFile.close();
      // fsUploadFile = NULL;
    }
    //    Serial.print("handleFileUpload Size: ");
    //    Serial.println(upload.totalSize);
  }
  delay(1);
}

struct __attribute__ ((__packed__)) zipFileHeader {
  uint32_t signature; // 0x04034b50;
  uint16_t versionneeded;
  uint16_t bitflags;
  uint16_t comp_method;
  uint16_t lastModFileTime;
  uint16_t lastModFileDate;
  uint32_t crc_32;
  uint32_t comp_size;
  uint32_t uncompr_size;
  uint16_t fname_len;
  uint16_t extra_field_len;
};

struct __attribute__ ((__packed__)) zipDataDescriptor {
  uint32_t signature; // 0x08074b50
  uint32_t crc32;
  uint32_t comp_size;
  uint32_t uncompr_size;
};

struct __attribute__ ((__packed__)) zipEndOfDirectory {
  uint32_t signature; // 0x06054b50;
  uint16_t nrofdisks;
  uint16_t diskwherecentraldirectorystarts;
  uint16_t nrofcentraldirectoriesonthisdisk;
  uint16_t totalnrofcentraldirectories;
  uint32_t sizeofcentraldirectory;
  uint32_t ofsetofcentraldirectoryrelativetostartofarchiv;
  uint16_t commentlength;
};

struct __attribute__ ((__packed__)) zipCentralDirectoryFileHeader {
  uint32_t signature; // 0x02014b50
  uint16_t versionmadeby;
  uint16_t versionneededtoextract;
  uint16_t flag;
  uint16_t compressionmethode;
  uint16_t lastModFileTime;
  uint16_t lastModFileDate;
  uint32_t crc_32;
  uint32_t comp_size;
  uint32_t uncompr_size;
  uint16_t fname_len;
  uint16_t extra_len;
  uint16_t comment_len;
  uint16_t diskstart;
  uint16_t internalfileattr;
  uint32_t externalfileattr;
  uint32_t relofsoflocalfileheader;
  // nun filename, extra field, comment
};

//*****************************************************************************************************
int ESPxWebFlMgr::WriteChunk(const char* b, size_t l) {
  //  Serial.print(" Chunk: " + String(l) + " ");

  const char * footer = "\r\n";
  char chunkSize[11];
  sprintf(chunkSize, "%zx\r\n", l);
  fileManager->client().write(chunkSize, strlen(chunkSize));
  fileManager->client().write(b, l);
  fileManager->client().write(footer, 2);

  return strlen(chunkSize) + l + 2;
}

//*****************************************************************************************************
/* https://en.wikipedia.org/wiki/Zip_(file_format)
   https://www.fileformat.info/tool/hexdump.htm
   https://hexed.it/?hl=de
   HxD https://mh-nexus.de/de/

   This code needs some memory:
     4 * <nr. of files> + copybuffersize

   Uses no compression, because, well, code size. Should be good for 4mb.
*/
void ESPxWebFlMgr::getAllFilesInOneZIP(void) {
  const byte copybuffersize = 100;

  fileManager->setContentLength(CONTENT_LENGTH_UNKNOWN);
  // fileManager->sendHeader(F("Content-Type"), F("text/text"));
  // fileManager->sendHeader(F("Transfer-Encoding"), F("chunked"));
  // fileManager->sendHeader(F("Connection"), F("close"));
  fileManager->sendHeader(F("Content-Disposition"), F("attachment; filename=alles.zip"));
  fileManager->sendHeader(F("Content-Transfer-Encoding"), F("binary"));
  fileManager->send(200, F("application/octet-stream"), "");

  // Pass 0: count files
  int files = 0;
  {
    Dir dir;
    File file = firstFile(dir);
    while (file) {
      String fn = file.name();
      if (!file.isDirectory() && (file.size() != 0)) {  //jz
        if ( (_ViewSysFiles) || (allowAccessToThisFile(fn)) ) {
          files++;
        }
      }
      file = nextFile(dir);
    }
    //Serial.println("Files: " + String(files));
  }
  // Store the crcs
  uint32_t crc_32s[files];

  // Pass 1: local headers + file
  {
    zipFileHeader zip;
    zip.signature = 0x04034b50;
    zip.versionneeded = 0;
    zip.bitflags = 1 << 3;
    zip.comp_method = 0; // stored
    zip.lastModFileTime = 0x4fa5;
    zip.lastModFileDate = 0xe44e;
    zip.extra_field_len = 0;

    int i = 0;
    Dir dir;
    File file = firstFile(dir);
    while (file) {
      String fn = file.name();
      if (!file.isDirectory() && (file.size() != 0) ) { //jz
        if ( (_ViewSysFiles) || (allowAccessToThisFile(fn)) ) {
          if (fn.indexOf("/") == 0) {
            fn.remove(0, 1); // "/" filenames beginning with "/" dont work for Windows....
          }

          zip.comp_size = 0;
          zip.uncompr_size = 0;
          zip.crc_32 = 0;
          zip.fname_len = fn.length();
          WriteChunk((char*)&zip, sizeof(zip));
          WriteChunk(fn.c_str(), zip.fname_len);

          //Serial.print("Send: " + fn);
          // File f = dir.open("r",FILE_READ);
          int len = file.size();
          //Serial.print("\nsending "); Serial.print(fn);
          //Serial.print(" len is "); Serial.println(len);

          // send crc+len later...
          zipDataDescriptor datadiscr;
          datadiscr.signature = 0x08074b50;
          datadiscr.comp_size = len;
          datadiscr.uncompr_size = len;

          const char * footer = "\r\n";
          char chunkSize[11];
          sprintf(chunkSize, "%zx\r\n", len);
          fileManager->client().write(chunkSize, strlen(chunkSize));

          { // pff.
            CRC32 crc;
            byte b[copybuffersize];
            int lenr = len;
            while (lenr > 0) {
              byte r = (lenr > copybuffersize) ? copybuffersize : lenr;
              file.read(b, r);
              crc.update(b, r);
              fileManager->client().write(b, r);
              lenr -= r;
              //  Serial.print(lenr);Serial.print(","); //jz
            }
            //Serial.println(" done");
            datadiscr.crc32 = crc.finalize();
            crc_32s[i] = datadiscr.crc32;
          }

          fileManager->client().write(footer, 2);

          WriteChunk((char*)&datadiscr, sizeof(datadiscr));

          // f.close();
          i++;
          /** /
                  Serial.print(" ");
                  Serial.print(l);
                  Serial.println();
            /**/
        }
      }
      file = nextFile(dir);
    }

  }

  // Pass 2: Central directory Structur
  {
    zipEndOfDirectory eod;
    eod.signature =  0x06054b50;
    eod.nrofdisks = 0;
    eod.diskwherecentraldirectorystarts = 0;
    eod.nrofcentraldirectoriesonthisdisk = 0;
    eod.totalnrofcentraldirectories = 0;
    eod.sizeofcentraldirectory = 0;
    eod.ofsetofcentraldirectoryrelativetostartofarchiv = 0;
    eod.commentlength = 0;

    zipCentralDirectoryFileHeader CDFH;

    CDFH.signature = 0x02014b50;
    CDFH.versionmadeby = 0;
    CDFH.versionneededtoextract = 0;
    CDFH.flag = 0;
    CDFH.compressionmethode = 0; // Stored
    CDFH.lastModFileTime = 0x4fa5;
    CDFH.lastModFileDate = 0xe44e;
    CDFH.extra_len = 0;
    CDFH.comment_len = 0;
    CDFH.diskstart = 0;
    CDFH.internalfileattr = 0x01;
    CDFH.externalfileattr = 0x20;
    CDFH.relofsoflocalfileheader = 0;

    int i = 0;

    Dir dir;
    File file = firstFile(dir);
    while (file) {
      String fn = file.name();
      if (!file.isDirectory() && (file.size() != 0)) { //jz
        if ( (_ViewSysFiles) || (allowAccessToThisFile(fn)) ) {
          if (fn.indexOf("/") == 0) {
            fn.remove(0, 1); // "/" filenames beginning with "/" dont work for Windows....
          }
          //        Serial.print("CDFH: " + fn);
          // File f = dir.open("r",FILE_READ);
          int len = file.size();

          //Serial.print("\nsending "); Serial.print(fn);  //jz
          //Serial.print(" len is "); Serial.println(len);

          CDFH.comp_size = len;
          CDFH.uncompr_size = len;
          CDFH.fname_len = fn.length();
          CDFH.crc_32 = crc_32s[i];

          // f.close();

          WriteChunk((char*)&CDFH, sizeof(CDFH));
          WriteChunk(fn.c_str(), CDFH.fname_len);

          int ofs = sizeof(zipFileHeader) + len + CDFH.fname_len + sizeof(zipDataDescriptor);

          // next position
          CDFH.relofsoflocalfileheader += ofs;

          // book keeping
          eod.nrofcentraldirectoriesonthisdisk++;
          eod.totalnrofcentraldirectories++;
          eod.ofsetofcentraldirectoryrelativetostartofarchiv += ofs;
          eod.sizeofcentraldirectory += sizeof(CDFH) + CDFH.fname_len;

          i++;
        }
      }
      file = nextFile(dir);
    }

    //    Serial.print("EOD: ");
    WriteChunk((char*)&eod, sizeof(eod));
    //    Serial.println();
  }

  const char * endchunk = "0\r\n\r\n";
  fileManager->client().write(endchunk, 5);

  fileManager->sendContent("");
  delay(1);
}

//*****************************************************************************************************
void ESPxWebFlMgr::fileManagerCommandExecutor(void) {
  // https://www.youtube.com/watch?v=KSxTxynXiBs
  /*
    for (uint8_t i = 0; i < fileManager->args(); i++) {
    Serial.print(i);
    Serial.print(" ");
    Serial.print(fileManager->argName(i));
    Serial.print(": ");
    Serial.print(fileManager->arg(i));
    Serial.println();
    }
  */

  // no Args: DIE!
  if (fileManager->args() == 0) {
    return;
  }

  // +--++--++--++--++--++--++--++--++--++--++--++--++--++--++--+
  // one arg, "za", zip all and download
  if ( (fileManager->args() == 1) && (fileManager->argName(0) == "za") ) {
    getAllFilesInOneZIP();
    // does it all
    return;
  }

  // +--++--++--++--++--++--++--++--++--++--++--++--++--++--++--+
  // one arg, "dwn", download
  // must happen in the context of the webpage... thus via "window.location.href="/c?dwn="+filename;"
  if ( (fileManager->args() == 1) && (fileManager->argName(0) == "dwn") ) {
    String fn = fileManager->arg(0);
    Serial.println(fn);
    if ( (_ViewSysFiles) || (allowAccessToThisFile(fn)) ) {
      //Serial.println("allowed");
      File f = ESPxWebFlMgr_FileSystem.open("/" + fn, "r"); // add slash for esp32_arduino 2.0
      if (f) {
        //Serial.println("got it open");
        fileManager->sendHeader(F("Content-Type"), F("text/text"));
        fileManager->sendHeader(F("Connection"), F("close"));
        //Serial.print(">");Serial.print(fn);Serial.println("<");
        //Serial.println(fn.indexOf("/"));
        if (fn.indexOf("/") == 0) {
          fileManager->sendHeader(F("Content-Disposition"), "attachment; filename=" + fn.substring(1));
        } else {
          fileManager->sendHeader(F("Content-Disposition"), "attachment; filename=" + fn);
        }
        fileManager->sendHeader(F("Content-Transfer-Encoding"), F("binary"));
        if (fileManager->streamFile(f, "application/octet-stream") != f.size()) {
          Serial.println(F("Sent less data than expected!"));
        }
        f.close();
        return;
      } else {
        Serial.print("Could not open file "); Serial.println(fn);
      }
    }
  }

  // +--++--++--++--++--++--++--++--++--++--++--++--++--++--++--+
  // one arg, "opd", opendirectory
  if ( (fileManager->args() == 1) && (fileManager->argName(0) == "opd") ) {
    String fn = fileManager->arg(0);
    if ( (_ViewSysFiles) || (allowAccessToThisFile(fn)) ) {
      delay(1);
    }
  }

  // +--++--++--++--++--++--++--++--++--++--++--++--++--++--++--+
  // one arg, "del", delete
  if ( (fileManager->args() == 1) && (fileManager->argName(0) == "del") ) {
    String fn = fileManager->arg(0);
    if ( (_ViewSysFiles) || (allowAccessToThisFile(fn)) ) {
      int x = ESPxWebFlMgr_FileSystem.remove( subdir + "/" + fn);  // Add slash
      //delay(1000);
      if (!x) {
        Serial.print("remove failed, try rmdir ");
        Serial.print( "/" + fn );
        File f = ESPxWebFlMgr_FileSystem.open( "/" + fn);
        if (!f) {
          Serial.printf("Failed to open %s\n",  "/" + fn);
          return;
        }
        if (f.isDirectory()) {
          String subdir = fn;
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
              Serial.print(">>/" + subdir + "/" + file.name() + "<<");
              if (ESPxWebFlMgr_FileSystem.remove("/" + subdir + "/" + file.name())) {
                Serial.println(" deleted.");
              } else {
                Serial.println(" FAILED.");
              }
            }
            file = f.openNextFile();
          }
          f.close();
          //Remove the dir
          if (ESPxWebFlMgr_FileSystem.rmdir( "/" + fn)) {
            Serial.printf("Dir removed:"); Serial.println(  "/" + fn);
          } else {
            Serial.println("Remove dir failed");
          }
        }
        //int y = ESPxWebFlMgr_FileSystem.rmdir(  "/" + fn);  // Add slash


        //if (!y) {
        //  Serial.print("rmdir failed, directory must be empty! ");
        //}
      }
    }
  }

  // +--++--++--++--++--++--++--++--++--++--++--++--++--++--++--+
  // one arg, "ren", rename
  if ( (fileManager->args() == 2) && (fileManager->argName(0) == "ren") ) {
    String fn = fileManager->arg(0);
    if ( (_ViewSysFiles) || (allowAccessToThisFile(fn)) ) {
      String fn2 = CheckFileNameLengthLimit(fileManager->arg(1));
      if ( (_ViewSysFiles) || (allowAccessToThisFile(fn2)) ) {
        Serial.println(subdir);
        Serial.println(fn);
        Serial.println(fn2);
        ESPxWebFlMgr_FileSystem.rename( subdir + "/" + fn,  subdir + "/" + fn2);
      }
    }
  }

  // dummy answer
  fileManager->send(200, "text/plain", "");
  delay(1);
}
