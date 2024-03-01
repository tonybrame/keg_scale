#include "HX711.h"
//#include "soc/rtc.h"
#include <stdlib.h>
#include <Wire.h>
#include "WiFi.h"
#include <HTTPClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>

#define DEBUG_OUTPUT 1
  
const int MAX_SCALES = 4;
int outPins[4] = { 19, 17, 25, 32 };
int clockPins[4] = { 18, 16, 26, 33 };
//int outPins[1] = { 25 };
//int clockPins[1] = { 26 };
HX711 scales[MAX_SCALES];


float currentReading[4] = { 0.0, 0.0, 0.0, 0.0 };

 
int rbutton = 18; // this button will be used to reset the scale to 0. Not used.
const float calibration_factor = 10000; // seems to get me lbs right on
const float notifyVariance = 0.1; //only send notifications after a weight change of this scale - accuracy could suffer since I'm not doing constant updates +/- .1 lb. 


const char* ssid = "Brame Network_2GEXT";
const char* password = "w1f1extp@55";

//Your Domain name with URL path or IP address with path
//tap.tonybrame.com
const char* serverName = "http://192.168.5.99/rpints/api/scale_data.php";

//set up a server to listen for tare instructions... 
const int IPaddress = 98; // 192.168.5.IPaddress 
const int PORT = 3005; // 192.168.5.IPaddress 

AsyncWebServer server(PORT);
AsyncWebSocket ws("/tare");

//wifi reconnect variables
unsigned long previousMillis = 0;
unsigned long interval = 30000;

//adding preferences to track the tare values across reboots. 
Preferences prefs;

 
void setup() {


  //initialize preferences - storing tare values for reboots and debug logging
  prefs.begin("scale-data", false); 

  int32_t previousDebug = 0;
#if DEBUG_OUTPUT
  delay(250);
  //write out previous debugLog point
  previousDebug = prefs.getInt("debugPoint", 0);
  delay(250);
#endif
  
  debugLog(1);
  
#if DEBUG_OUTPUT
  //I had to add these delays, if it isnt' there, it stalls for a connection. 
  delay(500);
  Serial.begin(115200);
  delay(500);
#endif

  serialPrintLine("Start Init");
  //serialPrintLine("Start Init"); 
  debugLog(2);
  
  serialPrint("Previous Run Debug Point: ");
  serialPrintLine(previousDebug);   
  
  // Set WiFi to station mode and disconnect from an AP if it was previously connected
  WiFi.mode(WIFI_STA);
  //this was on a sample, but it was potentially causing issues, and seems unecessary. removing for troubleshooting
//  WiFi.disconnect();
  
  debugLog(3);

  IPAddress local_IP(192, 168, 5, IPaddress);
  IPAddress gateway(192, 168, 5,  1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress primaryDNS(192, 168, 5, 1);
  IPAddress secondaryDNS(192, 168, 5, 1);   
  
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    debugLog(5);
    serialPrintLine("static address failed to configure");
  }

  debugLog(6);
  serialPrintLine("Connecting"); 
  WiFi.begin(ssid, password);
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    serialPrint(".");
  }
  debugLog(7);
  
  serialPrintLine("");
  serialPrint("Connected to WiFi network with IP Address: ");
  serialPrintLine(WiFi.localIP());
  
  debugLog(8);

  // Start server
  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.begin();
  
  debugLog(9);
  
  serialPrint("Listening on port ");
  serialPrintLine(PORT);
  
  
  for(int i = 0; i < MAX_SCALES; i++)
  {
    debugLog(10+i);
  
    scales[i].begin(outPins[i], clockPins[i]);
    scales[i].set_scale();
//    scales[i].tare(); //Reset the scale to 0 - used for testing, now tare is sent via web sockets.

    //load the existing tare data. If not there, use default factor.
    String myscale = String("scale_") + String(i+1);

    float storedScale = prefs.getFloat((myscale + String("_scale")).c_str(), 0);
    long storedOffset = prefs.getLong((myscale + String("_offset")).c_str(), 0);

    if(storedScale != 0 && storedOffset != 0)
    {
      scales[i].set_scale(storedScale);
      scales[i].set_offset(storedOffset);
    }
    else
      scales[i].set_scale(calibration_factor);
    
    //I don't think this is necessary.. 
    scales[i].read_average(); //Get a baseline reading
  }
  
  //rtc_clk_cpu_freq_set(RTC_CPU_FREQ_80M);
  
  serialPrintLine("Completed setup"); 

  debugLog(20);
 
 
}

void serialPrint(const char * message)
{  
#if DEBUG_OUTPUT  
  Serial.print(message);
#endif
}
void serialPrintLine(IPAddress message)
{  
#if DEBUG_OUTPUT  
  Serial.println(message);
#endif
}
void serialPrintLine(int message)
{  
#if DEBUG_OUTPUT  
  Serial.println(message);
#endif
}
void serialPrintLine(const char * message)
{  
#if DEBUG_OUTPUT  
  Serial.println(message);
#endif
}
void serialPrintLine(String & message)
{  
#if DEBUG_OUTPUT  
  Serial.println(message);
#endif
}

void loop() {
   
  ws.cleanupClients();
  
  float weight = 0.0;
  
  String string1, string2; 
  String cmessage; // complete message
  char buff[10];
  
  for(int i = 0; i < MAX_SCALES; i++)
  {
    if(scales[i].is_ready())
    {
      weight = scales[i].get_units(5);
      
      if(weight > (currentReading[i] + notifyVariance) || 
        weight < (currentReading[i] - notifyVariance))
      {
        currentReading[i] = weight;
        
        string1 = String(weight, 3);
        string2 = String(i+1);//show as 1 based index
        cmessage = cmessage + "Weight Scale " + string2 + ": " + string1 + " Lb"; 
        serialPrintLine(cmessage); 

        sendScaleWeight(string2, string1);
      }
    }
  }

  processTareCheck(); 

  reconnectWifi();

  delay(500);
  
}

void debugLog(int32_t debugPoint)
{
#if DEBUG_OUTPUT
    serialPrint("Debug: ");
    serialPrintLine(debugPoint);
    delay(250);
    prefs.putInt("debugPoint", debugPoint);
    delay(250);
#endif
}

void reconnectWifi()
{

  unsigned long currentMillis = millis();
  // if WiFi is down, try reconnecting every CHECK_WIFI_TIME seconds
  if ((WiFi.status() != WL_CONNECTED) && (currentMillis - previousMillis >=interval)) {
    //serialPrint(millis());
    serialPrintLine("Reconnecting to WiFi...");
    WiFi.disconnect();
    WiFi.reconnect();
    previousMillis = currentMillis;
    debugLog(30);
  }
  
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) {
  AwsFrameInfo *info = (AwsFrameInfo*)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {
    data[len] = 0;
    String myscale = (char*)data;
    serialPrint("Got Message: ");
    serialPrintLine(myscale);
    
    int scaleNumber = myscale.toInt();
    if(scaleNumber > 0 && scaleNumber <= MAX_SCALES){
      //tare that scale!
      scales[scaleNumber-1].tare(); //Reset the scale to 0
      serialPrintLine("Tare weight set on scale");

      //store the scale data.
      float current_scale = scales[scaleNumber-1].get_scale();
      long current_offset = scales[scaleNumber-1].get_offset();

      String myscale = String("scale_") + String(scaleNumber);

      prefs.putLong((myscale + String("_offset")).c_str(), current_offset);
      prefs.putFloat((myscale + String("_scale")).c_str(), current_scale);
      
    }
    else        
      serialPrintLine("Invalid scale number");
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type,
             void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
#if DEBUG_OUTPUT
      Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
#endif
      break;
    case WS_EVT_DISCONNECT:
#if DEBUG_OUTPUT
      Serial.printf("WebSocket client #%u disconnected\n", client->id());
#endif
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len);
      break;
    case WS_EVT_PONG:
    case WS_EVT_ERROR:
      break;
  }
}

//DEPRECATED: now uses web sockets - this was a TCP socket implementation
//listening on PORT, check to see if something has sent a request to tare a scale. It will just contain the 1 based index for the scale to tare... 
void processTareCheck()
{

  
  /* ArduinoWebsockets.h
   * 
  serialPrintLine("pre-accept?");
  auto client = socketServer.accept();
  serialPrintLine("post-accept?");
  
  if(client.available()) {
    auto msg = client.readBlocking();

    // log
    serialPrint("Got Message: ");
    String myscale = msg.data();
    serialPrintLine(myscale);

    int scaleNumber = myscale.toInt();
    if(scaleNumber > 0 && scaleNumber <= MAX_SCALES){
      //tare that scale!
      scales[scaleNumber-1].tare(); //Reset the scale to 0
      serialPrintLine("Tare weight set on scale");
    }
    else        
      serialPrintLine("Invalid scale number");
        
    // close the connection
    client.close();
  }
  
  serialPrintLine("after if");*/
  /*
   * TCP Socket
   * 
  WiFiClient client = wifiServer.available();
 
  if (client) {
 
    while (client.connected()) {
 
      serialPrintLine("Client connected");
      String myscale = "";
      while (client.available()>0) {
        char c = client.read();
        myscale += c;
      }
      serialPrint("Received: ");
      serialPrintLine(myscale);
      
      int scaleNumber = myscale.toInt();
      if(scaleNumber > 0 && scaleNumber <= MAX_SCALES){
        //tare that scale!
        scales[scaleNumber-1].tare(); //Reset the scale to 0
        serialPrintLine("Tare weight set on scale");
      }
      else        
        serialPrintLine("Invalid scale number");
 
      delay(10);
    }
 
    client.stop();
    serialPrintLine("Client disconnected");
 
  }*/
}

void sendScaleWeight(String scaleID, String lbs)
{
    if(WiFi.status()== WL_CONNECTED){
      WiFiClient client;
      HTTPClient http;
      
      http.begin(serverName);
      
      http.addHeader("Content-Type", "application/x-www-form-urlencoded");
      
      String httpRequestData = "api_key=ckXfQi9CPAb3BzVGt&scale_id=" + scaleID + "&weight=" + lbs;
      
      // Send HTTP POST request
      int httpResponseCode = http.POST(httpRequestData);

      if(httpResponseCode != 200){
        serialPrint("Error from server: ");
        serialPrintLine(httpResponseCode);
        
        debugLog(1000 + httpResponseCode);
      }
      else{
        serialPrintLine("Data sent successfully.");
      }
      
    }
    else{
      serialPrintLine("Wifi not connected, unable to send");
    }
}
 
 
