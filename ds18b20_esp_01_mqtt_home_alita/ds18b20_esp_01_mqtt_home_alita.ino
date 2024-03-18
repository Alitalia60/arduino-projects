/* 
Домашняя автоматизация 
WiFi комнатный датчик температуры
Передача данных через MQTT сервер
Тестировалось на Arduino IDE 2.3.2
Дата тестирования 25-2-2024г.
*/

#include <ESP8266WiFi.h>        // Подключаем библиотеку ESP8266WiFi
#include <OneWire.h>            // Подключаем библиотеку Wire
#include <DallasTemperature.h>  // Подключаем библиотеку DallasTemperature
#include <ArduinoMqttClient.h>

// #define OFFICE

#ifdef OFFICE
const char* ssid = "What_is_it";  // Название Вашей WiFi сети
const char* password = "Touran2017";  // Пароль от Вашей WiFi сети
#else
const char* ssid = "TP-Link_3AAA";  // Название Вашей WiFi сети
const char* password = "06019013";  // Пароль от Вашей WiFi сети
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
MqttClient mqttClient(wi_fi_client);

void setup() {
  Serial.begin(9600);  // Скорость передачи 115200
  delay(10);           // Пауза 10 мкс
  DS18B20.begin();     // Инициализация DS18B20

  // DS18B20.setResolution()

  Serial.println("");              // Печать пустой строки
  Serial.print("Connecting to ");  // Печать "Подключение к:"
  Serial.println(ssid);            // Печать "Название Вашей WiFi сети"

  WiFi.begin(ssid, password);  // Подключение к WiFi Сети

  while (WiFi.status() != WL_CONNECTED)  // Проверка подключения к WiFi сети
  {
    delay(500);         // Пауза 500 мкс
    Serial.print(".");  // Печать "."
  }
  Serial.println("");                // Печать пустой строки
  Serial.println("WiFi connected");  // Печать "Подключение к WiFi сети осуществлено"
  delay(5000);                       // Пауза 5 000 мс
  Serial.println(WiFi.localIP());

  Serial.print("Attempting to connect to the MQTT broker: ");
  Serial.println(broker);
  mqttClient.setId(room_client_id);
  mqttClient.setUsernamePassword("alita60", "11071960");
  // if (!mqttClient.connect(broker, port)) {
  //   Serial.print("MQTT connection failed! Error code = ");
  //   Serial.println(mqttClient.connectError());

  //   while (1)
  //     ;
  // }  // Печатаем полученный IP-адрес ESP
  // Serial.println("You're connected to the MQTT broker!");
  // Serial.println();
}

void loop() {
  
  Serial.println("You're connected to the MQTT broker!");
  Serial.println();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    // save the last time a message was sent
    previousMillis = currentMillis;

    if (!mqttClient.connect(broker, port)) {
    Serial.print("MQTT connection failed! Error code = ");
    Serial.println(mqttClient.connectError());

    while (1)
      ;
  }  // Печатаем полученный IP-адрес ESP

    mqttClient.poll();
    
    DS18B20.requestTemperatures();
    room_current = DS18B20.getTempCByIndex(0);  // Запрос на считывание температуры
    Serial.println(room_current);                   // Отображение температуры


    mqttClient.beginMessage("room-sensor/current-temp");
    mqttClient.print(room_current);
    mqttClient.endMessage();
    mqttClient.stop()
  }
}
