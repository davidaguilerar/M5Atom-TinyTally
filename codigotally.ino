// CÓDIGO TALLY ARBITER PARA M5ATOM LITE, QUE UTILIZA EL LED INTERNO DEL SISTEMA
// Este programa permite configurar un Tally Light basado en el microcontrolador M5 Atom Lite
// Tiene la capacidad de crear una red cautiva Wi-Fi para configurar en el router, y memorizar la configuración
// 2024 David Aguilera Riquelme - Basado en el código del M5Atom-Matrix disponible en https://github.com/josephdadams/TallyArbiter
// Este programa es Software Libre, basado en la Licencia MIT
// Instala las librerías y tarjetas indicadas aquí, y consulta la página de M5 Atom para saber más cómo configurarlo en tu PC.

#include <M5Atom.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <SocketIOclient.h>
#include <Arduino_JSON.h>
#include <PinButton.h>
#include <stdint.h>
#include <Arduino.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#define DATA_PIN_LED 27 // NeoPixelArray

PinButton btnAction(39); //Este es el botón que se encuentra en la parte frontal del M5Atom
Preferences preferences;

/* CONFIGURA ESTO PRIMERO
    Necesitas modificar acá la dirección IP y puerto del software servidor Tally Arbiter
*/

//Tally Arbiter Server
char tallyarbiter_host[40] = "192.168.0.64";
char tallyarbiter_port[6] = "4455";

// Al poner 1 en staticIP, se pone una IP estática al dispositivo, con 0 se usa DHCP. Recomendamos usar IP estática desde WifiManager

#define staticIP 0
#if staticIP == 1
IPAddress stationIP = IPAddress(192, 168, 0, 201);
IPAddress stationGW = IPAddress(192, 168, 0, 1);
IPAddress stationMask = IPAddress(255, 255, 255, 0);
#endif

//Número de cámara por defecto de tu Tally. Se puede cambiar pinchando el botón frontal del M5Atom
int camNumber = 0;

//Nombre del dispositivo - Se añadirá unos bytes del MAC Address para hacer un id único.
String listenerDeviceName = "m5Atom-";

//Esta es la contraseña de la red Wi-Fi de configuración del M5Atom. Déjala en blanco para una red abierta
const char* AP_password ="";


/* Fin de variables personalizadas
 *  
 */

//Tally Arbiter variables
SocketIOclient socket;
WiFiManager WiFiManager; // global WiFiManager instance

JSONVar BusOptions;
JSONVar Devices;
JSONVar DeviceStates;
String DeviceId = "unassigned";
String DeviceName = "unassigned";
String ListenerType = "m5";
const unsigned long reconnectInterval = 5000;
unsigned long currentReconnectTime = 0;
bool isReconnecting = false;

String prevType = ""; // reduce display flicker by storing previous state
String actualType = "";
String actualColor = "";
int actualPriority = 0;
long colorNumber = 0;

// default color values
int RGB_COLOR_WHITE = 0xffffff;
int RGB_COLOR_DIMWHITE = 0x555555;
int RGB_COLOR_WARMWHITE = 0xFFEBC8;
int RGB_COLOR_DIMWARMWHITE = 0x877D5F;
int RGB_COLOR_BLACK = 0x000000;
int RGB_COLOR_RED = 0xff0000;
int RGB_COLOR_ORANGE = 0xa5ff00;
int RGB_COLOR_YELLOW = 0xffff00;
int RGB_COLOR_DIMYELLOW = 0x555500;
int RGB_COLOR_GREEN = 0x008800; // toning this down as the green is way brighter than the other colours
int RGB_COLOR_BLUE = 0x0000ff;
int RGB_COLOR_PURPLE = 0x008080;

int numbercolor = RGB_COLOR_WARMWHITE;

int flashcolor = RGB_COLOR_WHITE;
int offcolor = RGB_COLOR_BLACK;
int badcolor = RGB_COLOR_RED;
int readycolor = RGB_COLOR_GREEN;
int alloffcolor = RGB_COLOR_BLACK;
int wificolor = RGB_COLOR_BLUE;
int infocolor = RGB_COLOR_ORANGE;


// Logger - logs to serial number
void logger(String strLog, String strType) {
  if (strType == "info") {
    Serial.println(strLog);
  }
  else {
    Serial.println(strLog);
  }
}
// Set Device name
void setDeviceName(){
  for (int i = 0; i < Devices.length(); i++) {
    if (JSON.stringify(Devices[i]["id"]) == "\"" + DeviceId + "\"") {
      String strDevice = JSON.stringify(Devices[i]["Type"]);
      DeviceName = strDevice.substring(1, strDevice.length() - 1);
      break;
    }
  }
  preferences.begin("tally-arbiter", false);
  preferences.putString("devicename", DeviceName);
  preferences.putString("deviceid", DeviceId);
  preferences.end();
  logger("-------------------------------------------------", "info-quiet");
  logger("DeviceName: " + String(DeviceName), "info-quiet");
  logger("DeviceId: " + String(DeviceId), "info-quiet");
  logger("-------------------------------------------------", "info-quiet");
}


void lucecita(int color){
  M5.dis.drawpix(0, color);
}

void parpadearlucecita(int color, int cantidad){
  for (int k = 0; k < cantidad; k++) {
    M5.dis.drawpix(0, color);
    delay(250);
    M5.dis.drawpix(0, 0x000000);
    delay(250);
  }
}

//---------------------------------------------------------------

// Determine if the device is currently in preview, program, or both
void evaluateMode() {
  if(actualType != prevType) {
    //M5.dis.clear();
    actualColor.replace("#", "");
    String hexstring = actualColor;
 // This order is to compensate for Matrix needing grb.
    int r = strtol(hexstring.substring(3, 5).c_str(), NULL, 16);
    int g = strtol(hexstring.substring(1, 3).c_str(), NULL, 16);
    int b = strtol(hexstring.substring(5).c_str(), NULL, 16);
    
    if (actualType != "") {
     int backgroundColorhex = (g << 16) | (r << 8) | b; // Swap positions of RGB to GRB conversion
      int currColor = backgroundColorhex;
      logger("Current color: " + String(backgroundColorhex), "info");
      //logger("Current camNumber: " + String(camNumber), "info");
      lucecita(currColor);
    } else {
      //drawNumber(camNumber, offcolor);
    }

    logger("Device is in " + actualType + " (color " + actualColor + " priority " + String(actualPriority) + ")", "info");
    // This is a hack to compensate for the Matrix needing GRB.
    logger(" r: " + String(g) + " g: " + String(r) + " b: " + String(b), "info");

    prevType = actualType;
  }  
}

void startReconnect() {
  if (!isReconnecting)
  {
    isReconnecting = true;
    currentReconnectTime = millis();
  }
}

void connectToServer() {
  logger("---------------------------------", "info-quiet");
  logger("Connecting to Tally Arbiter host: " + String(tallyarbiter_host), "info-quiet");
  socket.onEvent(socket_event);
  socket.begin(tallyarbiter_host, atol(tallyarbiter_port));
  logger("---------------------------------", "info-quiet");
}

// Here are all the socket listen events - messages sent from Tally Arbiter to the M5

void socket_Disconnected(const char * payload, size_t length) {
  logger("Disconnected from server, will try to re-connect: " + String(payload), "info-quiet");
  Serial.println("disconnected, going to try to reconnect");
  startReconnect();
}

void ws_emit(String event, const char *payload = NULL) {
  if (payload) {
    String msg = "[\"" + event + "\"," + payload + "]";
    Serial.println(msg);
    socket.sendEVENT(msg);
  } else {
    String msg = "[\"" + event + "\"]";
    Serial.println(msg);
    socket.sendEVENT(msg);
  }
}

String strip_quot(String str) {
  if (str[0] == '"') {
    str.remove(0, 1);
  }
  if (str.endsWith("\"")) {
    str.remove(str.length()-1, 1);
  }
  return str;
}

void socket_event(socketIOmessageType_t type, uint8_t * payload, size_t length) {
  String eventMsg = "";
  String eventType = "";
  String eventContent = "";

  switch (type) {
    case sIOtype_CONNECT:
      socket_Connected((char*)payload, length);
      break;

    case sIOtype_DISCONNECT:
      socket_Disconnected((char*)payload, length);
      break;
    case sIOtype_ACK:
    case sIOtype_ERROR:
    case sIOtype_BINARY_EVENT:
    case sIOtype_BINARY_ACK:
      // Not handled
      break;

    case sIOtype_EVENT:
      eventMsg = (char*)payload;
      eventType = eventMsg.substring(2, eventMsg.indexOf("\"",2));
      eventContent = eventMsg.substring(eventType.length() + 4);
      eventContent.remove(eventContent.length() - 1);

      logger("Got event '" + eventType + "', data: " + eventContent, "VERBOSE");

      if (eventType == "bus_options") socket_BusOptions(eventContent);
      if (eventType == "deviceId") socket_DeviceId(eventContent);
      if (eventType == "devices") socket_Devices(eventContent);
      if (eventType == "device_states") socket_DeviceStates(eventContent);
      if (eventType == "flash") socket_Flash();
      if (eventType == "reassign") socket_Reassign(eventContent);

      break;

    default:
      break;
  }
}

void socket_Reassign(String payload) {
  logger("Socket Reassign: " + String(payload), "info-quiet");
  Serial.println(payload);
  String oldDeviceId = payload.substring(0, payload.indexOf(','));
  String newDeviceId = payload.substring(oldDeviceId.length()+1);
  newDeviceId = newDeviceId.substring(0, newDeviceId.indexOf(','));
  oldDeviceId = strip_quot(oldDeviceId);
  newDeviceId = strip_quot(newDeviceId);
  
  String reassignObj = "{\"oldDeviceId\": \"" + oldDeviceId + "\", \"newDeviceId\": \"" + newDeviceId + "\"}";
  char charReassignObj[1024];
  strcpy(charReassignObj, reassignObj.c_str());
  ws_emit("listener_reassign_object", charReassignObj);
  ws_emit("devices");
  
  // Flash 2 times
  lucecita(alloffcolor);
  delay(200);
  lucecita(readycolor);
  delay(300);
  lucecita(alloffcolor);
  delay(200);
  lucecita(readycolor);
  delay(300);
  lucecita(alloffcolor);
  delay(200);
  lucecita(readycolor);
  delay(300);
  lucecita(alloffcolor);
  delay(200);

  logger("newDeviceId: " + newDeviceId, "info-quiet");
  DeviceId = newDeviceId;
  preferences.begin("tally-arbiter", false);
  preferences.putString("deviceid", newDeviceId);
  preferences.end();
  setDeviceName();
}
void socket_Flash() {
  //flash the screen white 3 times
  logger("The device flashed.", "info-quiet");
  for (int k = 0; k < 3; k++) {
    //Matrix Off
    lucecita(alloffcolor);
    delay(100);

    //Matrix On
    lucecita(flashcolor);
    delay(100);
  }
  //Matrix Off
  lucecita(alloffcolor);
  delay(100);
  //then resume normal operation
  evaluateMode();
}

void socket_Connected(const char * payload, size_t length) {
  logger("---------------------------------", "info-quiet");
  logger("Connected to Tally Arbiter host: " + String(tallyarbiter_host), "info-quiet");
  isReconnecting = false;
  String deviceObj = "{\"deviceId\": \"" + DeviceId + "\", \"listenerType\": \"" + listenerDeviceName.c_str() + "\", \"canBeReassigned\": true, \"canBeFlashed\": true, \"supportsChat\": false }";
  logger("deviceObj = " + String(deviceObj), "info-quiet");
  logger("DeviceId = " + String(DeviceId), "info-quiet");
  char charDeviceObj[1024];
  strcpy(charDeviceObj, deviceObj.c_str());
  ws_emit("listenerclient_connect", charDeviceObj);
  logger("charDeviceObj = " + String(charDeviceObj), "info-quiet");
  logger("---------------------------------", "info-quiet");
}

void socket_BusOptions(String payload) {
  //logger("Socket Message BusOptions: " + String(payload), "info-quiet");
  BusOptions = JSON.parse(payload);
}

void socket_Devices(String payload) {
  //logger("Socket Message Devices: " + String(payload), "info-quiet");
  Devices = JSON.parse(payload);
  setDeviceName();
}

void socket_DeviceId(String payload) {
  //logger("Socket Message DeviceId: " + String(payload), "info-quiet");
  DeviceId = strip_quot(String(payload));
  setDeviceName();
}

void socket_DeviceStates(String payload) {
  //logger("Socket Message DeviceStates: " + String(payload), "VERBOSE");
  DeviceStates = JSON.parse(payload);
  processTallyData();
}

String getBusTypeById(String busId) {
  for (int i = 0; i < BusOptions.length(); i++) {
    if (JSON.stringify(BusOptions[i]["id"]) == busId) {
      return JSON.stringify(BusOptions[i]["type"]);
    }
  }

  return "invalid";
}

String getBusColorById(String busId) {
  for (int i = 0; i < BusOptions.length(); i++) {
    if (JSON.stringify(BusOptions[i]["id"]) == busId) {
      return JSON.stringify(BusOptions[i]["color"]);
    }
  }

  return "invalid";
}

int getBusPriorityById(String busId) {
  for (int i = 0; i < BusOptions.length(); i++) {
    if (JSON.stringify(BusOptions[i]["id"]) == busId) {
      return (int) JSON.stringify(BusOptions[i]["priority"]).toInt();
    }
  }

  return 0;
}

void processTallyData() {
  bool typeChanged = false;
  for (int i = 0; i < DeviceStates.length(); i++) {
    if (DeviceStates[i]["sources"].length() > 0) {
      typeChanged = true;
      actualType = getBusTypeById(JSON.stringify(DeviceStates[i]["busId"]));
      actualColor = getBusColorById(JSON.stringify(DeviceStates[i]["busId"]));
      actualPriority = getBusPriorityById(JSON.stringify(DeviceStates[i]["busId"]));
    }
  }
  if(!typeChanged) {
    actualType = "";
    actualColor = "";
    actualPriority = 0;
  }
  evaluateMode();
}

// A whole ton of WiFiManager stuff, first up, here is the Paramaters
WiFiManagerParameter* custom_taServer;
WiFiManagerParameter* custom_taPort;
//WiFiManagerParameter* custom_tashownumbersduringtally;

void connectToNetwork() {
  // allow for static IP assignment instead of DHCP if stationIP is defined as something other than 0.0.0.0
  #if staticIP == 1
  if (stationIP != IPAddress(0, 0, 0, 0))
  {
    WiFiManager.setSTAStaticIPConfig(stationIP, stationGW, stationMask); // optional DNS 4th argument 
  }
  #endif
  
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  logger("Connecting to SSID: " + String(WiFi.SSID()), "info");

  //reset settings - wipe credentials for testing
  //WiFiManager.resetSettings();

  //add TA fields
  custom_taServer = new WiFiManagerParameter("taHostIP", "Servidor de Tally Arbiter", tallyarbiter_host, 40);
  custom_taPort = new WiFiManagerParameter("taHostPort", "Puerto", tallyarbiter_port, 6);

  WiFiManager.addParameter(custom_taServer);
  WiFiManager.addParameter(custom_taPort);
  //WiFiManager.addParameter(custom_tashownumbersduringtally);

  WiFiManager.setSaveParamsCallback(saveParamCallback);

  // custom menu via array or vector
  std::vector<const char *> menu = {"wifi","param","info","sep","restart","exit"};
  WiFiManager.setMenu(menu);

  // set dark theme
  WiFiManager.setClass("invert");

  WiFiManager.setConfigPortalTimeout(120); // auto close configportal after n seconds

  bool res;
  
  res = WiFiManager.autoConnect(listenerDeviceName.c_str(),AP_password);

  if (!res) {
    logger("Failed to connect", "error");
    lucecita(badcolor); //display failed mark
    // ESP.restart();
  } else {
    //if you get here you have connected to the WiFi
    logger("connected...yay :)", "info");

    // Flash screen if connected to wifi.
    lucecita(wificolor); //1 ring
    delay(500);
    lucecita(wificolor); //2 rings
    delay(500);
    lucecita(wificolor); //3 rings
    delay(500);
    lucecita(readycolor); //display okay mark
    delay(400);
    
    //TODO: fix MDNS discovery
    /*

    int nrOfServices = MDNS.queryService("tally-arbiter", "tcp");

    if (nrOfServices == 0) {
      logger("No server found.", "error");
    } else {
      logger("Number of servers found: ", "info");
      Serial.print(nrOfServices);
     
      for (int i = 0; i < nrOfServices; i=i+1) {
 
        Serial.println("---------------");
       
        Serial.print("Hostname: ");
        Serial.println(MDNS.hostname(i));
 
        Serial.print("IP address: ");
        Serial.println(MDNS.IP(i));
 
        Serial.print("Port: ");
        Serial.println(MDNS.port(i));
 
        Serial.println("---------------");
      }
    }
    */
  }
}

String getParam(String name) {
  //read parameter from server, for customhmtl input
  String value;
  if (WiFiManager.server->hasArg(name)) {
    value = WiFiManager.server->arg(name);
  }
  return value;
}


void saveParamCallback() {
  logger("[CALLBACK] saveParamCallback fired", "info-quiet");
  logger("PARAM tally Arbiter Server = " + getParam("taHostIP"), "info-quiet");
  String str_taHost = getParam("taHostIP");
  String str_taPort = getParam("taHostPort");
  String str_tashownumbersduringtally = getParam("tashownumbersduringtally");
  //saveEEPROM(); // this was commented out as prefrences is now being used in place
  logger("Saving new TallyArbiter host", "info-quiet");
  logger(str_taHost, "info-quiet");
  preferences.begin("tally-arbiter", false);
  preferences.putString("taHost", str_taHost);
  preferences.putString("taPort", str_taPort);
  preferences.putString("tashownumbersduringtally", str_tashownumbersduringtally);
  preferences.end();

}

// --------------------------------------------------------------------------------------------------------------------
// Setup is the pre-loop running program

void setup() {
  Serial.begin(115200);
  while (!Serial);
  logger("Initializing M5-Atom.", "info-quiet");
  
  //Save battery by turning off BlueTooth
  btStop();

  // Append last three pairs of MAC to listenerDeviceName to make it some what unique
  byte mac[6];              // the MAC address of your Wifi shield
  WiFi.macAddress(mac);
  listenerDeviceName = listenerDeviceName + String(mac[3], HEX) + String(mac[4], HEX) + String(mac[5], HEX);
  logger("Listener device name: " + listenerDeviceName, "info");

  // Set WiFi hostname
  WiFiManager.setHostname ((const char *) listenerDeviceName.c_str());

  M5.begin(true, false, true);
  delay(50);
  M5.dis.drawpix(0, 0xff0000);

  // blanks out the screen
  lucecita(alloffcolor);
  delay(100); //wait 100ms before moving on

  //do startup animation
  lucecita(infocolor);
  delay(400);
  lucecita(infocolor);
  delay(400);
  lucecita(infocolor);
  delay(400);
  
  connectToNetwork(); //starts Wifi connection

  // Load from non-volatile memory
  preferences.begin("tally-arbiter", false);

    if (preferences.getString("deviceid").length() > 0) {
      DeviceId = preferences.getString("deviceid");
      //DeviceId = "unassigned";
    }
    if (preferences.getString("devicename").length() > 0) {
      DeviceName = preferences.getString("devicename");
      //DeviceName = "unassigned";
    }

    if(preferences.getString("taHost").length() > 0){
      String newHost = preferences.getString("taHost");
      logger("Setting TallyArbiter host as " + newHost, "info-quiet");
      newHost.toCharArray(tallyarbiter_host, 40);
    }
    if(preferences.getString("taPort").length() > 0){
      String newPort = preferences.getString("taPort");
      logger("Setting TallyArbiter port as " + newPort, "info-quiet");
      newPort.toCharArray(tallyarbiter_port, 6);
    }
    camNumber = preferences.getInt("camNumber"); // Get camera from memory

  preferences.end();

  //debug
    char message[200]; // Adjust the size as needed
    sprintf(message, "After the preferences.end TA Host is: %s TA Port is: %s", tallyarbiter_host, tallyarbiter_port);
    logger(message, "info-quiet");


  ArduinoOTA.setHostname(listenerDeviceName.c_str());
  ArduinoOTA.setPassword("tallyarbiter");
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) logger("Auth Failed", "error");
      else if (error == OTA_BEGIN_ERROR) logger("Begin Failed", "error");
      else if (error == OTA_CONNECT_ERROR) logger("Connect Failed", "error");
      else if (error == OTA_RECEIVE_ERROR) logger("Receive Failed", "error");
      else if (error == OTA_END_ERROR) logger("End Failed", "error");
    });

  ArduinoOTA.begin();
  
  connectToServer();
  delay (100);
}
// --------------------------------------------------------------------------------------------------------------------



// --------------------------------------------------------------------------------------------------------------------
// This is the main program loop
void loop(){
  socket.loop();
  if (M5.Btn.wasPressed()){
    // Switch action below
    if (camNumber < 16){
      camNumber++;  
        preferences.begin("tally-arbiter", false);          // Open Preferences with no read-only access
        preferences.putInt("camNumber", camNumber);      // Save camera number
        delay(100);                                         // Introduce a short delay before closing
        parpadearlucecita(infocolor, camNumber);
        preferences.end();                                  // Close the Preferences after saving
    } else {
      camNumber = 0;
        preferences.begin("tally-arbiter", false);          // Open Preferences with no read-only access
        preferences.putInt("camNumber", camNumber);      // Save camera number
        delay(100);                                         // Introduce a short delay before closing
        preferences.end();                                  // Close the Preferences after saving
    }
    lucecita(offcolor);

    // Lets get some info sent out the serial connection for debugging
    logger("---------------------------------", "info-quiet");
    logger("Button Pressed.", "info-quiet");
    logger("M5Atom IP Address: " + WiFi.localIP().toString(), "info-quiet");
    logger("Tally Arbiter Server: " + String(tallyarbiter_host), "info-quiet");
    logger("Device ID: " + String(DeviceId), "info-quiet");
    logger("Device Name: " + String(DeviceName), "info-quiet");
    logger("Cam Number: " + String(camNumber), "info-quiet");
    logger("---------------------------------", "info-quiet");
  }
    
  // Is WiFi reset triggered?
  if (M5.Btn.pressedFor(5000)){
    WiFiManager.resetSettings();
    ESP.restart();
  }

  // handle reconnecting if disconnected
  if (isReconnecting)
  {
  unsigned long currentTime = millis();
    
    if (currentTime - currentReconnectTime >= reconnectInterval)
    {
      Serial.println("trying to re-connect with server");
      connectToServer();
      currentReconnectTime = millis();
    }
  }

  
  delay(100);
  M5.update();
}
// --------------------------------------------------------------------------------------------------------------------
