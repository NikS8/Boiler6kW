/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
                                                    boiler6kw.ino 
                                Copyright © 2018, Zigfred & Nik.S
31.12.2018 v1
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
/*******************************************************************\
Сервер boiler6kw ArduinoJson выдает данные: 
  аналоговые: 
    датчики трансформаторы тока  
  цифровые: 
    датчик скорости потока воды YF-B5
    датчики температуры DS18B20
/*******************************************************************/

//#include <ArduinoJson.h>
#include <Ethernet2.h>
#include <SPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EmonLib.h>
#include <RBD_Timer.h>

#define DEVICE_ID "boiler6kw"
#define VERSION 1

#define RESET_UPTIME_TIME 43200000  //  = 30 * 24 * 60 * 60 * 1000 
                                    // reset after 30 days uptime 
#define REST_SERVICE_URL "192.168.1.210"
#define REST_SERVICE_PORT 3010
char settingsServiceUri[] = "/settings/boiler6kw";
char intervalLogServiceUri[] = "/intervalLog/boiler6kw";

byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xEE, 0xED};
EthernetServer httpServer(40160);
EthernetClient httpClient;

EnergyMonitor emon1;
EnergyMonitor emon2;
EnergyMonitor emon3;

#define PIN_ONE_WIRE_BUS 9
uint8_t ds18Precision = 11;
#define DS18_CONVERSION_TIME 750 / (1 << (12 - ds18Precision))
unsigned short ds18DeviceCount;
bool isDS18ParasitePowerModeOn;
OneWire ds18wireBus(PIN_ONE_WIRE_BUS);
DallasTemperature ds18Sensors(&ds18wireBus);

#define PIN_FLOW_SENSOR 2
#define PIN_INTERRUPT_FLOW_SENSOR 0
#define FLOW_SENSOR_CALIBRATION_FACTOR 5
volatile int flowPulseCount;

// time
unsigned long currentTime;
unsigned long flowSensorLastTime;
// settings intervals
unsigned int intervalLogServicePeriod = 10000;
// timers
RBD::Timer intervalLogServiceTimer;
RBD::Timer ds18ConversionTimer;

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            setup
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void setup() {
  Serial.begin(9600);
  Ethernet.begin(mac);
    while (!Serial) continue;

    if (!Ethernet.begin(mac)) {
    Serial.println(F("Failed to initialize Ethernet library"));
    return;
  }
  httpServer.begin();
  Serial.println(F("Server is ready."));
  Serial.print(F("Please connect to http://"));
  Serial.println(Ethernet.localIP());
  
  pinMode(PIN_FLOW_SENSOR, INPUT);
  attachInterrupt(PIN_INTERRUPT_FLOW_SENSOR, flowSensorPulseCounter, RISING);
  sei();

  pinMode( A1, INPUT );
  pinMode( A2, INPUT );
  pinMode( A3, INPUT );
  emon1.current(1, 8.4);
  emon2.current(2, 8.4);
  emon3.current(3, 8.4);

  pinMode(PIN_FLOW_SENSOR, INPUT);
  attachInterrupt(PIN_INTERRUPT_FLOW_SENSOR, flowSensorPulseCounter, RISING);
  sei();

  ds18Sensors.begin();
  ds18DeviceCount = ds18Sensors.getDeviceCount();

  getSettings();

}
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            Settings
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void getSettings() {
  String responseText = doRequest(settingsServiceUri, "");
  // TODO parse settings and fill values to variables
  //intervalLogServicePeriod = 10000;
  //settingsServiceUri 
  //intervalLogServiceUri
  //ds18Precision
  ds18Sensors.requestTemperatures();
  intervalLogServiceTimer.setTimeout(intervalLogServicePeriod);
  intervalLogServiceTimer.restart();
  ds18ConversionTimer.setTimeout(DS18_CONVERSION_TIME);
  ds18ConversionTimer.restart();
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            loop
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void loop() {
  currentTime = millis();
  resetWhen30Days();

    realTimeService();
    intrevalLogService();
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            realTimeService
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void realTimeService() {

  EthernetClient reqClient = httpServer.available();
  if (!reqClient) return;

  while (reqClient.available()) reqClient.read();

  String data = createDataString();

  reqClient.println("HTTP/1.1 200 OK");
  reqClient.println("Content-Type: application/json");
  reqClient.println("Content-Length: " + data.length());
  reqClient.println();
  reqClient.print(data);

  reqClient.stop();
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            intrevalLogService
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void intrevalLogService() {
  if (intervalLogServiceTimer.getInverseValue() <= DS18_CONVERSION_TIME) {
    ds18RequestTemperatures();
  }

  if (intervalLogServiceTimer.onRestart()) {
    String data = createDataString();

    String response = doRequest(intervalLogServiceUri, data);
    Serial.println(response);
  }
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            ds18RequestTemperatures
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void ds18RequestTemperatures () {
  if (ds18ConversionTimer.onRestart()) {
    ds18Sensors.requestTemperatures();
  }
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            flowSensorPulseCounter
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void flowSensorPulseCounter () {
   flowPulseCount++;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            createDataString
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
String createDataString() {
  String resultData;
  resultData.concat("deviceId:");
  resultData.concat(DEVICE_ID);
  resultData.concat("\nversion:");
  resultData.concat(VERSION);
  resultData.concat("\nflow-0:" + String(getFlowData()));
  resultData.concat("\ntrans-1:" + String(emon1.calcIrms(1480)));
  resultData.concat("\ntrans-2:" + String(emon2.calcIrms(1480)));
  resultData.concat("\ntrans-3:" + String(emon3.calcIrms(1480)));
    for (uint8_t index = 0; index < ds18DeviceCount; index++) {
      DeviceAddress deviceAddress;
      ds18Sensors.getAddress(deviceAddress, index);
      resultData.concat("\nds18-" + dsAddressToString(deviceAddress) + ":" 
                                  + ds18Sensors.getTempC(deviceAddress));
    }

  return resultData;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            getFlowData
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
int getFlowData() {
  static int flowSensorPulsesPerSecond;

  if (intervalLogServiceTimer.isActive()) { // just return previous value if there is call not for intervalLogService
    return flowSensorPulsesPerSecond;
  }

  flowSensorPulsesPerSecond = (millis() - flowSensorLastTime) / 1000 * flowPulseCount;
  flowSensorLastTime = millis();
  flowPulseCount = 0;

  return flowSensorPulsesPerSecond;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            UTILS
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void resetWhen30Days () {
  if (millis() > (RESET_UPTIME_TIME)) {
    // do reset
  }
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            doRequest
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
String doRequest(char reqUri, String reqData) {
  String responseText;

  if (httpClient.connect(REST_SERVICE_URL, REST_SERVICE_PORT)) {  //starts client connection, checks for connection
    Serial.println("connected");

    if (reqData.length()) { // do post request
      httpClient.println((char) "POST" + reqUri + "HTTP/1.1");
      //httpClient.println("Host: checkip.dyndns.com"); // TODO remove if not necessary
      httpClient.println("Content-Type: application/csv;");
      httpClient.println("Content-Length: " + reqData.length());
      httpClient.println();
      httpClient.print(reqData);
    } else { // do get request
      httpClient.println( (char) "GET" +  reqUri + "HTTP/1.1");
      //httpClient.println("Host: checkip.dyndns.com"); // TODO remove if not necessary
      httpClient.println("Connection: close");  //close 1.1 persistent connection  
      httpClient.println(); //end of get request
    }
  } else {
    Serial.println("connection failed"); //error message if no client connect
    Serial.println();
  }

  while (httpClient.connected() && !httpClient.available()) {
    delay(1);
  } //waits for data
  while (httpClient.connected() || httpClient.available()) { //connected or data available
    responseText += httpClient.read(); //places captured byte in readString
  }

  return responseText;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            dsAddressToString
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/

String dsAddressToString(DeviceAddress deviceAddress) {
  String address;
  for (uint8_t i = 0; i < 8; i++) {
    if (deviceAddress[i] < 16 ) address += "0";
    address += String(deviceAddress[i], HEX);
  }
  return address;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            readRequest
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
bool readRequest(EthernetClient& client) {
  bool currentLineIsBlank = true;
  while (client.connected()) {
    if (client.available()) {
      char c = client.read();
      if (c == '\n' && currentLineIsBlank) {
        return true;
      } else if (c == '\n') {
        currentLineIsBlank = true;
      } else if (c != '\r') {
        currentLineIsBlank = false;
      }
    }
  }
  return false;
}