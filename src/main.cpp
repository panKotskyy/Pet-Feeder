
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <HX711.h>
#include <OneButton.h>
#include <AsyncElegantOTA.h>

//PINS
#define SERVO         13
static const int servoPin = 13;

// HX711 circuit wiring
#define LOADCELL_DOUT   3
#define LOADCELL_SCK    4
HX711 scale;
float const factor = 14966.87;
int ONE_PORTION = 15;

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define LOGO_HEIGHT   11
#define LOGO_WIDTH    16
static const unsigned char PROGMEM logo_bmp[] =
{ 0b00000110, 0b00110000,
  0b00000110, 0b00110000,
  0b00000110, 0b00110000,
  0b00110000, 0b00000110,
  0b01110001, 0b10000111,
  0b01100011, 0b11000011,
  0b00000111, 0b11100000,
  0b00011111, 0b11111000,
  0b00111111, 0b11111100,
  0b00111110, 0b01111100,
  0b00001100, 0b00110000 };

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
// Declaration SG90RS
Servo lid;
boolean lidOpen = false;

// Setup buttons
#define BTN1    14
#define BTN2    27
OneButton button1(BTN1, true);
OneButton button2(BTN2, true);

#define IMPACT1 35

// Define the Config struct
struct Config {
  String ssid;
  String password;
  String chatId;
  unsigned int tag;
  unsigned int portion;
  unsigned int firstMealTime;
  unsigned int lastMealTime;
  unsigned int numberOfMeals;

  String toString() {
    return "{\n\t\"ssid\": \"" + ssid + "\"\n" +
    "\t\"password\": \"" + password + "\"\n" + 
    "\t\"chatId\": \"" + chatId + "\"\n" + 
    "\t\"tag\": " + String(tag) + "\n" + 
    "\t\"portion\": " + String(portion) + "\n" + 
    "\t\"firstMealTime\": " + String(firstMealTime) + "\n" + 
    "\t\"lastMealTime\": " + String(lastMealTime) + "\n" + 
    "\t\"numberOfMeals\": " + String(numberOfMeals) + "\n}";
  }
};
Config config;

// Initialize Telegram BOT
#define BOTtoken "5933476596:AAG-mZ1tfNy0boHKzrOdjFmFLCckUIfEClc"
#define CHAT_ID "-948044538"
WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);
int botRequestDelay = 5000; // Checks for new messages every 1 second.
unsigned long lastTimeBotRan;

// Timer variables
unsigned long currentMillis = 0;
unsigned long previousMillis = 0;
unsigned long lastTimeWiFiReconects = 0;
const long interval = 10000;  // interval to wait for Wi-Fi connection (milliseconds)
const long wifiReconectInterval = 30000;  // interval between Wi-Fi reconecting tries (milliseconds)
String ssidList = "";

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

void initSDCard(){
  if(!SD.begin()){
    Serial.println("Card Mount Failed");
    return;
  }

  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE){
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  if(cardType == CARD_MMC){
    Serial.println("MMC");
  } else if(cardType == CARD_SD){
    Serial.println("SDSC");
  } else if(cardType == CARD_SDHC){
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
}

void deleteFile(fs::FS &fs, const char * path){
  Serial.printf("Deleting file: %s\n", path);
  if(fs.remove(path)){
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
  }
}

void getConfig() {
  // Open the JSON file
  File configFile = SD.open("/config.json");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return;
  }

  // Parse the JSON data
  StaticJsonDocument<512> configJson;
  DeserializationError error = deserializeJson(configJson, configFile);
  configFile.close();
  if (error) {
    Serial.print("Failed to parse JSON: ");
    Serial.println(error.c_str());
    return;
  }

  // Extract the values from the JSON
  config.ssid = configJson["ssid"] ? configJson["ssid"].as<String>() : "";
  config.password = configJson["password"] ? configJson["password"].as<String>() : "";
  config.chatId = configJson["chatId"] ? configJson["chatId"].as<String>() : "";
  config.tag = configJson["tag"] ? configJson["tag"] : 0;
  config.portion = configJson["portion"] ? configJson["portion"] : 0;
  config.firstMealTime = configJson["firstMealTime"] ? configJson["firstMealTime"] : 0;
  config.lastMealTime = configJson["lastMealTime"] ? configJson["lastMealTime"] : 0;
  config.numberOfMeals = configJson["numberOfMeals"] ? configJson["numberOfMeals"] : 0;
}

void saveConfig() {
  // Open the JSON file in "read" mode
  File configFile = SD.open("/config.json", FILE_READ);

  // Parse the JSON data
  StaticJsonDocument<512> configJson;
  if (!configFile || configFile.size() == 0) {
    Serial.println("Config file does not exist or is empty.");
    if (configFile) {
      configFile.close();
    }
  } else {
    DeserializationError error = deserializeJson(configJson, configFile);
    configFile.close();
    if (error) {
      Serial.print("Failed to parse JSON: ");
      Serial.println(error.c_str());
      return;
    }
  }

  // Update the values in the JSON data
  configJson["ssid"] = config.ssid;
  configJson["password"] = config.password;
  configJson["chatId"] = config.chatId;
  configJson["tag"] = config.tag;
  configJson["portion"] = config.portion;
  configJson["firstMealTime"] = config.firstMealTime;
  configJson["lastMealTime"] = config.lastMealTime;
  configJson["numberOfMeals"] = config.numberOfMeals;

  // Serialize the JSON data to a string
  String jsonString;
  serializeJson(configJson, jsonString);

  // Open the file in "write" mode and overwrite the existing data
  configFile = SD.open("/config.json", FILE_WRITE);
  if (!configFile) {
    Serial.println("Failed to open file");
    return;
  }

  // Write the updated JSON data to the file
  configFile.println(jsonString);
  configFile.close();
  
  Serial.println("JSON data updated!");
  // delay(5000); // Wait for 5 seconds before updating the next field
}

void handleWiFi() {
  if ((WiFi.getMode() == WIFI_STA) && (WiFi.status() != WL_CONNECTED)) {
    if (millis() - lastTimeWiFiReconects >= wifiReconectInterval) {
      Serial.println("Reconnecting to WiFi...");
      WiFi.disconnect();
      WiFi.reconnect();
      lastTimeWiFiReconects = millis();
    }
  }
}

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("Connected to AP successfully!");
}

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info){
  Serial.println("Disconnected from WiFi access point");
  Serial.print("WiFi lost connection. Reason: ");
  Serial.println(info.wifi_sta_disconnected.reason);
  // Serial.println("Trying to Reconnect");
  // WiFi.begin(config.ssid.c_str(), config.password.c_str());
  handleWiFi();
}

bool initWiFi() {
  if(config.ssid=="" || config.password==""){
    Serial.println(F("WiFi credentials not found."));
    return false;
  }

  // delete old config
  // WiFi.disconnect(true);

  // delay(1000);

  WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid.c_str(), config.password.c_str());
  Serial.println("Connecting to WiFi...");

  previousMillis = millis();

  while(WiFi.status() != WL_CONNECTED) {
    if (millis() - previousMillis >= interval) {
      Serial.println("Failed to connect.");
      return false;
    }
  }
  
  client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  bot.sendMessage(CHAT_ID, "Bot Started", "");
  return true;
}

// Handle what happens when you receive new messages
void handleNewMessages(int numNewMessages) {
  Serial.println("handleNewMessages");
  Serial.println(String(numNewMessages));

  for (int i=0; i<numNewMessages; i++) {
    // Chat id of the requester
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID){
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }
    
    // Print the received message
    String text = bot.messages[i].text;
    Serial.println(text);

    String from_name = bot.messages[i].from_name;

    if (text == "/show_config") {
      bot.sendMessage(chat_id, config.toString(), "");
    }

    if (text == "/delete_config") {
      deleteFile(SD, "/config.json");
      bot.sendMessage(chat_id, "Config deleted. Rebooting....", "");
    }
    
    if (text == "/led_off") {
      bot.sendMessage(chat_id, "LED state set to OFF", "");
    }

    if (text == "/ota") {
      bot.sendMessage(chat_id, "ota link", "");
    }
    
    if (text == "/state") {
      bot.sendMessage(chat_id, "state command received", ""); 
    }
  }
}

void scanWiFiNetworks() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int numSsids = WiFi.scanNetworks();
  for (int i = 0; i < numSsids; i++) {
    ssidList += "\"" + WiFi.SSID(i) + "\"";
    if (i < numSsids - 1) {
      ssidList += ",";
    }
  }
}

void initWifiManager() {
  Serial.println("Setting WiFi Manager");
  scanWiFiNetworks();
  WiFi.softAP("PET_FEEDER", NULL);
    
  IPAddress IP = WiFi.softAPIP();
  Serial.print("WM IP address: ");
  Serial.println(IP);

  // Web Server Root URL
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SD, "/wifimanager.html", "text/html");
  });
    
  server.on("/get_ssid_list", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "application/json", "[" + ssidList + "]");
  });

  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
    int params = request->params();
    for(int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isPost()){
        // HTTP POST ssid value
        if (p->name() == "ssid") {
          config.ssid = p->value().c_str();
          Serial.print("SSID set to: ");
          Serial.println(config.ssid);
        }
        // HTTP POST pass value
        if (p->name() == "password") {
          config.password = p->value().c_str();
          Serial.print("Password set to: ");
          Serial.println(config.password);
        }
      }
    }

    saveConfig();
    
    request->send(200, "text/plain", "Done. ESP will restart and you can proceed with telegram");
    delay(3000);
    ESP.restart();
  });

  server.serveStatic("/", SD, "/");
  server.begin();
}

void initWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SD, "/index.html", "text/html");
  });
  
  server.serveStatic("/", SD, "/");
  AsyncElegantOTA.begin(&server); 
  server.begin();
}

void handleTelegram() {
  if ((WiFi.getMode() == WIFI_STA) && (WiFi.status() == WL_CONNECTED)) {
    if (millis() > lastTimeBotRan + botRequestDelay) {
      int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

      while(numNewMessages) {
        Serial.println("got response");
        handleNewMessages(numNewMessages);
        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      }
      lastTimeBotRan = millis();
    }
  }
}

void initDisplay() {
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
}

void displayLogo() {
  display.clearDisplay();

  display.drawBitmap(
    (display.width() - LOGO_WIDTH)/2,
    (display.height() - LOGO_HEIGHT)/2 - 10,
    logo_bmp, LOGO_WIDTH, LOGO_HEIGHT, 1);
    
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(35, 32);
  // Display static text
  display.println("PET FEEDER");
  display.setCursor(33, 42);
  display.println("v0.1 (beta)");
  display.display();
}

void initLid() {
  lid.attach(servoPin);

  lid.write(90);
  delay(3000);

  lid.write(45);
  delay(3000);

  lid.write(90);
  delay(3000);

  lid.write(135);
  delay(3000);

  lid.write(90);
  delay(3000);

  lid.detach();
}

void initScale() {
  scale.begin(LOADCELL_DOUT, LOADCELL_SCK);
  scale.set_scale(factor);    // this value is obtained by calibrating the scale with known weights; see the README for details
  scale.tare();               // reset the scale to 0
}

void click1() {
  Serial.println("Button 1 click.");
  lid.attach(SERVO);
  while (!lidOpen)
  {
    lid.write(45);
  }
  lid.detach();
  Serial.println("Servo stop.");
}

void click2() {
  Serial.println("Button 2 click.");
  lid.attach(SERVO);
  while (!lidOpen)
  {
    lid.write(135);
  }
  while (lidOpen)
  {
    lid.write(45);
  }
  lid.detach();
  Serial.println("Servo stop.");
}

void initButtons() {
  // link the button 1 functions.
  button1.attachClick(click1);
  // button1.attachDoubleClick(doubleclick1);
  // button1.attachLongPressStart(longPressStart1);
  // button1.attachLongPressStop(longPressStop1);
  // button1.attachDuringLongPress(longPress1);

  // link the button 2 functions.
  button2.attachClick(click2);
  // button2.attachDoubleClick(doubleclick2);
  // button2.attachLongPressStart(longPressStart2);
  // button2.attachLongPressStop(longPressStop2);
  // button2.attachDuringLongPress(longPress2);
}

void IRAM_ATTR detectsLidMovement() {
  if (digitalRead(IMPACT1) == HIGH) {
    lidOpen = false;
  } else {
    lidOpen = true;
  }
}

void initImpact() {
  pinMode(IMPACT1, INPUT);
  attachInterrupt(digitalPinToInterrupt(IMPACT1), detectsLidMovement, CHANGE);
}

void serveFood() {
  Serial.println("Serving the food...");

  float portion = 0;
  while(portion < ONE_PORTION) {
    portion = scale.get_units();
    Serial.print("Current portion weight:\t");
    Serial.println(portion);
  }
  
  Serial.println("One portion is weighed!");

  portion = 0;
  scale.power_down();              // put the ADC in sleep mode
  delay(5000);
  scale.power_up();
}

void setup() {
  Serial.begin(115200);
  
  initDisplay();
  // initLid();
  initSDCard();
  getConfig();
  displayLogo();
  initScale();
  initButtons();
  initImpact();

  if (initWiFi()) {
    initWebServer();
  } else {
    initWifiManager();
  }
}

void loop() {

// keep watching the push buttons:
  button1.tick();
  button2.tick();

  handleTelegram();

  // serveFood();
  
}