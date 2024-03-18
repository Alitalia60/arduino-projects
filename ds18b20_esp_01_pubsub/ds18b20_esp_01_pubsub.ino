/* 
Домашняя автоматизация 
WiFi комнатный датчик температуры
Передача данных через MQTT сервер
Тестировалось на Arduino IDE 2.3.2
Дата тестирования 11-3-2024г.
*/

#include <ESP8266WiFi.h>        // Подключаем библиотеку ESP8266WiFi
#include <OneWire.h>            // Подключаем библиотеку Wire
#include <DallasTemperature.h>  // Подключаем библиотеку DallasTemperature
#include <PubSubClient.h>

// #define OFFICE
#define DEBUG_ENABLE

#ifdef OFFICE
const char* ssid = "What_is_it";  // Название Вашей WiFi сети
const char* pswd = "Touran2017";  // Пароль от Вашей WiFi сети
#else
const char* ssid = "TP-Link_3AAA";  // Название Вашей WiFi сети
const char* pswd = "06019013";      // Пароль от Вашей WiFi сети
#endif

#define ONE_WIRE_BUS 2  // Указываем, к какому выводу подключена датчик
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

float room_current = 0;

//set interval for sending messages (milliseconds)
const long interval = 30000;
unsigned long previousMillis = 0;

//******************* MqttClient *****************************/
// const char broker[] = "test.mosquitto.org";
const char broker[] = "dev.rightech.io";
int port = 1883;

const char room_client_id[] = "room-sensor";

// Use WiFiClient class to create TCP connections
WiFiClient wi_fi_client;
PubSubClient client(wi_fi_client);

// ********************************************************
void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  #ifdef DEBUG_ENABLE
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  #endif

  // WiFi.mode(WiFiMode_t);
  WiFi.begin(ssid, pswd);

  #ifdef DEBUG_ENABLE
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  #endif

  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

}

// ********************************************************
String macToStr(const uint8_t* mac) {
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    if (i < 5)
      result += ':';
  }
  return result;
}

// ********************************************************
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {

    #ifdef DEBUG_ENABLE
    Serial.print("Attempting MQTT connection...");
    Serial.print("clientId= ");
    Serial.println(room_client_id);
    #endif

    if (client.connect(room_client_id, "alita60", "11071960")) {
      #ifdef DEBUG_ENABLE
      Serial.println("MQTT room-sensor connected");
      #endif
      delay(5000);
    }
  }
}



// ********************************************************
void setup() {
  // Serial.begin(9600);  // Скорость передачи 115200
  #ifdef DEBUG_ENABLE
  Serial.begin(115200);
  #endif

  delay(10);        // Пауза 10 мкс
  DS18B20.begin();  // Инициализация DS18B20
  setup_wifi();
  client.setServer(broker, 1883);
  // client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    DS18B20.requestTemperatures();
    room_current = DS18B20.getTempCByIndex(0);  // Запрос на считывание температуры
    // Check if reading was successful
    if (room_current != DEVICE_DISCONNECTED_C) {
      #ifdef DEBUG_ENABLE
      Serial.println(room_current);  // Отображение температуры
      #endif

      char payload[10];
      dtostrf(room_current, 6, 2, payload);
      client.publish("room-sensor/current-temp", payload);
      client.publish("room-sensor/status", "ok");
    } else {
      Serial.println("Error: Could not read temperature data");
      client.publish("room-sensor/status", "off");
    }
  }
}
