/*
  Wordclock Code
*/

/*
 * TOODO:
 * - update time regulary
 * - use ldr
 * -- option to set min/max brightness
 * -- option to set ldr threshhold
 * - display numbers
 * -- display sensor data
 * -- display day/month/yeahr
 * - set custom single pixel colors
 * - add update functionality
 * - use WebServer
 */

// WiFi
#include <WiFi.h>

// Update
#include <Update.h>

// mDNS
#include <ESPmDNS.h>

// DHT
//#include "DHT.h"

// Time
#include "sys/time.h"

// NeoPixel
#include <Adafruit_NeoPixel.h>

// Prefences
#include <Preferences.h>
Preferences preferences;

// Pins
#define LED_PIN     2
#define NP_DATA_PIN 12
#define LDR_PIN     32 //use pins 32 and above when WiFi is ON
#define DHT_PIN     13

// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS      114

// When we setup the NeoPixel library, we tell it how many pixels, and which pin to use to send signals.
// Note that for older NeoPixel strips you might need to change the third parameter--see the strandtest
// example for more information on possible values.
Adafruit_NeoPixel pixels(NUMPIXELS, NP_DATA_PIN, NEO_GRB + NEO_KHZ800);

// DHT
//#define DHTTYPE DHT11   // DHT 11
#define DHTTYPE DHT22   // DHT 22  (AM2302), AM2321
//#define DHTTYPE DHT21   // DHT 21 (AM2301)

// Connect pin 1 (on the left) of the sensor to +5V
// NOTE: If using a board with 3.3V logic like an Arduino Due connect pin 1
// to 3.3V instead of 5V!
// Connect pin 2 of the sensor to whatever your DHTPIN is
// Connect pin 4 (on the right) of the sensor to GROUND
// Connect a 10K resistor from pin 2 (data) to pin 1 (power) of the sensor

// Initialize DHT sensor.
// Note that older versions of this library took an optional third parameter to
// tweak the timings for faster processors.  This parameter is no longer needed
// as the current DHT reading algorithm adjusts itself to work on faster procs.
//DHT dht(DHT_PIN, DHTTYPE);

// Time
#define TZ          1       // (utc+) TZ in hours
#define DST_MN      0//60      // use 60mn for summer time in some countries

#define TZ_MN       ((TZ)*60)
#define TZ_SEC      ((TZ)*3600)
#define DST_SEC     ((DST_MN)*60)
const char* TZ_INFO="CET-1CEST-2,M3.5.0/02:00:00,M10.5.0/03:00:00";

// LED Color
uint32_t led_color = pixels.Color(255, 255, 255);
uint32_t led_black = pixels.Color(0, 0, 0);

// Sensor data
float brightness = 1.0;
float humidity = 20.0;
float temperature = 20.0;

// ClockData
boolean refresh_disp =false;
int current_sec = 0;
int current_minute = 0;
int current_hour = 0;
int current_day = 0;
int current_month = 0;
int current_year = 0;
boolean manual_time=false;

// Set web server port number to 80
WiFiServer server(80);
boolean server_flag =false;
boolean wifi_flag =false;

int server_timeout = 300;
int server_last_active = 0;

// Update
// S3 Bucket Config
WiFiClient client;
String host = "xpard.de"; // Host => bucket-name.s3.region.amazonaws.com
int port = 80; // Non https. For HTTPS 443. As of today, HTTPS doesn't work.
String bin = "/clock_two.ino.esp32.bin"; // bin file name with a slash in front.
// Variables to validate
// response from S3
long contentLength = 0;
bool isValidContentType = false;
// Utility to extract header value from headers
String getHeaderValue(String header, String headerName) {
  return header.substring(strlen(headerName.c_str()));
}

// Variable to store the HTTP request
String header;
boolean post = false;
String hmtl_color = "";

// Variables to store WiFi data
String ssid = "";
String pass = "";
IPAddress sta_ip;

// time
String text_time = "";

// WiFi connection timout
int timeout_sec = 10;
boolean wifi_connected = false;
String client_message = "";

// AP Info
const char *ap_ssid = "ClockTwo";
const char *ap_password = "12345678";
IPAddress ap_ip;

// All LEDS
int temp[2] = {0, 110};

// pshow delay (ms)
int pshow_delay = 50;

// brightness_threshold (%)
int brightness_min = 1;
int brightness_threshold = 10;

// Logging
String log_level[4] = {"DEBUG","INFO","WARNING","ERROR"};
int logging = 0;
boolean last_q = false;
String temp_log[50];

void setup() {
  Serial.begin(115200);
  delay(10);

  logger(1, __func__, __LINE__, "http://192.168.2.108/update");

  initNeo();

  load_settings();
  
  logger(1, __func__, __LINE__, "Setting ESP mode to APSTA");
  WiFi.mode(WIFI_MODE_APSTA);

  startAP();

  connectWiFi();
  
  startMDNS();
  
  updateTime(1514764800);
   
  logger(1, __func__, __LINE__, "Starting WebServer...");
  server_flag =true;
  server.begin();

  logger(1, __func__, __LINE__, "Adding mDNS Service to port 80... ");
  MDNS.addService("http", "tcp", 80);

  logger(1, __func__, __LINE__, "NOT Starting DHT sensor... ");
  // dht.begin();

  refresh_disp = true;
}

int color =0;

void loop(){
  if(server_flag){
    server_logic();
  }
  update();
}

const int all_leds[2] = {0,114};

const int w_words[9][2] = {
  {99,2}, // es
  {102,3}, // ist
  {74, 3}, // vor
  {70,4}, // funk
  {66,4}, // nach
  {55,4}, // halb
  {48,2}, // am
  {37,2}, // pm
  {0, 3} // uhr
};

const int w_fives[6][2] = {
  {77, 4}, // drei
  {81, 4}, // vier
  {106, 4},// fünf
  {95, 4},// zehn
  {85,3},// ((drei)vier)tel
  {88,7} // zwanzig
};

const int w_hours[12][2] = {
  { 17,  5 },  // zwölf
  { 51, 4}, // eins
  { 44, 4}, // zwei
  { 33, 4 }, // drei
  { 40, 4 }, // vier
  { 62, 4}, // fünf
  { 28,  5 }, // sechs
  { 11, 6 }, // sieben
  { 22, 4 }, // acht
  { 4, 4 }, // neun
  { 7, 4 }, // zehn
  { 60, 3 } // elf
};

const int p_minutes[5][2] = {
  { 110, 0 },  // 0
  { 110, 1 },  // 1
  { 110, 2 }, // 2
  { 110, 3 }, // 3
  { 110, 4 } // 4
};

void update(){
  
  
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    logger(3, __func__, __LINE__, "Failed to obtain time");
    return;
  }
  
  if(timeinfo.tm_sec!=current_sec){
    current_sec=timeinfo.tm_sec;
    
    // second
    
    updateBrightness();

    checkServerTimeout();
    
    if(timeinfo.tm_min!=current_minute or refresh_disp){
      current_minute = timeinfo.tm_min;

      // minute
      
      logger(1, __func__, __LINE__, "DISPLAY UPDATE");
      
      int ad_minute = current_minute%5;
    
      if(ad_minute==0 or refresh_disp){

        refresh_disp = false;
    
        logger(1, __func__, __LINE__, "DISPLAY UPDATE WORDS");
    
        // diable/clear all leds
        //setPixelsColorByInterval(led_black, all_leds);
        pixels.clear();
        
    
        int five = (float)current_minute/(float)5;
        logger(1, __func__, __LINE__, String(five));
        
        logger(1, __func__, __LINE__, "ES IST ");
        setPixelsColorByInterval(led_color, w_words[0]);
        setPixelsColorByInterval(led_color, w_words[1]);
        
    
        if(five==1 or five==5 or five == 7 or five ==11){
          logger(1, __func__, __LINE__, "FÜNF ");
          setPixelsColorByInterval(led_color, w_fives[2]);
        }
        else if(five==2 or five ==10){
          logger(1, __func__, __LINE__, "ZEHN ");
          setPixelsColorByInterval(led_color, w_fives[3]);
        }
        else if(five==3 or five ==9){
          logger(1, __func__, __LINE__, "VIERTEL ");
          setPixelsColorByInterval(led_color, w_fives[1]);
          setPixelsColorByInterval(led_color, w_fives[4]);
        }
        else if(five==4 or five ==8){
          logger(1, __func__, __LINE__, "ZWANZIG ");
          setPixelsColorByInterval(led_color, w_fives[5]);
        }
        if(five>=1 and five <=4 or five == 7){
          logger(1, __func__, __LINE__, "NACH ");
          setPixelsColorByInterval(led_color, w_words[4]);
        }
        if(five==5 or five>=8){
          logger(1, __func__, __LINE__, "VOR ");
          setPixelsColorByInterval(led_color, w_words[2]);
        }
        if(five>=5 and five<=7){
          logger(1, __func__, __LINE__, "HALB ");
          setPixelsColorByInterval(led_color, w_words[5]);
        }
        if(five>=5){
          logger(1, __func__, __LINE__, String((timeinfo.tm_hour+1)%12));
          setPixelsColorByInterval(led_color, w_hours[(timeinfo.tm_hour+1)%12]);
        }else{
          logger(1, __func__, __LINE__, String(timeinfo.tm_hour%12));
          setPixelsColorByInterval(led_color, w_hours[timeinfo.tm_hour%12]);
        }
        
        
        if(five==0){
          logger(1, __func__, __LINE__, "UHR");
          setPixelsColorByInterval(led_color, w_words[8]);
        }
      }
      
      logger(1, __func__, __LINE__, " +"+String(ad_minute)+" Minuten");
      setPixelsColorByInterval(led_color, p_minutes[ad_minute]);

      logger(1, __func__, __LINE__, getFormattedLocalTime());
      
      if(timeinfo.tm_hour!=current_hour){
        current_hour = timeinfo.tm_hour;
        if(timeinfo.tm_mday!=current_day){
          current_day = timeinfo.tm_mday;
          if(timeinfo.tm_mon!=current_month){
            current_month = timeinfo.tm_mon;
            if(timeinfo.tm_year!=current_year){
              current_year = timeinfo.tm_year;
              // year
            } 
            // month
          } 
          // day
        }
        // hour
      }
      
      
      pshow();
    }
    
  }
}

void checkServerTimeout(){
  // check for server timeout
  if(server_flag or wifi_flag){
    time_t now;
    time(&now);
    if(now>server_last_active+server_timeout){
      // if abs delta is too large assume delayed time init/wrong start timestamp
      if(abs(now-server_last_active)>server_timeout+60){
        logger(2, __func__, __LINE__, "Detected invalid timestamp, correcting ...");
        server_last_active = now;
      }else{
        logger(2, __func__, __LINE__, "WiFi timout reached!");
        disableWiFi();
      }
    }
  }
}

void server_logic(){
  
  WiFiClient client = server.available();                 // Listen for incoming clients

  if (client) {                                           // If a new client connects,
    time_t now;
    time(&now);
    server_last_active = now;
    logger(1, __func__, __LINE__, "New Client.");         // print a message out in the serial port
    String currentLine = "";                              // make a String to hold incoming data from the client
    while (client.connected()) {                          // loop while the client's connected
      if (client.available()) {                           // if there's bytes to read from the client,
        char c = client.read();                           // read a byte, then
        loggerQ(0, __func__, __LINE__, String(c));                                  // print it out the serial monitor
        header += c;
        if (c == '\n' or post and !client.available()) {  // if the byte is a newline character
          // if the current line is blank, you got two newline characters in a row.
          // There might be POST data if this request is a POST request:
          if (currentLine.length() == 0 and !post and header.indexOf("POST /") >= 0) {
            currentLine = "";
            post = true;
            logger(0, __func__, __LINE__, ", POST Request detected continuing parsing...");
          // If not a POST request that's the end of the client HTTP request, so send a response:
          } else if (currentLine.length() == 0 or post and !client.available()) {
            // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
            // and a content-type so the client knows what's coming, then a blank line:
            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();
            
            if (header.indexOf("GET /wifi") >= 0 or header.indexOf("POST /wifi") >= 0) {
              int ssid_start = 0;
              int ssid_end = 0;
              int pass_start = 0;
              int pass_end = 0;
              
              if (header.indexOf("GET /wifi") >= 0){
                logger(0, __func__, __LINE__, "GET /wifi");
                ssid_start = header.indexOf("?ssid=")+6;
                ssid_end = header.indexOf("&pass=");
                pass_start = ssid_end+6;
                pass_end = header.indexOf(" HTTP/");
                pass = header.substring(pass_start,pass_end);
              }else{
                logger(0, __func__, __LINE__, "POST /wifi");
                ssid_start = header.indexOf("ssid=")+5;
                ssid_end = header.indexOf("&pass=");
                pass_start = ssid_end+6;
                pass = header.substring(pass_start);
              }
              ssid = header.substring(ssid_start,ssid_end);
              
              logger(0, __func__, __LINE__, "Recieved WiFi credentials: ");
              logger(1, __func__, __LINE__, ssid);

              save_settings(ssid, pass, led_color);
              
              connectWiFi();

              startMDNS();

              updateTime(1514764800);

              refresh_disp = true;
            }

            if (header.indexOf("GET /color") >= 0 or header.indexOf("POST /color") >= 0) {
              int color_start = 0;
              int color_end = 0;
              
              color_start = header.indexOf("color=")+6;
              if (header.indexOf("GET /color") >= 0){
                color_end = header.indexOf(" HTTP/");
                logger(0, __func__, __LINE__, "GET /color");
                hmtl_color = header.substring(color_start,color_end);
              }else{
                logger(0, __func__, __LINE__, "POST /color");
                hmtl_color = header.substring(color_start);
              }

              logger(0, __func__, __LINE__, hmtl_color);

              saveFromHTMLColor(hmtl_color);
              
              save_settings(ssid, pass, led_color);
              
              refresh_disp = true;

            }

            if (header.indexOf("GET /time") >= 0 or header.indexOf("POST /time") >= 0) {
              int time_start = 0;
              int time_end = 0;
              
              time_start = header.indexOf("time=")+5;
              if (header.indexOf("GET /time") >= 0){
                time_end = header.indexOf(" HTTP/");
                logger(0, __func__, __LINE__, "GET /time");
                text_time = header.substring(time_start,time_end);
              }else{
                logger(0, __func__, __LINE__, "POST /time");
                text_time = header.substring(time_start);
              }

              setTime(text_time.toInt());

              if(text_time.toInt()<=0){
                manual_time = false;

                connectWiFi();

                startMDNS();
    
                updateTime(1514764800);
    
                refresh_disp = true;
                
              }else{
                manual_time = true;

                client_message = getFormattedLocalTime();
              }
              
              refresh_disp = true;

            }

            if (header.indexOf("GET /update") >= 0 or header.indexOf("POST /update") >= 0) {

              // Display the HTML web page
              client.println("<!DOCTYPE html><html>");
              client.println("<head>");
              client.println("<meta http-equiv=\"refresh\" content=\"60; url=/\" />");
              client.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
              client.println("<link rel=\"icon\" href=\"data:,\">");
              // CSS to style the on/off buttons 
              // Feel free to change the background-color and font-size attributes to fit your preferences
              client.println("<style>");
              client.println("html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
              //client.println(".button { background-color: #4CAF50; border: none; color: white; padding: 16px 40px;");
              //client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
              //client.println(".button2 {background-color: #555555;}");
              client.println("</style></head>");
              
              // Web Page Heading
              client.println("<body><h1>Updating ...</h1>");
              client.println("<hr>");
              client.println("This may take a while (page will refresh in 1 Minute) ...");
              client.println("</body></html>");
              
              // The HTTP response ends with another blank line
              client.println();

              // Stopping connection (will restart anyway)
              client.stop();
              logger(2, __func__, __LINE__, "Client disconnected.");
  
              logger(0, __func__, __LINE__, "Update Response sent. Continuing...");
              // Break out of the while loop
              
              
              connectWiFi();

              connectWiFi();

              while (WiFi.status() != WL_CONNECTED) {
                setPixelsColorByInterval(led_black, all_leds);
                pshow();
                delay(500);
                logger(1, __func__, __LINE__, "FUNK");
                setPixelsColorByInterval(pixels.Color(255,0,0), w_words[4]);
                pshow();
              }
              setPixelsColorByInterval(pixels.Color(255,255,0), w_words[4]);
              pshow();

              execOTA();

              setPixelsColorByInterval(pixels.Color(0,255,255), w_words[4]);
              pshow();
              
            }else{
            
              // Display the HTML web page
              client.println("<!DOCTYPE html><html>");
              client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
              client.println("<link rel=\"icon\" href=\"data:,\">");
              // CSS to style the on/off buttons 
              // Feel free to change the background-color and font-size attributes to fit your preferences
              client.println("<style>");
              client.println("html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
              //client.println(".button { background-color: #4CAF50; border: none; color: white; padding: 16px 40px;");
              //client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
              //client.println(".button2 {background-color: #555555;}");
              client.println("</style></head>");
              
              // Web Page Heading
              client.println("<body><h1>ClockThirtyTwo Web Server</h1>");
  
              client.println("<hr>");
  
              if (!wifi_connected){
                client.println("WiFi connection not successful!<br>");
              }else{
                client.println("WiFi connection established to: "+ssid+"<br>");
              }
              //logger(0, __func__, __LINE__, hmtl_color);
              //String tmp_color = "#"+&hmtl_color[3];
              String tmp_color = "#"+colorToHexString();
              logger(0, __func__, __LINE__, tmp_color);
              client.println("<br>");
              client.println("<div style='background-color:"+tmp_color+"; text-align:center;'><p style='color:"+tmp_color+"; -webkit-filter: invert(100%); filter: invert(100%);'>"+getFormattedLocalTime()+"<br>Color: "+tmp_color+"</p></div>");
              client.println("<br>");
              
              client.println("<form action='/wifi' method='post'>");
              client.println("WIFI-SSID: <input type='text' name='ssid'><br>");
              client.println("WIFI-Password: <input type='text' name='pass'><br>");
              client.println("<button>Save</button>");
              client.println("</form>");
              client.println("<br>");
              client.println("<form action='/color' method='post'>");
              
              client.println("Select you color: <input type='color' name='color' value='"+tmp_color+"'><br>");
              client.println("<button>Save</button>");
              client.println("</form>");
              client.println("<br>");
              client.println("<form action='/time' method='post'>");
              client.println("Use timestamp to set time: <input id='dateid' type='number' name='time' value=''><script>document.getElementById('dateid').value = Math.ceil(Date.now()/1000)+60*60*2;</script><br>");
              client.println("<button>Save</button>");
              client.println("</form>");
              client.println("<br>");
              client.println("<form action='/update' method='get'>");
              client.println("<button>UPDATE</button>");
              client.println("(beta)");
              client.println("</form>");
              
              client.println("<hr>");
              client.println(client_message);
              client.println("<hr>");
              client.println(getHTMLLog());
              
              client_message="";
              
              client.println("</body></html>");
              
              // The HTTP response ends with another blank line
              client.println();
  
              logger(0, __func__, __LINE__, "Response sent. Continuing...");
              // Break out of the while loop
            }
            break;
          } else { // if you got a newline, then clear currentLine
            currentLine = "";
            //logger(0, __func__, __LINE__, "Response contained newline setting empty currentLine...");
          }
        //if (c == '\n') {
        } else if (c != '\r') {  // if you got anything else but a carriage return character,
          currentLine += c;      // add it to the end of the currentLine
          //Serial.println("Response does not contain \\r, adding response to header...");
        } else {
           //logger(0, __func__, __LINE__, "Response contains \\r");
        }
      } else {
        // client not available
        time_t now;
        time(&now);
        if(server_last_active+10<now){
          logger(2, __func__, __LINE__, "client not available timeout reached");
          break;
        }
      }
    }
    // Clear the header variable
    header = "";

    // reset post request flag
    post = false;
    
    // Close the connection
    client.stop();
    logger(2, __func__, __LINE__, "Client disconnected.");
  }
  
}

void saveFromHTMLColor(String hexstring){
  logger(1, __func__, __LINE__, &hexstring[3]);
  long long number = strtoll( &hexstring[3], NULL, 16);

  // Split them up into r, g, b values
  
  long long r = number >> 16;
  long long g = number >> 8 & 0xFF;
  long long b = number & 0xFF;
  logger(0, __func__, __LINE__, "DEBUG: "+String((int)r)+","+String((int)g)+","+String((int)b));

  led_color = pixels.Color(r, g, b);
}

String colorToHexString(){
  //other way round
  //uint32_t color = red<<16 | green<<8 | blue;
  uint8_t red = (led_color>>16) & 255;
  uint8_t green = (led_color>>8) & 255;
  uint8_t blue = led_color & 255;
  /*
  logger(0, __func__, __LINE__, "r:"+String(red)+", g:"+String(green)+", b:"+String(blue));
  char r[2];
  String(red, HEX).toCharArray(r, 2);
  char g[2];
  String(red, HEX).toCharArray(g, 2);
  char b[2];
  String(red, HEX).toCharArray(b, 2);
  */
  return intToHex( red )+intToHex( green )+intToHex( blue );
}

String intToHex( uint8_t col ){
  String result = String(col, HEX);
  if(result.length()==1){
    result = "0"+result;
  }
  return result;
}

void initNeo(){
  logger(1, __func__, __LINE__, "Initializing the NeoPixels...");
  pixels.begin(); // This initializes the NeoPixel library.
  pixels.show();

  logger(0, __func__, __LINE__, "Resetting NeoPixels...");
  setPixelsColorByInterval(led_black, temp);
  pshow();
}

void startAP(){
  logger(1, __func__, __LINE__, "Starting AP: ");
  logger(1, __func__, __LINE__, ap_ssid);
  WiFi.softAP(ap_ssid,ap_password);
  delay(100);
  //IPAddress Ip(1, 2, 3, 4);
  //IPAddress NMask(255, 255, 255, 0);
  //WiFi.softAPConfig(Ip, Ip, NMask);
  ap_ip = WiFi.softAPIP();
  logger(1, __func__, __LINE__, "AP IP address: ");
  logger(1, __func__, __LINE__, String(ap_ip[0])+String(ap_ip[1])+String(ap_ip[2])+String(ap_ip[3]));
}

void connectWiFi(){
  logger(1, __func__, __LINE__, "Connecting to WiFi: ");
  logger(1, __func__, __LINE__, ssid);
  WiFi.begin(ssid.c_str(), pass.c_str());

  logger(0, __func__, __LINE__, "Timeout: ");
  logger(0, __func__, __LINE__, String(timeout_sec));
  
  int timeout_timer = 0;
  int timeout_interval = 500;
  while (WiFi.status() != WL_CONNECTED and (float)((float)timeout_timer*(float)((float)timeout_interval/1000.0))<timeout_sec) {
    delay(timeout_interval);
    loggerQ(1, __func__, __LINE__, String(((float)((float)timeout_timer*(float)((float)timeout_interval/1000.0)))/timeout_sec*100));
    loggerQ(1, __func__, __LINE__, "%, ");
    timeout_timer++;
  }

  if(WiFi.status() != WL_CONNECTED){
    logger(2, __func__, __LINE__, "ERROR!");
    logger(3, __func__, __LINE__, "Could not connect to WiFi!");
    wifi_connected=false;
  }else{
    // Print local IP address
    logger(1, __func__, __LINE__, "Success!");
    loggerQ(1, __func__, __LINE__, "WiFi connected. WiFi IP address: ");
    sta_ip = WiFi.localIP();
    logger(1, __func__, __LINE__, String(sta_ip[0])+String(sta_ip[1])+String(sta_ip[2])+String(sta_ip[3]));
    wifi_connected=true;
  }
}

void startMDNS(){
  logger(1, __func__, __LINE__, "Starting mDNS");
  if (!MDNS.begin("clocktwo")) {
    logger(3, __func__, __LINE__, "Error setting up MDNS responder!");
    while(1) {
      delay(1000);
    }
  }else{
    logger(1, __func__, __LINE__, "mDNS responder started");
  }
}

void updateTime(int fallback){
  if(!wifi_connected and server_last_active==0){
    logger(2, __func__, __LINE__, "OFFLINE! Setting timestamp: ");
    logger(2, __func__, __LINE__, String(fallback));
    setTime(fallback);
  }else{
    loggerQ(1, __func__, __LINE__, "Using time zone");
    logger(1, __func__, __LINE__, String(TZ_INFO));
    loggerQ(1, __func__, __LINE__, "Getting time from: ");
    logger(1, __func__, __LINE__, "pool.ntp.org");
    configTime(TZ_SEC, DST_SEC, "pool.ntp.org");
    setenv("TZ", TZ_INFO, 1);
  }
}

void disableWiFi(){
  logger(1, __func__, __LINE__, "Stopping WebServer...");
  server_flag =false;
  logger(2, __func__, __LINE__, "Disconnecting WiFi...");
  WiFi.disconnect(true);
  logger(2, __func__, __LINE__, "Turning of WiFi...");
  wifi_flag =false;
  WiFi.mode(WIFI_OFF);
}

void connectInternet(){
  WiFi.mode(WIFI_STA);
  time_t now;
  time(&now);
  server_last_active = now;
  wifi_flag =true;
  connectWiFi();
}

String getFormattedLocalTime()
{
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    logger(3, __func__, __LINE__, "Failed to obtain time!");
    return "";
  }
  char timeStringBuff[50]; //50 chars should be enough
  strftime(timeStringBuff, sizeof(timeStringBuff), "%A, %B %d %Y %H:%M:%S", &timeinfo);
  return timeStringBuff;
}

void setTime(int timestamp){
  //ESP.eraseConfig();
  time_t rtc = timestamp;
  timeval tv = { rtc, 0 };
  timezone tz = { TZ_MN + DST_MN, 0 };
  settimeofday(&tv, &tz);
}

void updateBrightness(){
  /*
  The full-scale voltage is the voltage corresponding to a maximum reading (depending on ADC1 configured bit width, this value is: 4095 for 12-bits, 2047 for 11-bits, 1023 for 10-bits, 511 for 9 bits.)
  */
  float value = analogRead(LDR_PIN);
  if(abs(brightness*100-(value/4095)*100)>=brightness_threshold){
    brightness = value/4095;
    if(brightness*100<=brightness_min){
      brightness=(float)brightness_min/100;
    }
    logger(1, __func__, __LINE__, "Brighness: "+String(brightness));
    pixels.setBrightness((int)(255*brightness));
    refresh_disp = true;
  }
}

void updateDHTdata(){
  Serial.println("Trying to update DHT sensor data...");
  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  //float h = dht.readHumidity();
  // Read temperature as Celsius (the default)
  //float t = dht.readTemperature();
  // Read temperature as Fahrenheit (isFahrenheit = true)
  //float f = dht.readTemperature(true);

  // Check if any reads failed and exit early (to try again).
  //if (isnan(h) || isnan(t) /*|| isnan(f)*/) {
  /*  Serial.println("Failed to read from DHT sensor!");
    // return;
  }else{
    humidity = h;
    temperature = t;
    Serial.println("Updated DHT sensor data!");
  }*/
}

void load_settings(){
  logger(1, __func__, __LINE__, "Loading settings (ssid,pass,color) ...");
  preferences.begin("my-app",false);
  ssid=preferences.getString("ssid");
  pass=preferences.getString("pass");
  led_color=preferences.getInt("led_color");
  preferences.end();
  logger(1, __func__, __LINE__, "WiFi credentials: ");
  logger(1, __func__, __LINE__, ssid);
  
}

void save_settings(String _ssid, String _pass, uint32_t _led_color){
  logger(1, __func__, __LINE__, "Saving WiFi credentials: "+_ssid);
  preferences.begin("my-app",false);
  preferences.putString("ssid", _ssid);
  preferences.putString("pass", _pass);
  preferences.putInt("led_color", _led_color);
  preferences.end();
}

void setPixelsColorByInterval(uint32_t color, const int inteval[]){
  // inteval[0] != start index
  // inteval[1] != length
  // initialization; condition; increment
  // pixels.show();
  for (int i = 0; i < inteval[1]; i++) {
    loggerQ(0, __func__, __LINE__, "led:"+String(inteval[0]+i)+",");
    pixels.setPixelColor(inteval[0]+i, color);
  }
}

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    loggerQ(0, __func__, __LINE__, String(255 - WheelPos * 3));
    loggerQ(0, __func__, __LINE__, ",");
    loggerQ(0, __func__, __LINE__, "0");
    loggerQ(0, __func__, __LINE__, ",");
    loggerQ(0, __func__, __LINE__, String(WheelPos * 3));
    return pixels.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    loggerQ(0, __func__, __LINE__, "0");
    loggerQ(0, __func__, __LINE__, ",");
    loggerQ(0, __func__, __LINE__, String(WheelPos * 3));
    loggerQ(0, __func__, __LINE__, ",");
    loggerQ(0, __func__, __LINE__, String(255 - WheelPos * 3));
    return pixels.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  loggerQ(0, __func__, __LINE__, String(WheelPos * 3));
  loggerQ(0, __func__, __LINE__, ",");
  loggerQ(0, __func__, __LINE__, String(255 - WheelPos * 3));
  loggerQ(0, __func__, __LINE__, ",");
  loggerQ(0, __func__, __LINE__, "0");
  return pixels.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

void logger(int level, String function, int line, String input){
  if(last_q){
    Serial.println("");
    last_q = false;
    addToLog(input, false);
  }
  if(level>=logging){
    Serial.println(log_level[level]+"|"+function+"|"+line+"|"+input);
    addToLog(log_level[level]+"|"+function+"|"+line+"|"+input, true);
  }
  last_q = false;
}

void loggerQ(int level, String function, int line, String input){
  if(level>=logging){
    if(last_q){
      Serial.print(input);
      addToLog(input, false);
    }else{
      Serial.print(log_level[level]+"|"+function+"|"+line+"|"+input);
      last_q = true;
      addToLog(log_level[level]+"|"+function+"|"+line+"|"+input, true);
    }
  }
}

void addToLog(String curentLine, boolean nl){
  if(nl){
    for (int i=0; i<(sizeof temp_log/sizeof temp_log[0])-1; i++) {
     temp_log[i]=temp_log[i+1];
    }
    temp_log[(sizeof temp_log/sizeof temp_log[0])-1] = curentLine;
  }else{
    temp_log[(sizeof temp_log/sizeof temp_log[0])-1] += curentLine;
  }
}
String getHTMLLog(){
  String result = "";
  for (int i=0; i<sizeof temp_log/sizeof temp_log[0]; i++) {
   result += temp_log[i]+"<br>";
  }
  return result;
}

void pshow(){
  pixels.show();
  delay(pshow_delay);
  pixels.show();
}

void execOTA() {
  logger(1, __func__, __LINE__, "Connecting to: " + String(host));
  // Connect to S3
  if (client.connect(host.c_str(), port)) {
    // Connection Succeed.
    // Fecthing the bin
    logger(1, __func__, __LINE__, "Fetching Bin: " + String(bin));

    // Get the contents of the bin file
    client.print(String("GET ") + bin + " HTTP/1.1\r\n" +
                 "Host: " + host + "\r\n" +
                 "Cache-Control: no-cache\r\n" +
                 "Connection: close\r\n\r\n");

    // Check what is being sent
    //    Serial.print(String("GET ") + bin + " HTTP/1.1\r\n" +
    //                 "Host: " + host + "\r\n" +
    //                 "Cache-Control: no-cache\r\n" +
    //                 "Connection: close\r\n\r\n");

    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 5000) {
        logger(3, __func__, __LINE__, "Client Timeout !");
        client.stop();
        return;
      }
    }
    // Once the response is available,
    // check stuff

    /*
       Response Structure
        HTTP/1.1 200 OK
        x-amz-id-2: NVKxnU1aIQMmpGKhSwpCBh8y2JPbak18QLIfE+OiUDOos+7UftZKjtCFqrwsGOZRN5Zee0jpTd0=
        x-amz-request-id: 2D56B47560B764EC
        Date: Wed, 14 Jun 2017 03:33:59 GMT
        Last-Modified: Fri, 02 Jun 2017 14:50:11 GMT
        ETag: "d2afebbaaebc38cd669ce36727152af9"
        Accept-Ranges: bytes
        Content-Type: application/octet-stream
        Content-Length: 357280
        Server: AmazonS3
                                   
        {{BIN FILE CONTENTS}}

    */
    while (client.available()) {
      // read line till /n
      String line = client.readStringUntil('\n');
      // remove space, to check if the line is end of headers
      line.trim();

      // if the the line is empty,
      // this is end of headers
      // break the while and feed the
      // remaining `client` to the
      // Update.writeStream();
      if (!line.length()) {
        //headers ended
        break; // and get the OTA started
      }

      // Check if the HTTP Response is 200
      // else break and Exit Update
      if (line.startsWith("HTTP/1.1")) {
        if (line.indexOf("200") < 0) {
          logger(3, __func__, __LINE__, "Got a non 200 status code from server. Exiting OTA Update.");
          break;
        }
      }

      // extract headers here
      // Start with content length
      if (line.startsWith("Content-Length: ")) {
        contentLength = atol((getHeaderValue(line, "Content-Length: ")).c_str());
        logger(1, __func__, __LINE__, "Got " + String(contentLength) + " bytes from server");
      }

      // Next, the content type
      if (line.startsWith("Content-Type: ")) {
        String contentType = getHeaderValue(line, "Content-Type: ");
        logger(1, __func__, __LINE__, "Got " + contentType + " payload.");
        if (contentType == "application/octet-stream") {
          isValidContentType = true;
        }
      }
    }
  } else {
    // Connect to S3 failed
    // May be try?
    // Probably a choppy network?
    logger(3, __func__, __LINE__, "Connection to " + String(host) + " failed. Please check your setup");
    // retry??
    // execOTA();
  }

  // Check what is the contentLength and if content type is `application/octet-stream`
  logger(1, __func__, __LINE__, "contentLength : " + String(contentLength) + ", isValidContentType : " + String(isValidContentType));

  // check contentLength and content type
  if (contentLength && isValidContentType) {
    // Check if there is enough to OTA Update
    bool canBegin = Update.begin(contentLength);

    // If yes, begin
    if (canBegin) {

      setPixelsColorByInterval(pixels.Color(0,255,0), w_words[4]);
      pshow();
      
      logger(1, __func__, __LINE__, "Begin OTA. This may take 2 - 5 mins to complete. Things might be quite for a while.. Patience!");
      // No activity would appear on the Serial monitor
      // So be patient. This may take 2 - 5mins to complete
      size_t written = Update.writeStream(client);

      if (written == contentLength) {
        logger(1, __func__, __LINE__, "Written : " + String(written) + " successfully");
      } else {
        logger(2, __func__, __LINE__, "Written only : " + String(written) + "/" + String(contentLength) + ". Retry?" );
        // retry??
        // execOTA();
      }

      if (Update.end()) {
        logger(1, __func__, __LINE__, "OTA done!");
        if (Update.isFinished()) {
          logger(1, __func__, __LINE__, "Update successfully completed. Rebooting.");
          setPixelsColorByInterval(pixels.Color(0,0,255), w_words[4]);
          pshow();
          ESP.restart();
        } else {
          logger(3, __func__, __LINE__, "Update not finished? Something went wrong!");
        }
      } else {
        logger(3, __func__, __LINE__, "Error Occurred. Error #: " + String(Update.getError()));
      }
    } else {
      // not enough space to begin OTA
      // Understand the partitions and
      // space availability
      logger(3, __func__, __LINE__, "Not enough space to begin OTA");
      client.flush();
    }
  } else {
    logger(2, __func__, __LINE__, "There was no content in the response");
    client.flush();
  }
}
