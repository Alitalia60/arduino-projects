#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <EEPROM.h>

#define PIN_RELAY 5

// **************** PubSubClient ****************************
// Use WiFiClient class to create TCP connections
WiFiClient wi_fi_client;
PubSubClient client(wi_fi_client);

// ***************** sensors DS18B20 ***************************/
OneWire oneWire(2);
// Термо датчики

DallasTemperature tempSensors(&oneWire);
DeviceAddress sensor_addr;


const char* ssid = "TP-Link_3AAA";  // Название Вашей WiFi сети
// const char* ssid = "MT-PON-LITA";
const char* pswd = "06019013";

int8_t current_temp = 0;
int8_t min_temp = 1;

unsigned long prevTimer = 0;    // Variable used to keep track of the previous timer value
unsigned long actualTimer = 0;  // Variable used to keep track of the current timer value
const long intTimer = 15000;

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
      // if (!client.subscribe("room-sensor/#")) {
      //   Serial.println("subscribe room-sensor - false");
      // }
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


  if (String(topic) == "footer-sensor/limit-min-temp") {
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
}

// ********************************************************
void setup() {
  // put your setup code here, to run once:
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);
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
  // считывание последнего состояния Когтроллера, если в EEPROM ахинея - записываем false
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

    if (current_temp <= min_temp) {
      if (!digitalRead(PIN_RELAY)) {
        digitalWrite(PIN_RELAY, HIGH);
      }
    } else {
      if (digitalRead(PIN_RELAY)) {
        digitalWrite(PIN_RELAY, LOW);
      }
    }
    Serial.println(digitalRead(PIN_RELAY) ? "Relay is ON" : "Relay is OFF");
  }
}
