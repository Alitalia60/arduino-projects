//  Подключаем стандартную библиотеку для работы с Shield'ом по шине SPI
#include "SPI.h"
//  Подключаем стандартную библиотеку для работы с Ethernet
#include "Ethernet.h"
#include <ArduinoMqttClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <Bounce2.h>


#define  LCD_ENABLE
#define DEBUG_ENABLE

// ***************** Relays ***************************/
#define PIN_GK_ON 3
#define PIN_GK_BLOCK 4
#define PIN_TTK_ON 5
#define PIN_BOILER_ON 6
// кнопка включения управления от контроллера on/off
#define PIN_BUTTON_ON 7  // при нажатии на пин подается земля

// кнопка включения насоса ТТК on/off
#define PIN_BUTTON_TTK PIN_A0  // (14) при нажатии на пин подается земля
// когда светодиод горит - управление от контроллера
#define LED_ON PIN_A1  // (15)
// когда светодиод горит - включен насос ТТК
#define LED_TTK PIN_A2  // (16)

Bounce2::Button but_on = Bounce2::Button();
Bounce2::Button but_ttk = Bounce2::Button();

// Управление от комнатного датчика = false. Управление через MQTT = true
bool controller_on = true;
bool ttk_on = false;
int gk_current = 0;
int ttk_current = 0;

/******************* ethernet W5500 *****************************
3.3v	Питание от платы arduino		3.3v
MISO		D12
MOSI		D11
SS			D10
SCK			D13
GND			GND
RST			RST
INT			D2
*/

byte mac[] = {
  0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0x02
};

EthernetClient ethernetClient;

//******************* MqttClient *****************************/
// const char broker[] = "test.mosquitto.org";
const char broker[] = "dev.rightech.io";
int port = 1883;
const char boiler_topic[] = "boiler";
const char boiler_client_id[] = "boiler-controller";

MqttClient mqttClient(ethernetClient);

// ***************** sensors DS18B20 ***************************/
// int PIN_DS18B20 = 8;
// OneWire oneWire(PIN_DS18B20);
OneWire oneWire(2);
// Термо датчики

DallasTemperature tempSensors(&oneWire);
DeviceAddress GKSensorAddress, TTKSensorAddress;
// DeviceAddress GKSensorAddress = {0x28, 0xDD, 0xFE, 0x76, 0xE0, 0x01, 0x3C, 0x2B};
// DeviceAddress TTKSensorAddress = {0x28, 0xD8, 0x90, 0x75, 0xD0, 0x01, 0x3C, 0x25};

// ******************* LCD 1602 *****************************
#ifdef LCD_ENABLE
/*
питание +5v
SDA	-> A4
SCL	-> A5
*/
LiquidCrystal_I2C lcd(0x27, 16, 2);
#endif

unsigned long prevTimer = 0;    // Variable used to keep track of the previous timer value
unsigned long actualTimer = 0;  // Variable used to keep track of the current timer value
// const long intTimer = 15000;
const long intTimer = 3000;
// int queue = 0;

void setup() {

  // подавление дребезга
  but_on.attach(PIN_BUTTON_ON, INPUT);
  but_on.interval(5);
  but_on.setPressedState(LOW);

  but_ttk.attach(PIN_BUTTON_TTK, INPUT);
  but_ttk.interval(5);
  but_ttk.setPressedState(LOW);

#ifdef LCD_ENABLE
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("initialization");
  
  delay(1000);
#endif

  // ГК вкл/откл
  pinMode(PIN_GK_ON, OUTPUT);
  digitalWrite(PIN_GK_ON, HIGH);
  // ГК блокировка комнатного датчика
  pinMode(PIN_GK_BLOCK, OUTPUT);
  digitalWrite(PIN_GK_BLOCK, HIGH);
  // ТТК вкл/откл
  pinMode(PIN_TTK_ON, OUTPUT);
  digitalWrite(PIN_TTK_ON, HIGH);
  // водонагреватель вкл/откл
  pinMode(PIN_BOILER_ON, OUTPUT);
  digitalWrite(PIN_BOILER_ON, HIGH);
  //
  // кнопки
  pinMode(PIN_BUTTON_ON, INPUT);
  pinMode(PIN_BUTTON_TTK, INPUT);

//  Инициируем работу с монитором последовательного порта на скорости 9600 бод
#ifdef DEBUG_ENABLE
  Serial.begin(9600);
  tempSensors.begin();
  Serial.print("Датчиков = ");
  Serial.println(tempSensors.getDeviceCount(), DEC);
  if (!tempSensors.getAddress(TTKSensorAddress, 0)) Serial.println("Unable to find address for Device 1");
  if (!tempSensors.getAddress(GKSensorAddress, 1)) Serial.println("Unable to find address for Device 0");
#endif

  while (Ethernet.linkStatus() != LinkON) {
    ser_print("Ethernet not ready");
    // Serial.println("Ethernet not ready");
    delay(3000);
  };
  if (Ethernet.begin(mac) == 0) {
    ser_print("Failed to configure Ethernet using DHCP");
    // Serial.println("Failed to configure Ethernet using DHCP");
    // no point in carrying on, so do nothing forevermore:
    for (;;)
      ;
  }
  // print your local IP address:
  // ser_print(Ethernet.localIP());
  // Serial.println(Ethernet.localIP());

  // Serial.print("Attempting to connect to the MQTT broker: ");
  // Serial.println(broker);

  mqttClient.setId(boiler_client_id);
  // mqttClient.setUsernamePassword("alita60", "11071960");
  if (!mqttClient.connect(broker, port)) {
    Serial.print("MQTT failed! Error = ");
    Serial.println(mqttClient.connectError());

    while (1)
      ;
  }

  ser_print("Connected MQTT");
  ser_print("");
  // Serial.println("Connected MQTT");
  // Serial.println();
  mqttClient.onMessage(handle_message);
  // subscribe to a topic
  mqttClient.subscribe(boiler_topic);

  // topics can be unsubscribed using:
  // mqttClient.unsubscribe(topic);

  ser_print("Waiting for messages on topic: ");
  ser_print(boiler_topic);
  ser_print("");
  // Serial.print("Waiting for messages on topic: ");
  // Serial.println(boiler_topic);
  // Serial.println();
}

void loop() {
  // кнопка включения ON
  but_on.update();
  if (but_on.pressed()) {
    controller_on = !controller_on;
    digitalWrite(LED_ON, controller_on ? HIGH : LOW);
  }

  // кнопка включения TTK
  but_ttk.update();
  if (but_ttk.pressed()) {
    if (ttk_current < 40)  // нельзя выключать разогретый ТТК
    {
      ttk_on = !ttk_on;
      digitalWrite(LED_TTK, ttk_on ? HIGH : LOW);  // сведодиод насоса ТТК
      // отключение ГК
      digitalWrite(PIN_GK_ON, ttk_on ? LOW : ttk_on);     // реле ГК - выключение
      digitalWrite(PIN_GK_BLOCK, ttk_on ? LOW : ttk_on);  // блокировка комнатного термодатчика - выключение
      digitalWrite(PIN_TTK_ON, ttk_on ? HIGH : LOW);      // реле насоса ТТК
    }
  }

  actualTimer = millis();
  if (actualTimer - prevTimer >= intTimer) {
    prevTimer = actualTimer;
      tempSensors.requestTemperatures();  // Просим ds18b20 собрать данные
      delay(300);
      gk_current = get_temp(GKSensorAddress, 0);
      ttk_current = get_temp(TTKSensorAddress, 1);
      mqttClient.poll();
      mqttClient.beginMessage("boiler/gk-current-temp");
      mqttClient.print(gk_current);
      mqttClient.endMessage();
    
        lcd_print(0, 0, "GK =" + gk_current);
      // lcd.setCursor(0, 0);
      // lcd.print("GK =" + gk_current);

      mqttClient.beginMessage("boiler/ttk-current-temp");
      mqttClient.print(ttk_current);
      mqttClient.endMessage();

      lcd_print(0, 1, "TTK =" + ttk_current);
      // lcd.setCursor(0, 0);
      // lcd.print("TTK =" + ttk_current);
      
      
  }
}

void handle_message(int mes_size) {

  ser_print("message topic=");
  // Serial.println("message topic=");
  String mess_topic = mqttClient.messageTopic();
  ser_print(mess_topic);
  // Serial.println(mess_topic);

  String command;
  while (mqttClient.available()) {
    command += (char)mqttClient.read();
  }
  // Serial.print("payload= ");
  // Serial.println(command);
  // Serial.println();
  if (mess_topic == "boiler/ttk-on" && controller_on) {
    if (command == "1") {
      digitalWrite(PIN_TTK_ON, LOW);
    } else if (command == "0") {
      digitalWrite(PIN_TTK_ON, HIGH);
    };
  };

  if (mess_topic == "boiler/gk-on") {
    if (command == "1") {
      digitalWrite(PIN_GK_ON, LOW);
      digitalWrite(PIN_GK_BLOCK, LOW);
    } else if (command == "0") {
      digitalWrite(PIN_GK_ON, HIGH);
      digitalWrite(PIN_GK_BLOCK, HIGH);
    };
  }

  if (mess_topic == "boiler/boiler-on") {
    if (command == "1") {
      digitalWrite(PIN_BOILER_ON, LOW);
    } else if (command == "0") {
      digitalWrite(PIN_BOILER_ON, HIGH);
    };
  };
}

float get_temp(DeviceAddress sensor_addr, int num) {
  // считывание термодатчика
  float tempC = tempSensors.getTempC(sensor_addr);
  if (tempC == DEVICE_DISCONNECTED_C) {
    Serial.println(num);
    ser_print("Error: temperature data");
    // Serial.println(sensor_addr);
    return 999;
  }

  return tempC;
}

void lcd_print(byte row, byte line, String text) {
#ifdef LCD_ENABLE
  lcd.setCursor(row, line);
  lcd.print(text);
#endif
}

void ser_print(String text) {
#ifdef DEBUG_ENABLE
  Serial.println(text);
#endif
}