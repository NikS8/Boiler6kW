/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
                                                   boiler-down.ino 
                            Copyright © 2018-2019, Zigfred & Nik.S
31.12.2018 v1
03.01.2019 v2 откалиброваны коэфициенты трансформаторов тока
10.01.2019 v3 изменен расчет в YF-B5
11.01.2019 v4 переименование boiler6kw в boilerDown
23.01.2019 v5 добавлены ds18 ТА и в №№ ds18 только последние 2 знака 
28.01.2019 v6 переименование boilerDown в boiler-down
03.02.2019 v7 преобразование в формат  F("")
04.02.2019 v8 переменные с префиксом boiler-down-
04.02.2019 v9 в вывод добавлено ("data: {")
04.02.2019 v10 добавлена функция freeRam()
06.02.2019 v11 изменение вывода №№ DS18 и префикс заменен на "bd-"
10.02.2019 v12 удален intrevalLogService
10.02.2019 v13 добавлено измерение уровня воды (дальномер HC-SR04)
13.11.2019 v14 переход на статические IP
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
/*******************************************************************\
Сервер boiler-down выдает данные: 
  аналоговые: 
    датчики трансформаторы тока  
  цифровые: 
    датчик скорости потока воды YF-B5
    датчики температуры DS18B20
    дальномер HC-SR04 (измерение уровня воды)
/*******************************************************************/

#include <Ethernet2.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EmonLib.h>
#include <RBD_Timer.h>
#include <HCSR04.h>   //  Библиотека Bifrost.Arduino.Sensors.HCSR04

#define DEVICE_ID "boiler-down";
//String DEVICE_ID "boiler6kw";
#define VERSION 14

#define RESET_UPTIME_TIME 43200000  //  = 30 * 24 * 60 * 60 * 1000 
                                    // reset after 30 days uptime 

byte mac[] = {0xCA, 0x74, 0xBA, 0xCE, 0xBE, 0x01};
IPAddress ip(192, 168, 1, 102);
EthernetServer httpServer(40102); // Ethernet server

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

RBD::Timer ds18ConversionTimer;

#define TRIG_PIN 1
#define ECHO_PIN 3
HCSR04 hcsr04(TRIG_PIN, ECHO_PIN, 30, 4000); // пределы: от и до
int taLevelWater;

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            setup
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void setup() {
  Serial.begin(9600);
  Serial.println("Serial.begin(9600)"); 

  Ethernet.begin(mac,ip);
  
  Serial.println(F("Server is ready."));
  Serial.print(F("Please connect to http://"));
  Serial.println(Ethernet.localIP());
  
  httpServer.begin();
  
  Serial.print(F("FREE RAM: "));
  Serial.println(freeRam());

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
 
  //ds18Precision
  ds18Sensors.requestTemperatures();
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
 
}

/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            realTimeService
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
void realTimeService() {

  EthernetClient reqClient = httpServer.available();
  if (!reqClient) return;

  while (reqClient.available()) reqClient.read();

  ds18RequestTemperatures();

  txOn();
  taLevelWater = hcsr04.distanceInMillimeters();
  txOff();

  String data = createDataString();

  reqClient.println(F("HTTP/1.1 200 OK"));
  reqClient.println(F("Content-Type: application/json"));
  reqClient.print(F("Content-Length: "));
  reqClient.println(data.length());
  reqClient.println();
  reqClient.print(data);

  reqClient.stop();
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
  resultData.concat(F("\n\"data\": {"));

    resultData.concat(F("\n\"bd-trans-1\":"));
    resultData.concat(String(emon1.calcIrms(1480), 1));
    resultData.concat(F(","));
    resultData.concat(F("\n\"bd-trans-2\":"));
    resultData.concat(String(emon2.calcIrms(1480), 1));
    resultData.concat(F(","));
    resultData.concat(F("\n\"bd-trans-3\":"));
    resultData.concat(String(emon3.calcIrms(1480), 1));
    for (uint8_t index = 0; index < ds18DeviceCount; index++)
    {
      DeviceAddress deviceAddress;
      ds18Sensors.getAddress(deviceAddress, index);
      
      resultData.concat(F(",\n\""));
      for (uint8_t i = 0; i < 8; i++)
      {
        if (deviceAddress[i] < 16)  resultData.concat("0");

        resultData.concat(String(deviceAddress[i], HEX));
      }
      resultData.concat(F("\":"));
      resultData.concat(ds18Sensors.getTempC(deviceAddress));
    }
    resultData.concat(F(","));
    resultData.concat(F("\n\"bd-flow\":"));
    resultData.concat(String(getFlowData()));

    resultData.concat(F(","));
    resultData.concat(F("\n\"ta-level\":"));
    resultData.concat((360 - taLevelWater) / 10.0);
    resultData.concat(F(","));
    resultData.concat(F("\n\"ta-level-filled%\":"));
    resultData.concat((int)((360 - taLevelWater) / 3.6));

    resultData.concat(F("\n}"));
    resultData.concat(F(","));
    resultData.concat(F("\n\"freeRam\":"));
    resultData.concat(freeRam());
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
            Включение-выключение TX
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
  void txOff() { UCSR0B |= (1 << TXEN0); }
  void txOn() { UCSR0B &= ~(1 << TXEN0); }

  /*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            Количество свободной памяти
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
  int freeRam()
  {
    extern int __heap_start, *__brkval;
    int v;
    return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}
/*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*\
            end
\*~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~*/
