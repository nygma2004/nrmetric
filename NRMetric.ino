#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>           // MQTT support

#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <Adafruit_NeoPixel.h>
//#include <Fonts/Picopixel.h>
#include <Fonts/TomThumb.h>
#include <WiFiUdp.h>                // Added for NTP functionality
#include <Time.h>
#include <TimeLib.h>
#include <sunMoon.h>                // Sunrise, sunset calculation
#include "NTP.h"
#include "icons.h"

#define PINMATRIX 14

extern "C" {
#include "user_interface.h"
}


  

// Update the below parameters for your project
// Also check NTP.h for some parameters as well
const char* ssid = "xxx";
const char* password = "xxx";
const char* mqtt_server = "192.168.1.xxx"; 
const char* mqtt_user = "xxx";
const char* mqtt_password = "xxx";
const char* clientID = "nrmetric";
const char* topicStatus = "/nrmetric/status";
const char* topicUpdateRequest = "/nrmetric/update";

byte page_gfx[40]     ={ 25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25,25};
byte page_color[40]   ={  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
byte page_duration[40]={255, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5};
String page_text[40]   ={"empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty","empty"};
byte currentPage = 0;
byte transitionStep = 0;
bool pageChange = false;
byte maxPage = 0;
byte pageID = 0;
String NTPtime = "--:--";
char msg[50];
String mqttStat = "";
String message = "";
unsigned long lastTick, uptime, lastNTP, seconds;
int brightnessAnalog = 0;

os_timer_t myTimer;
MDNSResponder mdns;
ESP8266WebServer server(80);
WiFiClient espClient;
PubSubClient mqtt(mqtt_server, 1883, 0, espClient);

// MATRIX DECLARATION:
// Parameter 1 = width of NeoPixel matrix
// Parameter 2 = height of matrix
// Parameter 3 = pin number (most are valid)
// Parameter 4 = matrix layout flags, add together as needed:
//   NEO_MATRIX_TOP, NEO_MATRIX_BOTTOM, NEO_MATRIX_LEFT, NEO_MATRIX_RIGHT:
//     Position of the FIRST LED in the matrix; pick two, e.g.
//     NEO_MATRIX_TOP + NEO_MATRIX_LEFT for the top-left corner.
//   NEO_MATRIX_ROWS, NEO_MATRIX_COLUMNS: LEDs are arranged in horizontal
//     rows or in vertical columns, respectively; pick one or the other.
//   NEO_MATRIX_PROGRESSIVE, NEO_MATRIX_ZIGZAG: all rows/columns proceed
//     in the same order, or alternate lines reverse direction; pick one.
//   See example below for these values in action.
// Parameter 5 = pixel type flags, add together as needed:
//   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
//   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
//   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
//   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)


Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(32,8, PINMATRIX,
  NEO_MATRIX_BOTTOM    + NEO_MATRIX_RIGHT +
  NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG,
  NEO_GRB            + NEO_KHZ800);

// 0: white, 1: red, 2: green, 3: yellow, 4: blue, 5: violet, 6: torquise, 7: brown, 8: black
uint16_t colors[] = {
  matrix.Color(255, 255, 255), matrix.Color(255, 0, 0), matrix.Color(0, 255, 0), matrix.Color(255, 210, 0),matrix.Color(0, 0, 255), matrix.Color(255, 0, 255), matrix.Color(0, 255, 255), matrix.Color(91, 68, 43),matrix.Color(0, 0, 0)};


// This routing just puts the status string together which will be sent over MQTT
void refreshStats() {
  // Initialize the strings for MQTT 
  mqttStat = "{\"rssi\":";
  mqttStat += WiFi.RSSI();
  mqttStat += ",\"uptime\":";
  mqttStat += uptime;
  mqttStat += ",\"brightness\":";
  mqttStat += brightnessAnalog;
  mqttStat += "}";
}

void renderTime() {
  // Construct the current time string
  if (NTPUpdateMillis>0) {
    NTPtime  = "";
    unsigned long epoch2 = epoch+((millis()-NTPUpdateMillis) / 1000);
    if (((epoch2 % 86400L) / 3600)<10) {
      // add leading zero if hour is less than 10
      NTPtime  += "0";
    }
    NTPtime  += (epoch2 % 86400L) / 3600; // print the hour (86400 equals secs per day)
    NTPtime  += ":";
    if ( ((epoch2 % 3600) / 60) < 10 ) {
      // In the first 10 minutes of each hour, we'll want a leading '0'
      NTPtime  += "0";
    }
    NTPtime  += (epoch2  % 3600) / 60; // print the minute (3600 equals secs per minute)
  } else {
    NTPtime = "--:--";
  }  
}

// This is the 1 second timer callback function
uint8_t sec=0;
void timerCallback(void *pArg) {
  sec++;
  seconds++;
  if (seconds==10) {
    // Send MQTT update
    refreshStats();
    if (mqtt_server!="") {
      mqtt.publish(topicStatus, mqttStat.c_str());
      Serial.print(F("Status: "));
      Serial.println(mqttStat);
    }
    renderTime();
    seconds = 0;
  }
}


// MQTT reconnect logic
void reconnect() {
  //String mytopic;
  // Loop until we're reconnected
  while (!mqtt.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (mqtt.connect(clientID, mqtt_user, mqtt_password)) {
      Serial.println(F("connected"));
      // ... and resubscribe
      //mqtt.subscribe(inTopic);
    } else {
      Serial.print(F("failed, rc="));
      Serial.print(mqtt.state());
      Serial.println(F(" try again in 5 seconds"));
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

// HTTP request handle for the page update
void handleUpdateCommand() { 
  message = "";
  pageID = 0;
  if (server.args() == 0) {
    message = "No parameter";
  } else {
    if (server.hasArg("maxpage")) {
      maxPage = server.arg("maxpage").toInt(); 
      message += "maxPage set | ";
      //Serial.print(" maxPage: ");
      //Serial.print(maxPage);
    }
    if (server.hasArg("id")) {
      pageID = server.arg("id").toInt(); 
      message += "updating page | ";
      //Serial.print(" ID: ");
      //Serial.print(pageID);
    
      if (server.hasArg("gfx")) {
        page_gfx[pageID] = server.arg("gfx").toInt(); 
        message += "gfx set | ";
        //Serial.print(" gfx: ");
        //Serial.print(page_gfx[pageID]);
      }
      if (server.hasArg("color")) {
        page_color[pageID] = server.arg("color").toInt(); 
        message += "color set | ";
        //Serial.print(" color: ");
        //Serial.print(page_color[pageID]);
      }
      if (server.hasArg("duration")) {
        page_duration[pageID] = server.arg("duration").toInt(); 
        message += "duration set | ";
        //Serial.print(" duration: ");
        //Serial.print(page_duration[pageID]);
      }
      if (server.hasArg("text")) {
        page_text[pageID] = server.arg("text"); 
        message += "text set | ";
        //Serial.print(" text: ");
        //Serial.print(page_text[pageID]);
      }
    }
    if (message == "" ) {
      message = "invalid call";
    }
  }
  server.send(200, "text/plain", message);          //Returns the HTTP response
  Serial.println(F("Page update received"));
}

// Setup section of the sketch
void setup() {
  Serial.begin(115200);
  Serial.println("");  
  uptime = 0;
  sec = 0;
  seconds = 0;
  currentPage = 0; // Set this to page 0 temporarily
  Serial.println(F("Initializing display"));  
  matrix.begin();
  matrix.setTextWrap(false);
  matrix.setFont(&TomThumb);
  matrix.setBrightness(30);
  matrix.setTextColor(colors[0]);
  // draw icon
  matrix.drawRGBBitmap(0, 0,  RGB_bmp[0], 8,8);
  matrix.show();

  Serial.print(F("Connecting to Wifi"));
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
    seconds++;
    if (seconds>180) {
      // reboot the ESP if cannot connect to wifi
      ESP.restart();
    }
  }
  seconds = 0;
  Serial.println("");
  Serial.print(F("Connected to "));
  Serial.println(ssid);
  Serial.print(F("IP address: "));
  Serial.println(WiFi.localIP());
  Serial.print(F("Signal [RSSI]: "));
  Serial.println(WiFi.RSSI());

  // draw icon
  matrix.drawRGBBitmap(8, 0,  RGB_bmp[2], 8,8);
  matrix.show();

  os_timer_setfn(&myTimer, timerCallback, NULL);
  os_timer_arm(&myTimer, 1000, true);


  // Set up the MDNS and the HTTP server
  if (mdns.begin("NRMetric", WiFi.localIP())) {
    Serial.println(F("MDNS responder started"));
  }  
  server.on("/", [](){                        // Dummy page
    server.send(200, "text/plain", "NRMetric");
  });
  server.on("/update", handleUpdateCommand);        // Handling page update
  server.begin();
  Serial.println(F("HTTP server started"));


  // draw icon
  matrix.drawRGBBitmap(16, 0,  RGB_bmp[1], 8,8);
  matrix.show();
  
  // Set up the MQTT server connection
  if (mqtt_server!="") {
    mqtt.setServer(mqtt_server, 1883);
    mqtt.setCallback(callback);
  }

  // draw icon
  matrix.drawRGBBitmap(24, 0,  RGB_bmp[3], 8,8);
  matrix.show();
  // Start UDP for NTP function
  Serial.println(F("Starting UDP"));
  udp.begin(localPort);
  Serial.print(F("Local port: "));
  Serial.println(udp.localPort());
  requestNTPUpdate();
  // Initialize sunrise and sunset calculator
  sm.init(GMTOffset, sm_latitude, sm_longtitude);  

  // Update the screen brightness
  BrightnessCheck(); 
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Convert the incoming byte array to a string
  String mytopic = (char*)topic;
  payload[length] = '\0'; // Null terminator used to terminate the char array
  String message = (char*)payload;

  Serial.print(F("Message arrived on topic: ["));
  Serial.print(topic);
  Serial.print(F("], "));
  Serial.println(message);

  if(mytopic.indexOf("max")!=-1) {
    maxPage = message.toInt();
    Serial.print(F("New maxpage: "));
    Serial.println(maxPage);
  }
}

void handleDisplay() {
  // Handle page transitions and general screen update
  if (currentPage==0xff) {
    if (pageChange) {
      // Transitioning in the time
      Serial.println(F("Time >>IN"));
      for (transitionStep=0;transitionStep<=8;transitionStep++)
      {
        matrix.fillScreen(0);
        matrix.setTextColor(colors[0]);
        matrix.setFont();
        matrix.setCursor(1, transitionStep-8);
        matrix.print(NTPtime);
        matrix.show();
        yield();
        delay(40);
      }  
      pageChange = false;   
      sec = 0;
      // Request a new update    
      if (mqtt_server!="") {
        mqtt.publish(topicUpdateRequest, WiFi.localIP().toString().c_str());
        Serial.println(F("Update request sent..."));
      }
    } else {
      // Update the display in case the minute has changed
        //matrix.fillScreen(0);
        //matrix.setTextColor(colors[0]]);
        //matrix.setFont();
        //matrix.setCursor(1, 0);
        //matrix.print(NTPtime);
        //matrix.show();  
        // Check if the page expired    
        if (sec>10) {
          // time to transition out he current screen
          Serial.println(F("Time >>OUT"));
          for (transitionStep=0;transitionStep<=8;transitionStep++)
          {
            matrix.fillScreen(0);
            matrix.setFont();
            matrix.setTextColor(colors[0]);
            matrix.setCursor(1, transitionStep);
            matrix.print(NTPtime);
            matrix.show();
            yield();
            delay(40);
          }   
          currentPage = 0;
          pageChange = true;
          sec = 0; 
        }

    }
  }
  
  if (pageChange) {
    // Transitioning in the new page
    Serial.print(F("Page "));
    Serial.print(currentPage);
    Serial.println(F(" >>IN"));
    for (transitionStep=0;transitionStep<=8;transitionStep++)
    {
      matrix.fillScreen(0);
      //matrix.drawBitmap(0, 8-transitionStep,  twitter, 8,8,colors[0]);
      matrix.drawRGBBitmap(0, 8-transitionStep,  RGB_bmp[page_gfx[currentPage]], 8,8);
      matrix.setFont(&TomThumb);
      matrix.setTextColor(colors[page_color[currentPage]]);
      matrix.setCursor(9, transitionStep-8+6);
      matrix.print(page_text[currentPage]);
      matrix.show();
      yield();
      delay(40);
    }   
    // turn off the transition
    pageChange = false;
    // reset the counter
    sec = 0; 
    // Update the screen brightness
    BrightnessCheck(); 
  }

  if ((!pageChange)&&(sec>page_duration[currentPage])) {
    // time to transition out he current screen
    Serial.print(F("Page "));
    Serial.print(currentPage);
    Serial.println(F(" >>OUT"));
    for (transitionStep=0;transitionStep<=8;transitionStep++)
    {
      matrix.fillScreen(0);
      //matrix.drawBitmap(0, 0-transitionStep,  twitter, 8,8,colors[0]);
      matrix.drawRGBBitmap(0, 0-transitionStep,  RGB_bmp[page_gfx[currentPage]], 8,8);
      matrix.setFont(&TomThumb);
      matrix.setTextColor(colors[page_color[currentPage]]);
      matrix.setCursor(9, transitionStep+6);
      matrix.print(page_text[currentPage]);
      matrix.show();
      yield();
      delay(40);
    }   
    // go to the next page
    if (currentPage>=maxPage) {
      currentPage = 0xff;
    } else {
      currentPage++;
    }
    // Set the flag to transition in the new page
    pageChange=true;    
    sec = 0; 
  }
  
}

void BrightnessCheck() {
  // Check for the photoresistor and set the brightness level
  brightnessAnalog = analogRead(0);
  if (brightnessAnalog<100) {
    matrix.setBrightness(10);
  } else if(brightnessAnalog<400) {
    matrix.setBrightness(20);
  } else if(brightnessAnalog>400) {
    matrix.setBrightness(40);
  }  
}

void handleNTPResponse() {
  // Check for NTP response
  int cb = udp.parsePacket();
  if (cb!=0) {
    if (NTPUpdateMillis == 0) {
      // This is only true after boot
      currentPage = 0xff; // set the clock page
      pageChange = true; // transition in the clock page
    }
    NTPUpdateMillis = millis();
    NTPRequested = false;
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer
    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    const unsigned long seventyYears = 2208988800UL;
    epoch = secsSince1900 - seventyYears;
    if (summerTime(epoch)) {
      epoch += 3600; // add 1 hour DST
    }
    epoch += GMTOffset * 60;
    Serial.print(F("NTP Update (epoch): "));
    Serial.println(epoch);
    Serial.print(F("NTP Update (time): "));
    printDate(epoch);  Serial.println("");

    // Sunrise and Sunset calculations
    time_t sRise = sm.sunRise(epoch);
    time_t sSet  = sm.sunSet(epoch);
    if (summerTime(epoch)) {
      sRise += 3600; // add 1 hour DST
      sSet += 3600; // add 1 hour DST
    }
    renderTime();
    Serial.print(F("Today sunrise and sunset: "));
    printDate(sRise); Serial.print(F("; "));
    printDate(sSet);  Serial.println("");
  }  
}

void loop() {

  // Handle HTTP server requests
  server.handleClient();

  // Handle MQTT connection/reconnection
  if (mqtt_server!="") {
    if (!mqtt.connected()) {
      reconnect();
    }
    mqtt.loop();
  }

  handleDisplay();

  // Uptime calculation
  if (millis() - lastTick >= 60000) {            
    lastTick = millis();            
    uptime++;            
  }    

  // Check if NTP update is due
  if ((millis() - NTPUpdateMillis >= 60*60*1000) && (!NTPRequested)) {  
    requestNTPUpdate();
  }    

  handleNTPResponse();
}







