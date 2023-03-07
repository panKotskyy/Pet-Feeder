
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

// Define the Config struct
struct Config {
  String ssid;
  String password;
  String chatId;
  unsigned int tag;
  unsigned int portionWeightPerDay;
  unsigned int firstMealTime;
  unsigned int lastMealTime;
  unsigned int numberOfMealsPerDay;

  String toString() {
    return "{\n\t\"ssid\": \"" + ssid + "\"\n" +
    "\t\"password\": \"" + password + "\"\n" + 
    "\t\"chatId\": \"" + chatId + "\"\n" + 
    "\t\"tag\": " + String(tag) + "\n" + 
    "\t\"portionWeightPerDay\": " + String(portionWeightPerDay) + "\n" + 
    "\t\"firstMealTime\": " + String(firstMealTime) + "\n" + 
    "\t\"lastMealTime\": " + String(lastMealTime) + "\n" + 
    "\t\"numberOfMealsPerDay\": " + String(numberOfMealsPerDay) + "\n}";
  }
};
Config config;



// Initialize Telegram BOT
#define BOTtoken "5933476596:AAG-mZ1tfNy0boHKzrOdjFmFLCckUIfEClc"
#define CHAT_ID "-948044538"
WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);
int botRequestDelay = 1000; // Checks for new messages every 1 second.
unsigned long lastTimeBotRan;

// Timer variables
unsigned long previousMillis = 0;
const long interval = 10000;  // interval to wait for Wi-Fi connection (milliseconds)
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

void getConfig() {
  // Open the JSON file
  File configFile = SD.open("/config.json");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return;
  }

  // Parse the JSON data
  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, configFile);
  if (error) {
    Serial.print("Failed to parse JSON: ");
    Serial.println(error.c_str());
    return;
  }

  // Extract the values from the JSON data
  config.ssid = doc["ssid"] ? doc["ssid"].as<String>() : "";
  config.password = doc["password"] ? doc["password"].as<String>() : "";
  config.chatId = doc["chatId"] ? doc["chatId"].as<String>() : "";
  config.tag = doc["tag"] ? doc["tag"] : 0;
  config.portionWeightPerDay = doc["portionWeightPerDay"] ? doc["portionWeightPerDay"] : 0;
  config.firstMealTime = doc["firstMealTime"] ? doc["firstMealTime"] : 0;
  config.lastMealTime = doc["lastMealTime"] ? doc["lastMealTime"] : 0;
  config.numberOfMealsPerDay = doc["numberOfMealsPerDay"] ? doc["numberOfMealsPerDay"] : 0;
}

void saveConfig() {
  // Open the JSON file
  File configFile = SD.open("/config.json");
  if (!configFile) {
    Serial.println("Failed to open config file");
    return;
  }

  // Read the contents of the JSON file
  String jsonContents = "";
  while (configFile.available()) {
    jsonContents += char(configFile.read());
  }
  configFile.close();

  // Parse the JSON data
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, jsonContents);
  if (error) {
    Serial.print("Failed to parse JSON: ");
    Serial.println(error.c_str());
    return;
  }

  // Update the values in the JSON data
  doc["ssid"] = config.ssid;
  doc["password"] = config.password;

  // Serialize the JSON data to a string
  String jsonString;
  serializeJson(doc, jsonString);

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
//  delay(5000); // Wait for 5 seconds before updating the next field
  
}

bool initWiFi() {
  if(config.ssid=="" || config.password==""){
    Serial.println("Undefined SSID or password.");
    return false;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(config.ssid.c_str(), config.password.c_str());
  Serial.println("Connecting to WiFi...");

  unsigned long currentMillis = millis();
  previousMillis = currentMillis;

  while(WiFi.status() != WL_CONNECTED) {
    currentMillis = millis();
    if (currentMillis - previousMillis >= interval) {
      Serial.println("Failed to connect.");
      return false;
    }
  }

  Serial.println(WiFi.localIP());
  
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
    
    if (text == "/led_off") {
      bot.sendMessage(chat_id, "LED state set to OFF", "");
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

void setup() {
  Serial.begin(115200);
  
  initSDCard();
  getConfig();

  if (initWiFi()) {
    
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(SD, "/index.html", "text/html");
    });
  
    server.serveStatic("/", SD, "/");
    server.begin();
  } else {
    Serial.println("Setting WiFi Manager");
    scanWiFiNetworks();
    WiFi.softAP("PET-FEEDER-WIFI-MANAGER", NULL);
    
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(IP); 

    // Web Server Root URL
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send(SD, "/wifimanager.html", "text/html");
    });
    
    server.serveStatic("/", SD, "/");

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
    server.begin();
    
  }
}

void loop() {
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