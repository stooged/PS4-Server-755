#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <FS.h>

ADC_MODE(ADC_VCC);
MD5Builder md5;
DNSServer dnsServer;
ESP8266WebServer webServer;
File upFile;

String firmwareVer = "1.00";

//-------------------DEFAULT SETTINGS------------------//
String AP_SSID = "PS4_WEB_AP";
String AP_PASS = "password";
int WEB_PORT = 80;
IPAddress Server_IP(10,1,1,1);
IPAddress Subnet_Mask(255,255,255,0);
//-----------------------------------------------------//


String split(String str, String from, String to)
{
  String tmpstr = str;
  tmpstr.toLowerCase();
  from.toLowerCase();
  to.toLowerCase();
  int pos1 = tmpstr.indexOf(from);
  int pos2 = tmpstr.indexOf(to, pos1 + from.length());   
  String retval = str.substring(pos1 + from.length() , pos2);
  return retval;
}


bool instr(String str, String search)
{
int result = str.indexOf(search);
if (result == -1)
{
  return false;
}
return true;
}


String formatBytes(size_t bytes){
  if (bytes < 1024){
    return String(bytes)+" B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+" KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+" MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+" GB";
  }
}


void sendwebmsg(String htmMsg)
{
    String tmphtm = "<!DOCTYPE html><html><head><style>body { background-color: #1451AE;color: #ffffff;font-size: 14px; font-weight: bold; margin: 0 0 0 0.0; padding: 0.4em 0.4em 0.4em 0.6em;}</style></head><center><br><br><br><br><br><br>" + htmMsg + "</center></html>";
    webServer.setContentLength(tmphtm.length());
    webServer.send(200, "text/html", tmphtm);
}


String errorMsg(int errnum)
{
  if(errnum == UPDATE_ERROR_OK){
    return "No Error";
  } else if(errnum == UPDATE_ERROR_WRITE){
    return "Flash Write Failed";
  } else if(errnum == UPDATE_ERROR_ERASE){
    return "Flash Erase Failed";
  } else if(errnum == UPDATE_ERROR_READ){
    return "Flash Read Failed";
  } else if(errnum == UPDATE_ERROR_SPACE){
    return "Not Enough Space";
  } else if(errnum == UPDATE_ERROR_SIZE){
    return "Bad Size Given";
  } else if(errnum == UPDATE_ERROR_STREAM){
    return "Stream Read Timeout";
  } else if(errnum == UPDATE_ERROR_MD5){
    return "MD5 Check Failed";
  } else if(errnum == UPDATE_ERROR_FLASH_CONFIG){
     return "Flash config wrong real: " + String(ESP.getFlashChipRealSize()) + "<br>IDE: " + String( ESP.getFlashChipSize());
  } else if(errnum == UPDATE_ERROR_NEW_FLASH_CONFIG){
    return "new Flash config wrong real: " + String(ESP.getFlashChipRealSize());
  } else if(errnum == UPDATE_ERROR_MAGIC_BYTE){
    return "Magic byte is wrong, not 0xE9";
  } else if (errnum == UPDATE_ERROR_BOOTSTRAP){
    return "Invalid bootstrapping state, reset ESP8266 before updating";
  } else {
    return "UNKNOWN";
  }
}


String getContentType(String filename){
  if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  else if(filename.endsWith(".manifest")) return "text/cache-manifest";
  return "text/plain";
}


bool loadFromSdCard(String path) {
//Serial.println(path);
 if (path.equals("/config.ini"))
 {
  return false;
 }
  if (path.endsWith("/")) {
    path += "index.html";
  }
  if (instr(path,"/update/ps4/"))
  {
    String Region = split(path,"/update/ps4/list/","/");
    handleConsoleUpdate(Region);
    return true;
  }
  if (instr(path,"/document/"))
  {
    path.replace("/document/" + split(path,"/document/","/ps4/") + "/ps4/", "/");
  }
  String dataType = getContentType(path);
  
  if (path.endsWith(".js")) {
    path = path + ".gz";
  }
  
  File dataFile = SPIFFS.open(path, "r");
  if (!dataFile) {
     if (path.endsWith("index.html") || path.endsWith("index.htm"))
     {
       handlePayloads();
       return true;
      }
    return false;
  }
  if (webServer.hasArg("download")) {
    dataType = "application/octet-stream";
    String dlFile = path;
    if (dlFile.startsWith("/"))
    {
     dlFile = dlFile.substring(1);
    }
    webServer.sendHeader("Content-Disposition", "attachment; filename=\"" + dlFile + "\"");
    webServer.sendHeader("Content-Transfer-Encoding", "binary");
  }
  if (webServer.streamFile(dataFile, dataType) != dataFile.size()) {
    //Serial.println("Sent less data than expected!");
  }
  dataFile.close();
  return true;
}


void handleNotFound() {
  if (loadFromSdCard(webServer.uri())) {
    return;
  }
  String message = "\n\n";
  message += "URI: ";
  message += webServer.uri();
  message += "\nMethod: ";
  message += (webServer.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += webServer.args();
  message += "\n";
  for (uint8_t i = 0; i < webServer.args(); i++) {
    message += " NAME:" + webServer.argName(i) + "\n VALUE:" + webServer.arg(i) + "\n";
  }
  webServer.send(404, "text/plain", "Not Found");
  //Serial.print(message);
}

void handleFileUpload() {
  if (webServer.uri() != "/upload.html") {
    webServer.send(500, "text/plain", "Internal Server Error");
    return;
  }
  HTTPUpload& upload = webServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    if (filename.equals("/config.ini"))
    {return;}
    upFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (upFile) {
      upFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (upFile) {
      upFile.close();
    }
  }
}


void handleFwUpdate() {
  if (webServer.uri() != "/update.html") {
    sendwebmsg("Error");
    return;
  }
  HTTPUpload& upload = webServer.upload();
  if (upload.filename != "fwupdate.bin") {
    sendwebmsg("Invalid update file: " + upload.filename);
    return;
  }
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    upFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (upFile) {
      upFile.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (upFile) {
      upFile.close();
    }
    updateFw();
  }
}


void updateFw()
{
  if (SPIFFS.exists("/fwupdate.bin")) {
  File updateFile;
  //Serial.println("Update file found");
  updateFile = SPIFFS.open("/fwupdate.bin", "r");
 if (updateFile) {
  size_t updateSize = updateFile.size();
   if (updateSize > 0) {   
    md5.begin();
    md5.addStream(updateFile,updateSize);
    md5.calculate();
    String md5Hash = md5.toString();
    //Serial.println("Update file hash: " + md5Hash);
    updateFile.close();
    updateFile = SPIFFS.open("/fwupdate.bin", "r");
  if (updateFile) {
  pinMode(BUILTIN_LED, OUTPUT);
  digitalWrite(BUILTIN_LED, LOW);
    uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    if (!Update.begin(maxSketchSpace, U_FLASH)) {
    //Update.printError(Serial);
    digitalWrite(BUILTIN_LED, HIGH);
    updateFile.close();
    sendwebmsg("Update failed<br><br>" + errorMsg(Update.getError()));
    return;
    }
    int md5BufSize = md5Hash.length() + 1;
    char md5Buf[md5BufSize];
    md5Hash.toCharArray(md5Buf, md5BufSize) ;
    Update.setMD5(md5Buf);
    //Serial.println("Updating firmware...");
   long bsent = 0;
   int cprog = 0;
    while (updateFile.available()) {
    uint8_t ibuffer[1];
    updateFile.read((uint8_t *)ibuffer, 1);
    Update.write(ibuffer, sizeof(ibuffer));
      bsent++;
      int progr = ((double)bsent /  updateSize)*100;
      if (progr >= cprog) {
        cprog = progr + 10;
      //Serial.println(String(progr) + "%");
      }
    }
    updateFile.close(); 
  if (Update.end(true))
  {
  digitalWrite(BUILTIN_LED, HIGH);
  //Serial.println("Installed firmware hash: " + Update.md5String()); 
  //Serial.println("Update complete");
  SPIFFS.remove("/fwupdate.bin");
  sendwebmsg("Uploaded file hash: " + md5Hash + "<br>Installed firmware hash: " + Update.md5String() + "<br><br>Update complete, Rebooting.");
  delay(1000);
  ESP.restart();
  }
  else
  {
    digitalWrite(BUILTIN_LED, HIGH);
    //Serial.println("Update failed");
    sendwebmsg("Update failed");
     //Update.printError(Serial);
    }
  }
  }
  else {
  //Serial.println("Error, file is invalid");
  updateFile.close(); 
  digitalWrite(BUILTIN_LED, HIGH);
  SPIFFS.remove("/fwupdate.bin");
  sendwebmsg("Error, file is invalid");
  return;    
  }
  }
  }
  else
  {
    //Serial.println("No update file found");
    sendwebmsg("No update file found");
  }
}


void handleFormat()
{
  //Serial.print("Formatting SPIFFS");
  SPIFFS.end();
  SPIFFS.format();
  SPIFFS.begin();
  writeConfig();
  webServer.sendHeader("Location","/fileman.html");
  webServer.send(302, "text/html", "");
}


void handleDelete(){
  if(!webServer.hasArg("file")) 
  {
    webServer.sendHeader("Location","/fileman.html");
    webServer.send(302, "text/html", "");
    return;
  }
 String path = webServer.arg("file");
 if (SPIFFS.exists("/" + path) && path != "/" && !path.equals("config.ini")) {
    SPIFFS.remove("/" + path);
 }
   webServer.sendHeader("Location","/fileman.html");
   webServer.send(302, "text/html", "");
}


void handleFileMan() {
  Dir dir = SPIFFS.openDir("/");
  String output = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>File Manager</title><style type=\"text/css\">a:link {color: #ffffff; text-decoration: none;} a:visited {color: #ffffff; text-decoration: none;} a:hover {color: #ffffff; text-decoration: underline;} a:active {color: #ffffff; text-decoration: underline;} table {font-family: arial, sans-serif; border-collapse: collapse; width: 100%;} td, th {border: 1px solid #dddddd; text-align: left; padding: 8px;} button {display: inline-block; padding: 1px; margin-right: 6px; vertical-align: top; float:left;} body {background-color: #1451AE;color: #ffffff; font-size: 14px; padding: 0.4em 0.4em 0.4em 0.6em; margin: 0 0 0 0.0;}</style><script>function statusDel(fname) {var answer = confirm(\"Are you sure you want to delete \" + fname + \" ?\");if (answer) {return true;} else { return false; }}</script></head><body><br><table>"; 
  while(dir.next()){
    File entry = dir.openFile("r");
    String fname = String(entry.name()).substring(1);
    if (fname.length() > 0 && !fname.equals("config.ini"))
    {
    output += "<tr>";
    output += "<td><a href=\"" +  fname + "\">" + fname + "</a></td>";
    output += "<td>" + formatBytes(entry.size()) + "</td>";
    output += "<td><form action=\"/" + fname + "?download=1\" method=\"post\"><button type=\"submit\" name=\"file\" value=\"" + fname + "\">Download</button></form></td>";
    output += "<td><form action=\"/delete\" method=\"post\"><button type=\"submit\" name=\"file\" value=\"" + fname + "\" onClick=\"return statusDel('" + fname + "');\">Delete</button></form></td>";
    output += "</tr>";
    }
    entry.close();
  }
  output += "</table></body></html>";
  webServer.setContentLength(output.length());
  webServer.send(200, "text/html", output);
}


void handlePayloads() {
  Dir dir = SPIFFS.openDir("/");
  String output = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>PS4 Server</title><style>.btn {background-color: DodgerBlue; border: none; color: white; padding: 12px 16px; font-size: 16px; cursor: pointer; font-weight: bold;}.btn:hover {background-color: RoyalBlue;}body {background-color: #1451AE; color: #ffffff; text-shadow: 3px 2px DodgerBlue;)</style></head><body><center><h1>PS4 Payloads</h1>";
  int cntr = 0;
  while(dir.next()){
    File entry = dir.openFile("r");
    String fname = String(entry.name()).substring(1);
    if (fname.length() > 0)
    {
    if (fname.endsWith(".html") && fname != "index.html")
    {
    String fnamev = fname;
    fnamev.replace(".html","");
    output +=  "<a href='" +  fname + "'><button class='btn'>" + fnamev  + "</button></a>&nbsp;";
     cntr++;
     if (cntr == 3)
     {
      cntr = 0;
      output +=  "<p></p>";
      }
    }
    }
    entry.close();
  }
  output += "</center></body></html>";
  webServer.setContentLength(output.length());
  webServer.send(200, "text/html", output);
}


void handleConfig()
{
  if(webServer.hasArg("ap_ssid") && webServer.hasArg("ap_pass") && webServer.hasArg("web_ip") && webServer.hasArg("web_port") && webServer.hasArg("subnet")) 
  {
    AP_SSID = webServer.arg("ap_ssid");
    AP_PASS = webServer.arg("ap_pass");
    String tmpip = webServer.arg("web_ip");
    String tmpwport = webServer.arg("web_port");
    String tmpsubn = webServer.arg("subnet");
    File iniFile = SPIFFS.open("/config.ini", "w");
    if (iniFile) {
    iniFile.print("\r\nSSID=" + AP_SSID + "\r\nPASSWORD=" + AP_PASS + "\r\n\r\nWEBSERVER_IP=" + tmpip + "\r\nWEBSERVER_PORT=" + tmpwport + "\r\n\r\nSUBNET_MASK=" + tmpsubn + "\r\n");
    iniFile.close();
    }
    String htmStr = "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"8; url=/info.html\"><style type=\"text/css\">#loader {  z-index: 1;   width: 50px;   height: 50px;   margin: 0 0 0 0;   border: 6px solid #f3f3f3;   border-radius: 50%;   border-top: 6px solid #3498db;   width: 50px;   height: 50px;   -webkit-animation: spin 2s linear infinite;   animation: spin 2s linear infinite; } @-webkit-keyframes spin {  0%  {  -webkit-transform: rotate(0deg);  }  100% {  -webkit-transform: rotate(360deg); }}@keyframes spin {  0% { transform: rotate(0deg); }  100% { transform: rotate(360deg); }} body { background-color: #1451AE; color: #ffffff; font-size: 20px; font-weight: bold; margin: 0 0 0 0.0; padding: 0.4em 0.4em 0.4em 0.6em;}   #msgfmt { font-size: 16px; font-weight: normal;}#status { font-size: 16px;  font-weight: normal;}</style></head><center><br><br><br><br><br><p id=\"status\"><div id='loader'></div><br>Config saved<br>Rebooting</p></center></html>";
    webServer.setContentLength(htmStr.length());
    webServer.send(200, "text/html", htmStr);
    delay(1000);
    ESP.restart();
  }
  else
  {
   webServer.sendHeader("Location","/config.html");
   webServer.send(302, "text/html", "");
  }
}


void handleReboot()
{
  //Serial.print("Rebooting ESP");
  String htmStr = "<!DOCTYPE html><html><head><meta http-equiv=\"refresh\" content=\"8; url=/info.html\"><style type=\"text/css\">#loader {  z-index: 1;   width: 50px;   height: 50px;   margin: 0 0 0 0;   border: 6px solid #f3f3f3;   border-radius: 50%;   border-top: 6px solid #3498db;   width: 50px;   height: 50px;   -webkit-animation: spin 2s linear infinite;   animation: spin 2s linear infinite; } @-webkit-keyframes spin {  0%  {  -webkit-transform: rotate(0deg);  }  100% {  -webkit-transform: rotate(360deg); }}@keyframes spin {  0% { transform: rotate(0deg); }  100% { transform: rotate(360deg); }} body { background-color: #1451AE; color: #ffffff; font-size: 20px; font-weight: bold; margin: 0 0 0 0.0; padding: 0.4em 0.4em 0.4em 0.6em;}   #msgfmt { font-size: 16px; font-weight: normal;}#status { font-size: 16px;  font-weight: normal;}</style></head><center><br><br><br><br><br><p id=\"status\"><div id='loader'></div><br>Rebooting</p></center></html>";
  webServer.setContentLength(htmStr.length());
  webServer.send(200, "text/html", htmStr);
  delay(1000);
  ESP.restart();
}


void handleConfigHtml()
{
  String htmStr = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Config Editor</title><style type=\"text/css\">body {    background-color: #1451AE; color: #ffffff; font-size: 14px;  font-weight: bold;    margin: 0 0 0 0.0;    padding: 0.4em 0.4em 0.4em 0.6em;}  input[type=\"submit\"]:hover {     background: #ffffff;    color: green; }input[type=\"submit\"]:active {     outline-color: green;    color: green;    background: #ffffff; }table {    font-family: arial, sans-serif;     border-collapse: collapse;}td, th {border: 1px solid #dddddd;     text-align: left;    padding: 8px;}</style></head><body><form action=\"/config.html\" method=\"post\"><center><table><tr><td>SSID:</td><td><input name=\"ap_ssid\" value=\"" + AP_SSID + "\"></td></tr><tr><td>PASSWORD:</td><td><input name=\"ap_pass\" value=\"" + AP_PASS + "\"></td></tr><tr><td>WEBSERVER IP:</td><td><input name=\"web_ip\" value=\"" + Server_IP.toString() + "\"></td></tr><tr><td>WEBSERVER PORT:</td><td><input name=\"web_port\" value=\"" + String(WEB_PORT) + "\"></td></tr><tr><td>SUBNET MASK:</td><td><input name=\"subnet\" value=\"" + Subnet_Mask.toString() + "\"></td></tr></table><br><input id=\"savecfg\" type=\"submit\" value=\"Save Config\"></center></form></body></html>";
  webServer.setContentLength(htmStr.length());
  webServer.send(200, "text/html", htmStr);
}


void handleUpdateHtml()
{
  String htmStr = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Firmware Update</title><style type=\"text/css\">#loader {  z-index: 1;  width: 50px;  height: 50px;  margin: 0 0 0 0;  border: 6px solid #f3f3f3;  border-radius: 50%;  border-top: 6px solid #3498db;  width: 50px;  height: 50px;  -webkit-animation: spin 2s linear infinite;  animation: spin 2s linear infinite;}@-webkit-keyframes spin {  0% { -webkit-transform: rotate(0deg); }  100% { -webkit-transform: rotate(360deg); }}@keyframes spin {  0% { transform: rotate(0deg); }  100% { transform: rotate(360deg); }}body {    background-color: #1451AE; color: #ffffff; font-size: 20px;  font-weight: bold;    margin: 0 0 0 0.0;    padding: 0.4em 0.4em 0.4em 0.6em;}  input[type=\"submit\"]:hover {     background: #ffffff;    color: green; }input[type=\"submit\"]:active {     outline-color: green;    color: green;    background: #ffffff; }input[type=\"button\"]:hover {     background: #ffffff;    color: #000000; }input[type=\"button\"]:active {     outline-color: #000000;    color: #000000;    background: #ffffff; }#selfile {  font-size: 16px;  font-weight: normal;}#status {  font-size: 16px;  font-weight: normal;}</style><script>function formatBytes(bytes) {  if(bytes == 0) return '0 Bytes';  var k = 1024,  dm = 2,  sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB', 'PB', 'EB', 'ZB', 'YB'],  i = Math.floor(Math.log(bytes) / Math.log(k));  return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];}function statusUpl() {  document.getElementById(\"upload\").style.display=\"none\";  document.getElementById(\"btnsel\").style.display=\"none\";  document.getElementById(\"status\").innerHTML = \"<div id='loader'></div><br>Uploading firmware file...\";  setTimeout(statusUpd, 5000);}function statusUpd() {  document.getElementById(\"status\").innerHTML = \"<div id='loader'></div><br>Updating firmware, Please wait.\";}function FileSelected(e){  file = document.getElementById('fwfile').files[document.getElementById('fwfile').files.length - 1];  if (file.name.toLowerCase() == \"fwupdate.bin\")  {  var b = file.slice(0, 1);  var r = new FileReader();  r.onloadend = function(e) {  if (e.target.readyState === FileReader.DONE) {  var mb = new Uint8Array(e.target.result);   if (parseInt(mb[0]) == 233)  {  document.getElementById(\"selfile\").innerHTML =  \"File: \" + file.name + \"<br>Size: \" + formatBytes(file.size) + \"<br>Magic byte: 0x\" + parseInt(mb[0]).toString(16).toUpperCase();   document.getElementById(\"upload\").style.display=\"block\"; } else  {  document.getElementById(\"selfile\").innerHTML =  \"<font color='#df3840'><b>Invalid firmware file</b></font><br><br>Magic byte is wrong<br>Expected: 0xE9<br>Found: 0x\" + parseInt(mb[0]).toString(16).toUpperCase();     document.getElementById(\"upload\").style.display=\"none\";  }    }    };    r.readAsArrayBuffer(b);  }  else  {    document.getElementById(\"selfile\").innerHTML =  \"<font color='#df3840'><b>Invalid firmware file</b></font><br><br>File should be fwupdate.bin\";    document.getElementById(\"upload\").style.display=\"none\";  }}</script></head><body><center><form action=\"/update.html\" enctype=\"multipart/form-data\" method=\"post\"><p>Firmware Updater<br><br></p><p><input id=\"btnsel\" type=\"button\" onclick=\"document.getElementById('fwfile').click()\" value=\"Select file\" style=\"display: block;\"><p id=\"selfile\"></p><input id=\"fwfile\" type=\"file\" name=\"fwupdate\" size=\"0\" accept=\".bin\" onChange=\"FileSelected();\" style=\"width:0; height:0;\"></p><div><p id=\"status\"></p><input id=\"upload\" type=\"submit\" value=\"Update Firmware\" onClick=\"statusUpl();\" style=\"display: none;\"></div></form><center></body></html>";
  webServer.setContentLength(htmStr.length());
  webServer.send(200, "text/html", htmStr);
}


void handleUploadHtml()
{
  String htmStr = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>File Upload</title><style type=\"text/css\">#loader {  z-index: 1;  width: 50px;  height: 50px;  margin: 0 0 0 0;  border: 6px solid #f3f3f3;  border-radius: 50%;  border-top: 6px solid #3498db;  width: 50px;  height: 50px;  -webkit-animation: spin 2s linear infinite;  animation: spin 2s linear infinite;}@-webkit-keyframes spin {  0% { -webkit-transform: rotate(0deg); }  100% { -webkit-transform: rotate(360deg); }}@keyframes spin {  0% { transform: rotate(0deg); }  100% { transform: rotate(360deg); }}body {    background-color: #1451AE; color: #ffffff; font-size: 20px;  font-weight: bold;    margin: 0 0 0 0.0;    padding: 0.4em 0.4em 0.4em 0.6em;}  input[type=\"submit\"]:hover {     background: #ffffff;    color: green; }input[type=\"submit\"]:active {     outline-color: green;    color: green;    background: #ffffff;  } input[type=\"button\"]:hover {     background: #ffffff;    color: #000000; }input[type=\"button\"]:active {     outline-color: #000000;    color: #000000;    background: #ffffff; }#selfile {  font-size: 16px;  font-weight: normal;}#status {  font-size: 16px;  font-weight: normal;}</style><script>function formatBytes(bytes) {  if(bytes == 0) return '0 Bytes';  var k = 1024,  dm = 2,  sizes = ['Bytes', 'KB', 'MB', 'GB', 'TB', 'PB', 'EB', 'ZB', 'YB'],  i = Math.floor(Math.log(bytes) / Math.log(k));  return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];}function statusUpl() {  document.getElementById(\"upload\").style.display=\"none\";  document.getElementById(\"btnsel\").style.display=\"none\";  document.getElementById(\"status\").innerHTML = \"<div id='loader'></div><br>Uploading files\";}function FileSelected(e){  var strdisp = \"\";  var file = document.getElementById(\"upfiles\").files;  for (var i = 0; i < file.length; i++)  {   strdisp = strdisp + file[i].name + \" - \" + formatBytes(file[i].size) + \"<br>\";  }  document.getElementById(\"selfile\").innerHTML = strdisp;  document.getElementById(\"upload\").style.display=\"block\";}</script></head><body><center><form action=\"/upload.html\" enctype=\"multipart/form-data\" method=\"post\"><p>File Uploader<br><br></p><p><input id=\"btnsel\" type=\"button\" onclick=\"document.getElementById('upfiles').click()\" value=\"Select files\" style=\"display: block;\"><p id=\"selfile\"></p><input id=\"upfiles\" type=\"file\" name=\"fwupdate\" size=\"0\" onChange=\"FileSelected();\" style=\"width:0; height:0;\" multiple></p><div><p id=\"status\"></p><input id=\"upload\" type=\"submit\" value=\"Upload Files\" onClick=\"statusUpl();\" style=\"display: none;\"></div></form><center></body></html>";
  webServer.setContentLength(htmStr.length());
  webServer.send(200, "text/html", htmStr);
}


void handleFormatHtml()
{
  String htmStr = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Storage Format</title><style type=\"text/css\">#loader {  z-index: 1;  width: 50px;  height: 50px;  margin: 0 0 0 0;  border: 6px solid #f3f3f3;  border-radius: 50%;  border-top: 6px solid #3498db;  width: 50px;  height: 50px;  -webkit-animation: spin 2s linear infinite;  animation: spin 2s linear infinite;}@-webkit-keyframes spin {  0% { -webkit-transform: rotate(0deg); }  100% { -webkit-transform: rotate(360deg); }}@keyframes spin {  0% { transform: rotate(0deg); }  100% { transform: rotate(360deg); }}body {    background-color: #1451AE; color: #ffffff; font-size: 20px;  font-weight: bold;    margin: 0 0 0 0.0;    padding: 0.4em 0.4em 0.4em 0.6em;}  input[type=\"submit\"]:hover {     background: #ffffff;    color: green; }input[type=\"submit\"]:active {     outline-color: green;    color: green;    background: #ffffff; }#msgfmt { font-size: 16px;  font-weight: normal;}#status {  font-size: 16px;  font-weight: normal;}</style><script>function statusFmt() { var answer = confirm(\"Are you sure you want to format?\");  if (answer) {   document.getElementById(\"format\").style.display=\"none\";   document.getElementById(\"status\").innerHTML = \"<div id='loader'></div><br>Formatting Storage\";   return true;  }  else {   return false;  }}</script></head><body><center><form action=\"/format.html\" method=\"post\"><p>Storage Format<br><br></p><p id=\"msgfmt\">This will delete all the files on the server</p><div><p id=\"status\"></p><input id=\"format\" type=\"submit\" value=\"Format Storage\" onClick=\"return statusFmt();\" style=\"display: block;\"></div></form><center></body></html>";
  webServer.setContentLength(htmStr.length());
  webServer.send(200, "text/html", htmStr);
}


void handleAdminHtml()
{
  String htmStr = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Admin Panel</title><style>body {    background-color: #1451AE; color: #ffffff; font-size: 14px;  font-weight: bold;    margin: 0 0 0 0.0;    padding: 0.4em 0.4em 0.4em 0.6em;}.sidenav {    width: 140px;    position: fixed;    z-index: 1;    top: 20px;    left: 10px;    background: #6495ED;    overflow-x: hidden;    padding: 8px 0;}.sidenav a {    padding: 6px 8px 6px 16px;    text-decoration: none;    font-size: 14px;    color: #ffffff;    display: block;}.sidenav a:hover {    color: #1451AE;}.main {    margin-left: 150px;     padding: 10px 10px; position: absolute;   top: 0;   right: 0; bottom: 0;  left: 0;}</style></head><body><div class=\"sidenav\"><a href=\"/index.html\" target=\"mframe\">Main Page</a><a href=\"/info.html\" target=\"mframe\">ESP Information</a><a href=\"/fileman.html\" target=\"mframe\">File Manager</a><a href=\"/upload.html\" target=\"mframe\">File Uploader</a><a href=\"/update.html\" target=\"mframe\">Firmware Update</a><a href=\"/config.html\" target=\"mframe\">Config Editor</a><a href=\"/format.html\" target=\"mframe\">Storage Format</a><a href=\"/reboot.html\" target=\"mframe\">Reboot ESP</a></div><div class=\"main\"><iframe src=\"info.html\" name=\"mframe\" height=\"100%\" width=\"100%\" frameborder=\"0\"></iframe></div></table></body></html> ";
  webServer.setContentLength(htmStr.length());
  webServer.send(200, "text/html", htmStr);
}


void handleConsoleUpdate(String rgn)
{
  String Version = "05.050.000";
  String sVersion = "05.050.000";
  String lblVersion = "5.05";
  String imgSize = "0";
  String imgPath = "";
  String xmlStr = "<?xml version=\"1.0\" ?><update_data_list><region id=\"" + rgn + "\"><force_update><system level0_system_ex_version=\"0\" level0_system_version=\"" + Version + "\" level1_system_ex_version=\"0\" level1_system_version=\"" + Version + "\"/></force_update><system_pup ex_version=\"0\" label=\"" + lblVersion + "\" sdk_version=\"" + sVersion + "\" version=\"" + Version + "\"><update_data update_type=\"full\"><image size=\"" + imgSize + "\">" + imgPath + "</image></update_data></system_pup><recovery_pup type=\"default\"><system_pup ex_version=\"0\" label=\"" + lblVersion + "\" sdk_version=\"" + sVersion + "\" version=\"" + Version + "\"/><image size=\"" + imgSize + "\">" + imgPath + "</image></recovery_pup></region></update_data_list>";
  webServer.setContentLength(xmlStr.length());
  webServer.send(200, "text/xml", xmlStr);
}


void handleRebootHtml()
{
  String htmStr = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>ESP Reboot</title><style type=\"text/css\">#loader {  z-index: 1;   width: 50px;   height: 50px;   margin: 0 0 0 0;   border: 6px solid #f3f3f3;   border-radius: 50%;   border-top: 6px solid #3498db;   width: 50px;   height: 50px;   -webkit-animation: spin 2s linear infinite;   animation: spin 2s linear infinite; } @-webkit-keyframes spin {  0%  {  -webkit-transform: rotate(0deg);  }  100% {  -webkit-transform: rotate(360deg); }}@keyframes spin {  0% { transform: rotate(0deg); }  100% { transform: rotate(360deg); }} body { background-color: #1451AE; color: #ffffff; font-size: 20px; font-weight: bold; margin: 0 0 0 0.0; padding: 0.4em 0.4em 0.4em 0.6em;}   input[type=\"submit\"]:hover { background: #ffffff; color: green; }input[type=\"submit\"]:active { outline-color: green; color: green; background: #ffffff; } #msgfmt { font-size: 16px; font-weight: normal;}#status { font-size: 16px;  font-weight: normal;} </style><script>function statusRbt() { var answer = confirm(\"Are you sure you want to reboot?\");  if (answer) {document.getElementById(\"reboot\").style.display=\"none\";   document.getElementById(\"status\").innerHTML = \"<div id='loader'></div><br>Rebooting ESP Board\"; return true;  }else {   return false;  }}</script></head><body><center><form action=\"/reboot.html\" method=\"post\"><p>ESP Reboot<br><br></p><p id=\"msgrbt\">This will reboot the esp board</p><div><p id=\"status\"></p><input id=\"reboot\" type=\"submit\" value=\"Reboot ESP\" onClick=\"return statusRbt();\" style=\"display: block;\"></div></form><center></body></html>";
  webServer.setContentLength(htmStr.length());
  webServer.send(200, "text/html", htmStr);
}


void handleInfo()
{
  FSInfo fs_info;
  SPIFFS.info(fs_info);
  float flashFreq = (float)ESP.getFlashChipSpeed() / 1000.0 / 1000.0;
  FlashMode_t ideMode = ESP.getFlashChipMode();
  float supplyVoltage = (float)ESP.getVcc()/ 1000.0 ;
  String output = "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>System Information</title><style type=\"text/css\">body { background-color: #1451AE;color: #ffffff;font-size: 14px;font-weight: bold; margin: 0 0 0 0.0; padding: 0.4em 0.4em 0.4em 0.6em;}</style></head>";
  output += "<hr>###### Software ######<br><br>";
  output += "Firmware version " + firmwareVer + "<br>";
  output += "Core version: " + String(ESP.getCoreVersion()) + "<br>";
  output += "SDK version: " + String(ESP.getSdkVersion()) + "<br>";
  output += "Boot version: " + String(ESP.getBootVersion()) + "<br>";
  output += "Boot mode: " + String(ESP.getBootMode()) + "<br>";
  output += "Chip Id: " + String(ESP.getChipId()) + "<br><hr>";
  output += "###### CPU ######<br><br>";
  output += "CPU frequency: " + String(ESP.getCpuFreqMHz()) + "MHz<br><hr>";
  output += "###### Flash chip information ######<br><br>";
  output += "Flash chip Id: " +  String(ESP.getFlashChipId()) + "<br>";
  output += "Estimated Flash size: " + formatBytes(ESP.getFlashChipSize()) + "<br>";
  output += "Actual Flash size based on chip Id: " + formatBytes(ESP.getFlashChipRealSize()) + "<br>";
  output += "Flash frequency: " + String(flashFreq) + " MHz<br>";
  output += "Flash write mode: " + String((ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN")) + "<br><hr>";
  output += "###### File system (SPIFFS) ######<br><br>"; 
  output += "Total space: " + formatBytes(fs_info.totalBytes) + "<br>";
  output += "Used space: " + formatBytes(fs_info.usedBytes) + "<br>";
  output += "Block size: " + String(fs_info.blockSize) + "<br>";
  output += "Page size: " + String(fs_info.pageSize) + "<br>";
  output += "Maximum open files: " + String(fs_info.maxOpenFiles) + "<br>";
  output += "Maximum path length: " +  String(fs_info.maxPathLength) + "<br><hr>";
  output += "###### Sketch information ######<br><br>";
  output += "Sketch hash: " + ESP.getSketchMD5() + "<br>";
  output += "Sketch size: " +  formatBytes(ESP.getSketchSize()) + "<br>";
  output += "Free space available: " +  formatBytes(ESP.getFreeSketchSpace()) + "<br><hr>";
  output += "###### power ######<br><br>";
  output += "Supply voltage: " +  String(supplyVoltage) + " V<br><hr>";
  output += "</html>";
  webServer.setContentLength(output.length());
  webServer.send(200, "text/html", output);
}


void writeConfig()
{
  File iniFile = SPIFFS.open("/config.ini", "w");
  if (iniFile) {
  iniFile.print("\r\nSSID=" + AP_SSID + "\r\nPASSWORD=" + AP_PASS + "\r\n\r\nWEBSERVER_IP=" + Server_IP.toString() + "\r\nWEBSERVER_PORT=" + String(WEB_PORT) + "\r\n\r\nSUBNET_MASK=" + Subnet_Mask.toString() + "\r\n");
  iniFile.close();
  }
}


void setup(void) 
{
  //Serial.begin(115200);
  //Serial.setDebugOutput(true);
  //Serial.println("Version: " + firmwareVer);
  if (SPIFFS.begin()) {
  if (SPIFFS.exists("/config.ini")) {
  File iniFile = SPIFFS.open("/config.ini", "r");
  if (iniFile) {
  String iniData;
    while (iniFile.available()) {
      char chnk = iniFile.read();
      iniData += chnk;
    }
   iniFile.close();
   
   if(instr(iniData,"SSID="))
   {
   AP_SSID = split(iniData,"SSID=","\r\n");
   AP_SSID.trim();
   }
   
   if(instr(iniData,"PASSWORD="))
   {
   AP_PASS = split(iniData,"PASSWORD=","\r\n");
   AP_PASS.trim();
   }
   
   if(instr(iniData,"WEBSERVER_PORT="))
   {
   String strWprt = split(iniData,"WEBSERVER_PORT=","\r\n");
   strWprt.trim();
   WEB_PORT = strWprt.toInt();
   }
   
   if(instr(iniData,"WEBSERVER_IP="))
   {
    String strwIp = split(iniData,"WEBSERVER_IP=","\r\n");
    strwIp.trim();
    Server_IP.fromString(strwIp);
   }

   if(instr(iniData,"SUBNET_MASK="))
   {
    String strsIp = split(iniData,"SUBNET_MASK=","\r\n");
    strsIp.trim();
    Subnet_Mask.fromString(strsIp);
   }
    }
  }
  else
  {
   writeConfig(); 
  }
  }
  else
  {
    //Serial.println("No SPIFFS");
  }

  //Serial.println("SSID: " + AP_SSID);
  //Serial.println("Password: " + AP_PASS);
  //Serial.print("\n");
  //Serial.println("WEB Server IP: " + Server_IP.toString());
  //Serial.println("Subnet: " + Subnet_Mask.toString());
  //Serial.println("WEB Server Port: " + String(WEB_PORT));
  //Serial.println("DNS Server IP: " + Server_IP.toString());
  //Serial.print("\n\n");

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(Server_IP, Server_IP, Subnet_Mask);
  WiFi.softAP(AP_SSID.c_str(), AP_PASS.c_str());
  //Serial.println("WIFI AP started");

  dnsServer.setTTL(30);
  dnsServer.setErrorReplyCode(DNSReplyCode::ServerFailure);
  dnsServer.start(53, "*", Server_IP);
  //Serial.println("DNS server started");

  webServer.onNotFound(handleNotFound);
  webServer.on("/update.html", HTTP_POST, []() {} ,handleFwUpdate);
  webServer.on("/update.html", HTTP_GET, handleUpdateHtml);
  webServer.on("/upload.html", HTTP_POST, []() {
  webServer.sendHeader("Location","/fileman.html");
  webServer.send(302, "text/html", "");
  }, handleFileUpload);
  webServer.on("/upload.html", HTTP_GET, handleUploadHtml);
  webServer.on("/format.html", HTTP_GET, handleFormatHtml);
  webServer.on("/format.html", HTTP_POST, handleFormat);
  webServer.on("/fileman.html", HTTP_GET, handleFileMan);
  webServer.on("/info.html", HTTP_GET, handleInfo);
  webServer.on("/delete", HTTP_POST, handleDelete);
  webServer.on("/config.html", HTTP_GET, handleConfigHtml);
  webServer.on("/config.html", HTTP_POST, handleConfig);
  webServer.on("/admin.html", HTTP_GET, handleAdminHtml);
  webServer.on("/reboot.html", HTTP_GET, handleRebootHtml);
  webServer.on("/reboot.html", HTTP_POST, handleReboot);
  webServer.begin(WEB_PORT);
  //Serial.println("HTTP server started");
}


void loop(void) {
  dnsServer.processNextRequest();
  webServer.handleClient();
}
