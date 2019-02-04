/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
                                                    boilerDown.ino 
                            Copyright © 2018-2019, Zigfred & Nik.S
31.12.2018 v1
03.01.2019 v2 откалиброваны коэфициенты трансформаторов тока
10.01.2019 v3 изменен расчет в YF-B5
11.01.2019 v4 переименование boiler6kw в boilerDown
23.01.2019 v5 добавлены ds18 ТА и в №№ ds18 только последние 2 знака 
28.01.2019 v6 переименование boilerDown в boiler-down
03.02.2019 v7 преобразование в формат  F("")
04.02.2019 v8 переменные с префиксом boiler-down-
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
/*******************************************************************\
Сервер boiler6kw выдает данные: 
  аналоговые: 
    датчики трансформаторы тока  
  цифровые: 
    датчик скорости потока воды YF-B5
    датчики температуры DS18B20
/*******************************************************************/

#include <Ethernet2.h>
#include <SPI.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EmonLib.h>
#include <RBD_Timer.h>

#define DEVICE_ID "boiler-down";
//String DEVICE_ID "boiler6kw";
#define VERSION 8

#define RESET_UPTIME_TIME 43200000  //  = 30 * 24 * 60 * 60 * 1000 
                                    // reset after 30 days uptime 
#define REST_SERVICE_URL "192.168.1.210"
#define REST_SERVICE_PORT 3010
char settingsServiceUri[] = "/settings/boiler-down";
char intervalLogServiceUri[] = "/intervalLog/boiler-down";

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
//byte flowSensorInterrupt = 0; // 0 = digital pin 2
volatile long flowSensorPulseCount = 0;

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
  delay(1000);
    if (!Ethernet.begin(mac)) {
    Serial.println(F("Failed to initialize Ethernet library"));
    return;
  }
  httpServer.begin();
  Serial.println(F("Server is ready."));
  Serial.print(F("Please connect to http://"));
  Serial.println(Ethernet.localIP());
  
  pinMode( A1, INPUT );
  pinMode( A2, INPUT );
  pinMode( A3, INPUT );
  emon1.current(1, 9.3);
  emon2.current(2, 9.27);
  emon3.current(3, 9.29);

  pinMode(PIN_FLOW_SENSOR, INPUT);
  //digitalWrite(PIN_FLOW_SENSOR, HIGH);
  attachInterrupt(PIN_INTERRUPT_FLOW_SENSOR, flowSensorPulseCounter, FALLING);
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

  reqClient.println(F("HTTP/1.1 200 OK"));
  reqClient.println(F("Content-Type: application/json"));
  reqClient.println(F("Content-Length: "));
  reqClient.println(data.length());
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
void flowSensorPulseCounter()
{
  // Increment the pulse counter
  flowSensorPulseCount++;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            createDataString
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
String createDataString() {
  String resultData;
  resultData.concat(F("{"));
  resultData.concat(F("\n\"deviceId\":"));
  //  resultData.concat(String(DEVICE_ID));
  resultData.concat(F("\"boiler-down\""));
  resultData.concat(F(","));
  resultData.concat(F("\n\"version\":"));
  resultData.concat((int)VERSION);
  resultData.concat(F(","));
  resultData.concat(F("\n\"boiler-down-flow\":"));
  resultData.concat(String(getFlowData()));
  resultData.concat(F(","));
  resultData.concat(F("\n\"boiler-down-trans-1\":"));
  resultData.concat(String(emon1.calcIrms(1480)));
  resultData.concat(F(","));
  resultData.concat(F("\n\"boiler-down-trans-2\":"));
  resultData.concat(String(emon2.calcIrms(1480)));
  resultData.concat(F(","));
  resultData.concat(F("\n\"boiler-down-trans-3\":"));
  resultData.concat(String(emon3.calcIrms(1480)));
  for (uint8_t index = 0; index < ds18DeviceCount; index++)
  {
    DeviceAddress deviceAddress;
    ds18Sensors.getAddress(deviceAddress, index);
    String stringAddr = dsAddressToString(deviceAddress);
    resultData.concat(F(",\n\""));
   // resultData.concat(F("\n\"ds"));
   // resultData.concat(index);
   // resultData.concat(F(" "));
    //resultData.concat(stringAddr.substring(14));
    resultData.concat(stringAddr);
    resultData.concat(F("\":"));
    resultData.concat(ds18Sensors.getTempC(deviceAddress));
    }
    
  resultData.concat(F("\n}"));

    return resultData;
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            getFlowData
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
int getFlowData()
{
  //  static int flowSensorPulsesPerSecond;
  unsigned long flowSensorPulsesPerSecond;

  unsigned long deltaTime = millis() - flowSensorLastTime;
  //  if ((millis() - flowSensorLastTime) < 1000) {
  if (deltaTime < 1000)
  {
    return;
  }

  //detachInterrupt(flowSensorInterrupt);
  detachInterrupt(PIN_INTERRUPT_FLOW_SENSOR);
  //     flowSensorPulsesPerSecond = (1000 * flowSensorPulseCount / (millis() - flowSensorLastTime));
  //    flowSensorPulsesPerSecond = (flowSensorPulseCount * 1000 / deltaTime);
  flowSensorPulsesPerSecond = flowSensorPulseCount;
  flowSensorPulsesPerSecond *= 1000;
  flowSensorPulsesPerSecond /= deltaTime; //  количество за секунду

  flowSensorLastTime = millis();
  flowSensorPulseCount = 0;
  //attachInterrupt(flowSensorInterrupt, flowSensorPulseCounter, FALLING);
  attachInterrupt(PIN_INTERRUPT_FLOW_SENSOR, flowSensorPulseCounter, FALLING);

  return flowSensorPulsesPerSecond;

}
  /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            UTILS
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
  void resetWhen30Days()
  {
    if (millis() > (RESET_UPTIME_TIME))
    {
      // do reset
    }
  }

  /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            doRequest
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
  String doRequest(char reqUri, String reqData)
  {
    String responseText;

    if (httpClient.connect(REST_SERVICE_URL, REST_SERVICE_PORT))
    { //starts client connection, checks for connection
      Serial.println("connected");

      if (reqData.length())
      { // do post request
        httpClient.println((char)"POST" + reqUri + "HTTP/1.1");
        //httpClient.println("Host: checkip.dyndns.com"); // TODO remove if not necessary
        httpClient.println(F("Content-Type: application/csv;"));
        httpClient.println(F("Content-Length: "));
        httpClient.println(reqData.length());
        httpClient.println();
        httpClient.print(reqData);
      } else { // do get request
      httpClient.println( (char) "GET" +  reqUri + "HTTP/1.1");
      //httpClient.println("Host: checkip.dyndns.com"); // TODO remove if not necessary
      httpClient.println(F("Connection: close"));  //close 1.1 persistent connection  
      httpClient.println(); //end of get request
    }
  } else {
    Serial.println(F("connection failed")); //error message if no client connect
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

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            end
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/