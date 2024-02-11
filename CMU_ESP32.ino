#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <BluetoothSerial.h>
#include <FTPCommon.h>
#include <FTPServer.h>
#include <SPIFFS.h>
#include <FS.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <ArduinoOTA.h>
#include <HardwareSerial.h>
#include "defCMU.h"

#define FORMAT_SPIFFS_IF_FAILED true

/**** Pin Settings *******/
/**** LED pin Settings *******/
const int led = 2; // Set LED pin as GPIO2
/**** Serial pin Settings *******/
#define RXD2 16
#define TXD2 17
const int led_pin = 5;

/**** topic Settings *******/
const char *topic = "led_state";

// WiFi
const String IniFilePath = "/pswd.txt";

WiFiClientSecure espClient;
PubSubClient client(espClient);
BluetoothSerial SerialBT;
FTPServer ftpSrv(SPIFFS);
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

File file;

String inputString = "";   // a String to hold incoming data from Serial
String inputBTString = ""; // a String to hold incoming string from BTSerial

String led_string = "led";
bool led_active = false;

byte webSocketConnected = 0;

unsigned long lastMsg = 0;
word count = 100;
byte prog = 0;
byte prog_mode = 0;
byte prog_speed = 0;
int loopCount = 868;
long secCount = 60;
long minCount = 0;

#define MSG_BUFFER_SIZE (50)
char msg[MSG_BUFFER_SIZE];

void setup()
{
  pinMode(led, OUTPUT);
  digitalWrite(led, 1);
  // Set software serial baud to 115200;
  Serial.begin(115200);
  Serial.println("Configuring Serial2...");
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);
  delay(3000);

  inputString.reserve(100);
  inputBTString.reserve(100);

  if (SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED))
  {
    Serial.println("SPIFFS Mounted");
    Serial.println();
    Serial.print("Config file");
    if (SPIFFS.exists(IniFilePath)) {
      Serial.println(" read");
      file = SPIFFS.open(IniFilePath, FILE_READ);
      if (file)
      {
        int n = 0;
        while (file.available())
        {
          String s = file.readStringUntil('\n');
          s.trim();
          switch (n) {
            case 0: ssid = s; break;
            case 1: password = s; break;
            case 5: mqtt_broker = s; break;
            case 7: mqtt_username = s; break;
            case 9: mqtt_password = s; break;
            case 11: mqtt_port = s.toInt(); break;
            default: Serial.println(s);
          }
          n++;
        }
      }
      file.close();
    }
    else
      Serial.println(" not found");
  }
  else
    Serial.println("SPIFFS Mount Failed");

  // Connecting to a WiFi network
  WiFi.begin(ssid.c_str(), password.c_str());
  Serial.print("Connecting to WiFi.");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.println(".");
  }
  Serial.println("Connected to the Wi-Fi network");
  // connecting to a mqtt broker
  espClient.setInsecure();
  client.setServer(mqtt_broker.c_str(), mqtt_port);
  client.setCallback(mqtt_callback);
  while (!client.connected())
  {
    String client_id = "esp32-client-";
    client_id += String(WiFi.macAddress());
    Serial.printf("The client %s connects to the public MQTT broker\n", client_id.c_str());
    //        if (client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
    if (client.connect(mqtt_broker.c_str(), mqtt_username.c_str(), mqtt_password.c_str()))
    {
      Serial.println("MQTT broker connected");
    }
    else
    {
      Serial.print("failed with state ");
      Serial.print(client.state());
      delay(2000);
    }
  }
  // Publish and subscribe
  // client.publish(topic, "CMU32State");
  client.subscribe(topic);

  ftpSrv.begin("root", "root");
  Serial.println("FTP server start");

  webSocket.begin(); // Start the websockets and link the events function (defined below, under the loop)
  Serial.println("WebSocket server start");

  webSocket.onEvent(webSocketEvent);
  // Configure web server to host HTML files
  server.on("/led_state", led_state);
  server.on("/led_off", led_off);
  server.on("/led_on", led_on);
  server.onNotFound([]() {
    if(!handleFileRead(server.uri())) server.send(404, "text/plain", "FileNotFound");
  });
  server.begin();
  Serial.println("HTTP server started");
  ArduinoOTA.setHostname("CMU32");               // Имя хоста
  ArduinoOTA.setPassword((const char *)"CMU32"); // Пароль для подключения к хосту. Если не нужен — комментируем эту строку
  ArduinoOTA.begin();                            // Инициализация
}

void mqtt_callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  Serial.print("Message:");
  for (int i = 0; i < length; i++)
  {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  Serial.println("-----------------------");
}

/**** Method for Publishing MQTT Messages **********/
void publishMessage(const char *topic, String payload, boolean retained)
{
  if (client.publish(topic, payload.c_str(), true))
    Serial.println("Message publised [" + String(topic) + "]: " + payload);
}

void bluetoothIn()
{
  char inChar = SerialBT.read();
  inputBTString += inChar;
  if (inChar == '\n')
  {
    switch (inputBTString[0])
    {
    case '0':
      led_off;
      Serial.println("off");
      break;
    case '1':
      led_on;
      Serial.println("on");
      break;
    case '2':
    {
      DynamicJsonDocument doc_tx(256);
      doc_tx["deviceId"] = "ledCMU";
      doc_tx["siteId"] = "juraspb@CMU";
      doc_tx["mode"] = prog_mode;
      doc_tx["pgm"] = prog;
      doc_tx["speed"] = prog_speed;
      char mqtt_message[128];
      serializeJson(doc_tx, mqtt_message);
      publishMessage("ledCMU_mode", mqtt_message, true);
      break;
    }
    case 'w':
    {
      ssid = inputBTString.substring(1);
      Serial.println(ssid);
      break;
    }
    case 'p':
    {
      password = inputBTString.substring(1);
      Serial.println(password);
      break;
    }
    case 's':
    {
      file = SPIFFS.open(IniFilePath, FILE_WRITE);
      file.print(ssid);
      file.print(password);
      file.close();
      // Serial.println("wifi ssid:%s, pswd:%s" ,ssid,password); break;
      break;
    }
      /*
              case 'l': {
                listAllFiles();
                break;
              }
      */
    }
    inputBTString = "";
  }
}

enum consoleaction
{
  show,
  wait,
  format,
  list
};

consoleaction action = show;

void loop()
{
  if (--loopCount == 0)
  {
    digitalWrite(led, 0);
    loopCount = 868; // Секунда

    
    if (--secCount==0) {
      secCount = 60;
      minCount++;
    }

    DynamicJsonDocument doc_tx(128);
    doc_tx["min"] = String(minCount);
    doc_tx["sec"] = String(60-secCount);
    char time_message[128];
    serializeJson(doc_tx, time_message);
    webSocket.broadcastTXT(time_message);
    
    //Serial2.printf("Sec =%d", secCount);
    //Serial2.println();
    //Serial2.write(secCount);
    digitalWrite(led, 1);
  }
  client.loop();
  ftpSrv.handleFTP();
  if (SerialBT.available()) bluetoothIn();
  server.handleClient();
  webSocket.loop();
  ArduinoOTA.handle();

  if (action == show)
  {
    Serial.printf_P(PSTR("Enter 'F' to format, 'L' to list the contents of the FS\n"));
    action = wait;
  }
  else if (action == wait)
  {
    if (Serial.available())
    {
      char c = Serial.read();
      if (c == 'F')
        action = format;
      else if (c == 'L')
        action = list;
      else if (!(c == '\n' || c == '\r'))
        action = show;
    }
  }
  else if (action == format)
  {
    uint32_t startTime = millis();
    SPIFFS.format();
    Serial.printf_P(PSTR("FS format done, took %lu ms!\n"), millis() - startTime);
    action = show;
  }
  else if (action == list)
  {
    Serial.printf_P(PSTR("Listing contents...\n"));
    uint16_t dirCount = ListDir("/");
    Serial.printf_P(PSTR("%d files total\n"), dirCount);
    action = show;
  }
}

uint16_t ListDir(const char *path)
{
  uint16_t dirCount = 0;
  File root = SPIFFS.open(path);
  if (!root)
  {
    Serial.println(F("failed to open root"));
    return 0;
  }
  if (!root.isDirectory())
  {
    Serial.println(F("/ not a directory"));
    return 0;
  }

  File file = root.openNextFile();
  while (file)
  {
    ++dirCount;
    if (file.isDirectory())
    {
      Serial.print(F("  DIR : "));
      Serial.println(file.name());
      dirCount += ListDir(file.name());
    }
    else
    {
      Serial.print(F("  FILE: "));
      Serial.print(file.name());
      Serial.print(F("\tSIZE: "));
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
  return dirCount;
}

void led_on()
{ 
  led_active = true;
  digitalWrite(led_pin, 0);
  server.send(200, "text/plain","on");
}

void led_off()
{ 
  led_active = false;
  digitalWrite(led_pin, 1);
  server.send(200, "text/plain","off");
}

void led_state()
{ // Функция для определения текущего статуса реле
  String state;
  if (digitalRead(led_pin)) 
    state = "on"; 
  else         
    state = "off";  
  Serial.println("led_status="+state);
  server.send(200, "text/plain", state); // возвращаем результат, преобразовав число в строку
}

// A function to handle our incoming sockets messages
void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t lenght)
{
  switch (type)
  {
  // Runs when a user disconnects
  case WStype_DISCONNECTED:
  {
    Serial2.printf("User #%u - Disconnected!\n", num);
    if (webSocketConnected > 0)
      webSocketConnected--;
    Serial2.println(webSocketConnected);
    break;
  }
  // Runs when a user connects
  case WStype_CONNECTED:
  {
    IPAddress ip = webSocket.remoteIP(num);
    Serial2.printf("--- Connection. IP: %d.%d.%d.%d Namespace: %s UserID: %u\n", ip[0], ip[1], ip[2], ip[3], payload, num);
    Serial2.println();
    // Send last pot value on connect
    webSocket.broadcastTXT(led_string);
    webSocketConnected++;
    Serial2.println(webSocketConnected);
    break;
  }
  // Runs when a user sends us a message
  case WStype_TEXT:
  {
    DynamicJsonDocument doc_rx(128);
    DeserializationError err = deserializeJson(doc_rx, payload);
    if (err) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(err.f_str());
    }
    else {
      auto s = String(doc_rx["device"].as<const char*>());
      if (s=="btn") {
        auto str = String(doc_rx["id"].as<const char*>());
        String s3 = "";
        str.copy(s3,4,5);
        //int btnNum;
        Serial2.println(str+':'+s3);
      }
      else {
        String incoming = "";
        for (int i = 0; i < lenght; i++) 
        {
          incoming.concat((char)payload[i]);
        }
        Serial2.println(incoming);
      }
    }
    break;
  }
  }
}

// A function we use to get the content type for our HTTP responses
String getContentType(String filename)
{
  if (server.hasArg("download"))
    return "application/octet-stream";
  else if (filename.endsWith(".htm"))
    return "text/html";
  else if (filename.endsWith(".html"))
    return "text/html";
  else if (filename.endsWith(".css"))
    return "text/css";
  else if (filename.endsWith(".js"))
    return "application/javascript";
  else if (filename.endsWith(".png"))
    return "image/png";
  else if (filename.endsWith(".gif"))
    return "image/gif";
  else if (filename.endsWith(".jpg"))
    return "image/jpeg";
  else if (filename.endsWith(".ico"))
    return "image/x-icon";
  else if (filename.endsWith(".xml"))
    return "text/xml";
  else if (filename.endsWith(".pdf"))
    return "application/x-pdf";
  else if (filename.endsWith(".zip"))
    return "application/x-zip";
  else if (filename.endsWith(".gz"))
    return "application/x-gzip";
  return "text/plain";
}

// Takes a URL (for example /index.html) and looks up the file in our file system,
// Then sends it off via the HTTP server!
bool handleFileRead(String path)
{
#ifdef DEBUG
  Serial.println("handleFileRead: " + path);
#endif
  if (path.endsWith("/"))
    path += "index.html";
  if (SPIFFS.exists(path))
  {
    File file = SPIFFS.open(path, FILE_READ);
    size_t sent = server.streamFile(file, getContentType(path));
    file.close();
    return true;
  }
  return false;
}
