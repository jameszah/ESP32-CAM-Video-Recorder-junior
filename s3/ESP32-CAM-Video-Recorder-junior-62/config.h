#include <pgmspace.h>

//61.3
///////////////////////////////
const char edit_html[] PROGMEM = R"===(
<!doctype html>
<html>
   <head>
   <style>
.slidecontainer {
  width: 100%;
}

.slider {
  -webkit-appearance: none;
  width: 600px;
  height: 25px;
  background: #d3d3d3;
  outline: none;
  opacity: 0.7;
  -webkit-transition: .2s;
  transition: opacity .2s;
}

.slider:hover {
  opacity: 1;
}

.slider::-webkit-slider-thumb {
  -webkit-appearance: none;
  appearance: none;
  width: 10px;
  height: 25px;
  background: #04AA6D;
  cursor: pointer;
}

.slider::-moz-range-thumb {
  width: 10px;
  height: 25px;
  background: #04AA6D;
  cursor: pointer;
}
</style>

      <title>ESP32-CAM Video Recorder Junior - Video Editor</title>
      <script>
var baseHost = document.location.origin
var streamUrl = baseHost + ':81/stream_avi'

document.addEventListener('DOMContentLoaded', function (event) {

   const view = document.getElementById('liveFeed')
// here zzz
}
)
   const stopStream = () => {}


   
   function initialize() {
         
//      const query = `${baseHost}/find?f=/JamCam0481.0007.avi&n=0` 

//
   
   const query2 = `${baseHost}/status`
   fetch(query2)
      .then(function (response) {
         return response.json()
      })
      .then(function (state) {
         var x = state.toString()
         //console.log(x)
         //document.getElementById("file_to_edit").innerHTML = state.file_to_edit
         console.log(`request to ${query2} finished, OnOff: ${state.OnOff}`)
         console.log(`File: ${state.File}`)
         console.log(`Remain: ${state.Remain}`)
         console.log(`Total: ${state.Size}`)
         console.log(`Free: ${state.Free}`)
         
         console.log(`file_to_edit: ${state.file_to_edit}`)
         document.getElementById("file_to_edit").value = state.file_to_edit
         file_to_write = state.file_to_edit
         let newText = file_to_write.replace(".avi", "index.avi");
         document.getElementById("file_to_write").value = newText
         
         console.log( document.getElementById("file_to_edit").value)


        //
// +/JamCam0481.0007.avi
    console.log(document.getElementById("file_to_edit").value)
    const query = `${baseHost}/find?f=` + document.getElementById("file_to_edit").value + `&n=0` 
    console.log(`the query --- ${query}`)
    
     //document.getElementById('liveFeed').src = query // does query twice?

fetch(query)
  .then(response => { 
    response.blob()
    .then(blob => { 
    
     document.getElementById('liveFeed').src = URL.createObjectURL(blob); 
     document.getElementById("fsta").value =  0;
     document.getElementById("fcur").value =  response.headers.get("framenum");
     //document.getElementById("ftotal").value =  response.headers.get("total");
     document.getElementById("fend").value =  response.headers.get("total");
     document.getElementById("myRange").max = response.headers.get("total");
     
     console.log(`request to ${query} finished, status: ${response.status}`)
     console.log("filename: " + response.headers.get("file"));   
     console.log("filepct: " + response.headers.get("framepct")); 
           })
        })  
      })
   }

      </script>
   </head>
   <body onload="initialize()" style="background-color: white">
      <div id="second">
         <h2>ESP32-CAM Video Recorder Junior</h2>
         

      </div>
      <div id="third">
         <img id="liveFeed" style="font-family: monospace;  text-align: center;
            color: white; position: relative"
            alt="There seems to be a problem with the live feed..."/>
         <br>
         <div class="slidecontainer">
  <input type="range" min="0" max="100" value="0" class="slider" id="myRange">
  
  <p>Start: <input type="number" id="fsta" min=0 max=99999 style="color:blue;" value="0"> 
  --- 
  Current: <input type="number" id="fcur" min=0 max=99999 onchnage="fcurchange()" style="color:blue;" value="0"> 
  --- 
  End: <input type="number" id="fend" min=0 max=99999 style="color:blue;" value="0"> </p>
  <p>Click on the bar to look at frames through the video </p>
  <p>Live videos show up to the current frame - reload for newer frames </p>
  <p>File: <input type="text" id="file_to_edit" readonly size=25 style="color:red;" value="???"> 
  

  New File Name: <input type="text" id="file_to_write" size=25 style="color:blue;" value="???"></p>
  If it says 2 frames, then re-index the file:  <button id="reindex">ReIndex the File</button>

      </div>
      <div id="four">
         <hr>
         <br>
         <textarea id="freeform" name="freeformname" rows="5" cols="40">
Click Record Status ...
             </textarea>
      </div>
      <a href="https://github.com/jameszah/ESP32-CAM-Video-Recorder-junior">https://github.com/jameszah/ESP32-CAM-Video-Recorder-junior</a> <br>ver 62
<script>
var slider = document.getElementById("myRange");
var fcur = document.getElementById("fcur");
var fsta = document.getElementById("fsta");
var fend = document.getElementById("fend");
fcur.value = slider.value;

//document.getElementById('liveFeed').src = query // does query twice?

function fcurchange() {
    //if (fcur.value != this.value){
    //fcur.value = this.value;
    const query = `${baseHost}/find?f=/JamCam0481.0007.avi&n=` + fcur.value 

fetch(query)
  .then(response => { 
    response.blob()
    .then(blob => { 
    
     document.getElementById('liveFeed').src = URL.createObjectURL(blob); 
     document.getElementById("fcur").value =  response.headers.get("framenum");
     //document.getElementById("ftotal").value =  response.headers.get("total");
     console.log(`request to ${query} finished, status: ${response.status}`)
     //console.log("filename: " + response.headers.get("file"));   
     //console.log("filepct: " + response.headers.get("framepct")); 
    })
  })  
  //}
}
slider.onchange = function() {
  if (fcur.value != this.value){
    fcur.value = this.value;
    var fn = document.getElementById("file_to_edit").value
    const query = `${baseHost}/find?f=` + fn + `&n=` + this.value 

fetch(query)
  .then(response => { 
    response.blob()
    .then(blob => { 
    
     document.getElementById('liveFeed').src = URL.createObjectURL(blob); 
     document.getElementById("fcur").value =  response.headers.get("framenum");
     //document.getElementById("ftotal").value =  response.headers.get("total");
     console.log(`request to ${query} finished, status: ${response.status}`)
     //console.log("filename: " + response.headers.get("file"));   
     //console.log("filepct: " + response.headers.get("framepct")); 
    })
  })  
  }
}


const reindexButton = document.getElementById('reindex')
reindexButton.onclick = () => {
  oldname = document.getElementById("file_to_edit").value 
  newname =  document.getElementById("file_to_write").value 

  document.getElementById('freeform').value =  "This will take a minute - wait here!\n" 
   
  const query = `${baseHost}/reindex?o=` + oldname +  `&n=` + newname 

  
  fetch(query)
     .then(response => {
        console.log(`request to ${query} finished, status: ${response.status}`)
        document.getElementById('freeform').value =  "DONE - go back to the list of files\n"  
   })
  
}



</script>
   </body>
</html>
)===";

////////////////////////////////

//61.3
const char config_txt[] PROGMEM = 
R"==x==(desklens  // camera name
13  // framesize  13=hd
1800  // length of video in seconds
0  // interval - ms between recording frames 
1  // speedup - multiply framerate 
0  // streamdelay - ms between streaming frames
GMT  // timezone
ssid  // ssid #1 wifi name
12344321  // ssid password
ssid  // ssid $2 wifi name
12344321  // ssid password
ssid  // AP ssid wifi name - switch to camaera name if it is ssid
12344321  // ssid password
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
Above lines - 1 item per line followed by 2 spaces

desklens - camera name for files, mdns, etc
13 - framesize 13 is hd 720p 1280x720
 - for ov2640 camera 10=vga, 11=svga, 12=xga, 13=hd, 14=sxga, 15=uxga
 - for ov5640 camera add 16=fhd, 19=qxga, 20=qhd, 23=qsxga
1800 - length of video in seconds
0 - interval - millisecond between frames for recording
 - 0 is as fast as possible bound by camera speed or sd writing speed
 - 500 is 2 frames per second (subject to bounds)
 - 10000 is 0.1 frames per second or 10 seconds per frame
 - vga is max 25 fps, hd is max 12.5 fps, uxga at 12.5 fps etc
1 - speedup - multiply framerate to speed up playback
  - 1 is realtime
  - 24 will play a 1 fps recording at 24 fps on your computer
  - 300 will play 10 sec per frame at 30 fps on your computer
0 - streamdelay - milliseconds of delay between streaming frames
 - this allows you to slow down streaming to save battery power 
     and network usage
 - also allows a fixed rate if you are recording the stream at your computer
 - vga will stream about 30-33 fps depending on wifi and brightness colors 
    in the jpeg
 - hd will stream about 10-12 fps 
 - uxga will only stream at 3 fps 
 - ov5640 camera will record 1280x720 at 25fps, etc
GMT - timezone for dates and times on your files
  - mountain: MST7MDT,M3.2.0/2:00:00,M11.1.0/2:00:00
  - eastern: EST5EDT,M3.2.0,M11.1.0
  - central europe: CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00
  - Ulaanbaatar, Mongolia: ULAT-8ULAST,M3.5.0/2,M9.5.0/2 
  - find timezone here: 
     https://sites.google.com/a/usapiens.com/opnode/time-zones
ssid - your wifi ssid username
 - defualt ap written on serial monitor name_CC, where name is
   your camera name, and CC is last 2 digits of MAC address
12344321 - your wifi ssid password

SourceCode:  https://github.com/jameszah/ESP32-CAM-Video-Recorder-junior
One-Click Installer: https://jameszah.github.io/ESP32-CAM-VideoCam/
James Zahary - Dec 1, 2024

)==x==";
