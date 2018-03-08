#include <SimpleDHT.h>
#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <EasyDDNS.h>
#include <SPI.h>
#include <NRFLite.h>
#include <ESP8266WiFi.h>
#include <EasyNTPClient.h>
#include <WiFiUdp.h>
#include <Time.h>
#include <TimeLib.h>
 
//const char* ssid = "DevFluence";
//const char* password = "W1ck3dsaint!!";
const char* ssid = "Ainslie";
const char* password = "ytsurk36552";
const int MAX_DEVICES = 255;
const int MAX_RETRIES = 255;
const String DDNS_DOMAIN = "ainslie.duckdns.org";
const String DDNS_TOKEN = "3b10c0db-7786-45d2-9413-2a283c2c91e2";
const int DDNS_UPDATE_INTERVAL = 10000;
const int SENSOR_REFRESH = 5000;
const int SENSOR_AVERAGE_INTERVAL = 3600000;
const static uint8_t RADIO_ID = 0;       // Our radio's id.  The transmitter will send to this id.
const static uint8_t PIN_RADIO_CE = 2;
const static uint8_t PIN_RADIO_CSN = 4;
const int LED_PIN = 13; // GPIO13
const int PIN_DHT22 = 2;
const byte COMMAND  = B00000001;
const byte STATE    = B00000010;
const byte PIN      = B00000100;
const byte MODE     = B00001000;
const byte TIME     = B00010000;
const byte KEEP_HIGH    = B00100000;
const byte KEEP_LOW = B10000010;
const byte TOGGLE   = B01000000;
const byte SET_OUTPUTS = B10000000;
const byte GET_INPUTS = B10000001;
const byte HEARTBEAT = B10000100;
const byte INPUTS = B10010000;
const byte DATA      = B10001000;
const byte NEW_ID      = B10100000;
const byte RELAY    = B11000000;


WiFiServer server(80);
WiFiClient client;
Adafruit_BMP085 bmp;
NRFLite _radio;
SimpleDHT22 dht22;
WiFiUDP udp;
EasyNTPClient ntpClient(udp, "pool.ntp.org", ((2*60*60))); // IST = GMT + 5:30

unsigned long refreshMillis, shiftMillis;
time_t lastHeard[MAX_DEVICES];
String  pageData;
String requestArray[7];
String inputArray[10];
bool input6, input7, inputData, bufferReady, lastPacketSuccess = false;
time_t lastPacketTime;
float temperature, humidity, pressure = 0;
char dataToSend[28];
uint8_t latestFreshID = 255;
struct WeatherData
{
  float temperature[5];
  float humidity[5];
  float pressure[5];
};

struct RadioPacket // Any packet up to 32 bytes can be sent.
{
    uint8_t FromRadioId;
    char Payload[28];
};

struct WebState
{
  byte deviceID;
  byte command;
  byte state;
  byte pin;
  byte pinCommand;
  uint16_t pinTime;
};

RadioPacket _radioData;
RadioPacket _newData;
WebState webState;
WeatherData weatherData;

void printBits(byte myByte){
 for(byte mask = 0x80; mask; mask >>= 1){
   if(mask  & myByte)
       Serial.print('1');
   else
       Serial.print('0');
 }
 Serial.println();
}

void setup()
{
  Serial.begin(115200);

//  if (!_radio.init(RADIO_ID, PIN_RADIO_CE, PIN_RADIO_CSN))
  {
    Serial.println("Cannot communicate with radio");
  //  while (1); // Wait here forever.
  } 
  //else
  {
    Serial.println("Radio is ready");
  }
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
 
  WiFi.begin(ssid, password);
//  bmp.begin(); 
  while (WiFi.status() != WL_CONNECTED) 
  {
    delay(100);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  
  randomSeed(millis());
 
  // Start the server
  server.begin();
  Serial.println("Server started");
 
  // Print the IP address
  Serial.print("Use this URL to connect: ");
  Serial.print("http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");
  //EasyDDNS.service("duckdns");
  //EasyDDNS.client(DDNS_DOMAIN, DDNS_TOKEN);
  _radioData.FromRadioId = 0;
  refreshMillis = millis();
  shiftMillis = millis();
  //while (syncLatestTime() <= 1519917225)
  {
    delay(1);
  }
  Serial.println(makePrettyTime(now()));
  setSyncProvider(syncLatestTime);
  setSyncInterval(3600);
  //readSensors();
  lastPacketTime = now();
  Serial.println("All init completed.");
}

time_t syncLatestTime()
{
  setTime(ntpClient.getUnixTime());
  return now();
}

bool sendRadioPacket(uint8_t destination, RadioPacket data)
{
  bool sent = false;
  int retryCount = 0;
  //String msg;
  //msg.toCharArray(data.Payload, msg.length()+1);
  //Serial.println("Sending: "+msg);
  while (!sent)
  {
    sent = _radio.send(destination, &data, sizeof(data));
    return sent;
    retryCount++;
    if (retryCount == MAX_RETRIES) { return false; Serial.println("Failed"); break; }
  }

  if (sent)
  {
    bufferReady = false;
  }
}


void decodePacket(char data[28])
{
  byte command;
  byte pinNumber;
  byte pinMode;
  if (data[0] == INPUTS)
  {
    Serial.print("pin :");
    Serial.print(data[1], DEC);
    Serial.print(" ");
    printBits(data[1]);
    Serial.print("status :");
    Serial.print(data[2], DEC);
    Serial.print(" ");
    printBits(data[2]);
  }
  if (data[0] == NEW_ID)
  {
    _newData.Payload[0] = COMMAND;
    _newData.Payload[1] = NEW_ID;
    _newData.Payload[2] = latestFreshID;
    lastPacketSuccess = sendRadioPacket(_radioData.FromRadioId, _newData);
    if (lastPacketSuccess)
    {
      lastPacketTime = now();
    }
  }
  if (data[0] == RELAY)
  {
    _newData.Payload[0] = RELAY;
    uint8_t destination = data[1];
    //etc
  }
}

void doRadioWork()
{
    if (_radio.hasData()) //CHECK FOR INCOMING DATA AND PROCESS IT
    {
      _radio.readData(&_radioData); // Note how '&' must be placed in front of the variable name.
      String packet = _radioData.Payload;
      lastHeard[_radioData.FromRadioId] = now();
      if (packet[0] == HEARTBEAT)
      {
        return;
      } 
      else
      {
        decodePacket(_radioData.Payload);
      }
    }
}

void readSensors()
{
  int err = SimpleDHTErrSuccess;
  if ((err = dht22.read2(PIN_DHT22, &temperature, &humidity, NULL)) != SimpleDHTErrSuccess) {
    Serial.print("Read DHT22 failed, err="); Serial.println(err);delay(2000);
    return;
  }
  temperature = (temperature + bmp.readTemperature())/2; 
  pressure = bmp.readPressure();
  weatherData.temperature[0] = temperature;
  weatherData.pressure[0] = pressure;
  weatherData.humidity[0] = humidity;
}

void shiftSensorData()
{
  for (int i=4;i >=1;i--)
  {
    weatherData.temperature[i] = weatherData.temperature[i-1];
    weatherData.pressure[i] = weatherData.pressure[i-1];
    weatherData.humidity[i] = weatherData.humidity[i-1];
  }
  readSensors();
}

String trimRequest(String data)
{
  return data.substring(data.indexOf('&')+1);
}

String getRequestValue(String data)
{
  return data.substring(data.indexOf('=')+1, data.indexOf('&'));
}

void indexGetData(String data)
{
  data = data.substring(data.indexOf('?')+1);
//  Serial.println("data to index: " + data);
  int index = 0;
  while (data.indexOf('&') > 0)
  {
    String pair = data.substring(0, data.indexOf('&'));
    //Serial.println("pair: " + pair + " for index: " + index);
    requestArray[index] = pair;
    //Serial.println(requestArray[index]);
    index++;
    data = trimRequest(data);
  }
  requestArray[index] = data.substring(0, data.indexOf(' '));
  //Serial.println(requestArray[index]);
}

String getDeviceStatus(int deviceID)
{
  //ask a device for its status
}

String getAllDeviceStatus()
{
  String aggregatedDeviceStatus = "";
  for (int i = 0; i <= MAX_DEVICES; i++)
  {
    aggregatedDeviceStatus = aggregatedDeviceStatus + getDeviceStatus(i);
  }
  return aggregatedDeviceStatus;
}

byte setCommand(String data)
{
  if (getRequestValue(data).indexOf('1') > -1) 
  {
    return SET_OUTPUTS;
  } else if (getRequestValue(data).indexOf('2') > -1)
  {
    return GET_INPUTS;
  }
}

uint8_t setState(String data)
{
  if (getRequestValue(data).indexOf('toggle') > -1) 
  {
    return TOGGLE;
  } else if (getRequestValue(data).indexOf('high') > -1)
  {
    return KEEP_HIGH;
  } else if (getRequestValue(data).indexOf('low') > -1)
  {
    return KEEP_LOW;
  }
}

uint8_t setMode(String data)
{
  return getRequestValue(data).toInt();
}

uint8_t setPin(String data)
{
  return getRequestValue(data).toInt();
}

uint8_t setDeviceID(String data)
{
  return getRequestValue(data).toInt();
}

uint16_t setDuration(String data)
{
  return getRequestValue(data).toInt();
}

bool getFreshID()
{
  //go fetch latest unused ID
  latestFreshID = 3; //tmp
}

void processGetData()
{
  //check if request is empty; return basic status
    //weatherData
    //all device status
  //Serial.println("processing...");
  String radioReply = "";
  for (int i = 0; i <= 6; i++)
  {
    if (requestArray[i].indexOf("device") == 0) 
    {
      webState.deviceID = setDeviceID(requestArray[i]);
      _newData.FromRadioId = (uint8_t)webState.deviceID;
    }
    if (requestArray[i].indexOf("cmd") == 0) 
    {
      webState.command = setCommand(requestArray[i]);
      radioReply = radioReply + COMMAND+webState.command+"|";
    }
    if (requestArray[i].indexOf("state") == 0) 
    {
      webState.state = setState(requestArray[i]);
      radioReply = radioReply + STATE+webState.state+"|";
    }
    if (requestArray[i].indexOf("pin") == 0) 
    {
      webState.pin = setPin(requestArray[i]);
      radioReply = radioReply + PIN+webState.pin+"|";
    }
    if (requestArray[i].indexOf("mode") == 0) 
    {
      webState.pinCommand = setMode(requestArray[i]);
      radioReply = radioReply + MODE+webState.pinCommand+"|";
    }
    if (requestArray[i].indexOf("time") == 0) 
    {
      webState.pinTime = setDuration(requestArray[i]);
      radioReply = radioReply + TIME+webState.pinTime+"|";
    }
  }
  dataToSend[0] = COMMAND;
  dataToSend[1] = webState.command;
  dataToSend[2] = STATE;
  dataToSend[3] = webState.state;
  dataToSend[4] = PIN;
  dataToSend[5] = webState.pin;
  dataToSend[6] = MODE;
  dataToSend[7] = webState.pinCommand;
  int pinTime = webState.pinTime;
  char low = pinTime & 0xFF;
  char high = pinTime >> 8;
  dataToSend[8] = low;
  dataToSend[9] = high;
  for (int i=0;i<28;i++)
  {
    _newData.Payload[i] = dataToSend[i];
  }
  lastPacketSuccess = sendRadioPacket(webState.deviceID, _newData);
  if (lastPacketSuccess)
  {
    lastPacketTime = now();
  }
}

void clearGetData()
{
  for (int i = 0; i <= 6; i++)
  {
    requestArray[i] = "";
  }
}

String buildWebReply()
{
  String reply = "";
  reply = reply + "<font size=4 face=Helvetica>";
  reply = reply + "<table width=100%>";
  reply = reply + "<tr>";
  reply = reply + "<td>";

  //output default information here
  reply = reply + "Current weather: ";
  reply = reply + (float)temperature; 
  reply = reply + " *C, ";
  reply = reply + (float)humidity; 
  reply = reply + " RH%, ";
  reply = reply + pressure/100; 
  reply = reply + " pa ";
  reply = reply + "</td>";
  reply = reply + "<td alight=right>";
  reply = reply + "Last packet sent: ";
  if (lastPacketSuccess)
  {
    reply = reply + "OK";
  } else
  {
    reply = reply + "Failed";
  }
  reply = reply + " at ";
  reply = reply + makePrettyTime(lastPacketTime);
  
  reply = reply + "</td>";
  reply = reply + "</tr>";
  reply = reply + "</table>";
  reply = reply + "<hr>";
  reply = reply + "Previous Weather: <br>";
  for (int i=0;i<=4;i++)
  {
    reply = reply + (i+1);
    reply = reply + "hr: ";
    reply = reply + (float)weatherData.temperature[i]; 
    reply = reply + " *C, ";
    reply = reply + (float)weatherData.humidity[i]; 
    reply = reply + " RH%, ";
    reply = reply + weatherData.pressure[i]/100; 
    reply = reply + " pa ";
    reply = reply + "<br>";
  }
  reply = reply + "<hr>";
  //output interesting information here.
  for (int i = 0; i <= 6; i++)
  {
    reply = reply + requestArray[i]+"<br>";
  }
  reply = reply + "<a href=\"/?device=2&cmd=1&state=toggle&pin=5&mode=1&time=1000\"\"><button>Open Garage </button></a>";
  reply = reply + "<br>";
  reply = reply + "<a href=\"/?device=2&cmd=2&pin=6\"\"><button>Check 6 </button></a>";
  reply = reply + "<br>";

  reply = reply + "Last Heard<BR>";
  for (int i = 0; i <= 99; i++)
  {
    if (lastHeard[i] != NULL)
    {
      reply = reply + "Radio: ";
      reply = reply + i;
      reply = reply + " ";
      reply = reply + makePrettyTime(lastHeard[i])+"<br>"; //might need to make this better!!
    }
  }
  reply = reply + "</font>";
  reply = reply + "</html>";
  return reply;
}

void sensorRefresh()
{
    if ((millis() - refreshMillis) >= SENSOR_REFRESH)
    {
      refreshMillis = millis();
      //readSensors();
    }
    if ((millis() - shiftMillis) >= SENSOR_AVERAGE_INTERVAL)
    {
      shiftMillis = millis();
      shiftSensorData();
    }
}

String makePrettyTime(time_t value)
{
  String timeValue;
  timeValue = hour(value);
  timeValue = timeValue + ":";
  timeValue = timeValue + minute(value);
  timeValue = timeValue + ":";
  timeValue = timeValue + second(value);
  timeValue = timeValue + " ";
  timeValue = timeValue + day(value);
  timeValue = timeValue + "-";
  timeValue = timeValue + month(value);
  timeValue = timeValue + "-";
  timeValue = timeValue + year(value);
  return timeValue;
}

void loop()
{
  RadioPacket tmp;
  WiFiClient client = server.available();
  //EasyDDNS.update(DDNS_UPDATE_INTERVAL);

  String request = "";
  if (!client.available())
  {
    doRadioWork();
    if (!client) 
    {
      return;
    }
  }
  
  request = client.readStringUntil('\r');
  client.flush();
  if (request != "")
  {
    indexGetData(request);
    if (request.indexOf('?') > 0)
    {
      processGetData();
    }
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println(""); 
    client.println("<!DOCTYPE HTML>");
    client.println("<html>");
    client.print(buildWebReply());
    delay(1);
    client.stop();
    clearGetData();
  }
}
