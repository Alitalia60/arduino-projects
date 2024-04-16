#define DEBUG_MODE
#define VERSION "04.04.2024"

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <EEPROM.h>

#define PIN_RELAY 5 //(D1)
// #define PIN_LED 2 //(D4)
// #define PIN_LED 13 //(D7)
// #define PIN_LED 15 //(D8)
#define PIN_LED 16 //(D0)
// 

// **************** PubSubClient ****************************
// Use WiFiClient class to create TCP connections
WiFiClient wi_fi_client;
PubSubClient client(wi_fi_client);

// ***************** sensors DS18B20 ***************************/
OneWire oneWire(2);
// Термо датчики

DallasTemperature tempSensors(&oneWire);
DeviceAddress sensor_addr;


const char* pswd = "06019013";
#ifdef DEBUG_MODE
const char* ssid = "TP-Link_3AAA";  // Название Вашей WiFi сети
#else
const char* ssid = "MT-PON-LITA";
#endif

int8_t current_temp = 0;
int8_t min_temp = 1;
bool heater_is_on = false;

unsigned long prevTimer = 0;    // Variable used to keep track of the previous timer value
unsigned long actualTimer = 0;  // Variable used to keep track of the current timer value
const long intTimer = 20000;

//
// ***********************************************
void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, pswd);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.localIP());
}


// ********************************************************
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    // Attempt to connect
    // if (client.connect("main-controller", "alita60", "11071960")) {
    if (client.connect("footer_sensor")) {
      Serial.println("Connected MQTT 'footer_sensor'");
      if (!client.subscribe("footer-sensor/#")) {
        Serial.println("subscribe 'footer-sensor/#' - false");
      }
      if (!client.subscribe("footer-heater/#")) {
        Serial.println("subscribe footer-heater - false");
      }
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
    delay(2000);
  }
}

// ********************************************************
void handle_message(char* topic, byte* payload, unsigned int length) {
  String str_topic = String(topic);
  String pl;

  for (int i = 0; i < length; i++) {
    pl += (char)payload[i];
  }

  Serial.print("Recieved:  ");
  Serial.print(str_topic);
  Serial.print(" = ");
  Serial.println(pl);


  if (String(topic) == "footer-sensor/set-min-temp") {
    int8_t _new_temp = 0;
    _new_temp = pl.toInt();

    int8_t _get_temp;
    EEPROM.get(0, _get_temp);
    Serial.print("EEPROM.get = ");
    Serial.println(_get_temp);

    if (_get_temp != _new_temp) {
      EEPROM.put(0, _new_temp);
      EEPROM.commit();  // для esp8266/esp32
      min_temp = _new_temp;
      Serial.print("Set new MIN-TEMP: ");
      Serial.println(min_temp);
      Serial.println();
    }
  }

  if (String(topic) == "footer-sensor/get-min-temp") {
    String str_min_temp;
    char ch_min_temp[4];
    str_min_temp = String(min_temp);
    str_min_temp.toCharArray(ch_min_temp, 4);
    Serial.print("footer-sensor/get-min-temp. min_temp = ");
    Serial.println(ch_min_temp);
    client.publish("footer-sensor/limit-min-temp", ch_min_temp);
  }


  if (String(topic) == "heater") {
    if (pl == "on") {
      heater_is_on = heater_on();
    } else if (pl == "off") {
      heater_is_on = heater_off();
    } else if (pl == "status") {
      client.publish("footer-heater/status", heater_is_on ? "on" : "off");

      String str_min_temp;
      char ch_min_temp[4];
      str_min_temp = String(min_temp);
      str_min_temp.toCharArray(ch_min_temp, 4);
      client.publish("footer-sensor/limit-min-temp", ch_min_temp);
    }
  }
}

// ********************************************************
void setup() {
  // put your setup code here, to run once:
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);
  digitalWrite(PIN_LED, HIGH);
  Serial.begin(115200);

  Serial.println("------------------------------");
  Serial.println("setup WiFi");
  setup_wifi();

  tempSensors.begin();
  // Serial.println(tempSensors.getDeviceCount(), DEC);
  if (!tempSensors.getAddress(sensor_addr, 1)) Serial.println("Unable to find address for Device 1");

  client.setServer("dev.rightech.io", 1883);
  client.setCallback(handle_message);

  Serial.println("Attempting MQTT connection...");
  reconnect();

  EEPROM.begin(8);  // для esp8266/esp32
  // считывание последнего состояния Контроллера, если в EEPROM ахинея - записываем false
  int8_t _min_temp;
  EEPROM.get(0, _min_temp);
  if (_min_temp == 0) {
    EEPROM.put(0, min_temp);
    EEPROM.commit();  // для esp8266/esp32
  }

  Serial.print("Get MIN-TEMP: ");
  Serial.println(min_temp);
  Serial.println();
}

// ********************************************************
void loop() {
  // put your main code here, to run repeatedly:
  actualTimer = millis();


  if (!client.connected()) {
    Serial.println("Lost MQTT connection");
    reconnect();
  }

  client.loop();

  if (actualTimer - prevTimer >= intTimer) {
    prevTimer = actualTimer;

    Serial.println("...loop");

    tempSensors.requestTemperatures();  // Просим ds18b20 собрать данные
    delay(300);

    float tempC = tempSensors.getTempC(sensor_addr);
    if (tempC == -127) {
      tempC = random(-2, 3);
    }
    current_temp = tempC;

    char _payload[10];
    dtostrf(tempC, 6, 2, _payload);
    client.publish("footer-sensor/current-temp", _payload);

    Serial.print("Temp = ");
    Serial.println(tempC);

    bool current_status = heater_is_on;
    if (current_temp <= min_temp) {
      Serial.println("temp less then min");
      if (!heater_is_on) {
        heater_is_on = heater_on();
      }
    } else {
      if (heater_is_on) {
        heater_is_on = heater_off();
      }
    }
    if (current_status != heater_is_on) {
      client.publish("footer-heater/status", heater_is_on ? "on" : "off");
      Serial.println("temp change. footer-heater/status: " + heater_is_on ? "ON" : "OFF");
    }
  }
}

// ********************************************************
bool heater_off() {
  digitalWrite(PIN_RELAY, LOW);
  digitalWrite(PIN_LED, LOW);
  delay(500);
  return digitalRead(PIN_RELAY);
}

// ********************************************************
bool heater_on() {
  digitalWrite(PIN_RELAY, HIGH);
  digitalWrite(PIN_LED, HIGH);
  delay(500);
  return digitalRead(PIN_RELAY);
}
