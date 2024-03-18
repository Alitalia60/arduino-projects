#define OFFICE
// #define LCD_ENABLE

// Подключаем стандартную библиотеку для работы с Shield'ом по шине SPI
// #include "SPI.h"
#include <ESP8266WiFi.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <PubSubClient.h>
#include <Bounce2.h>
#ifdef LCD_ENABLE
#include <LiquidCrystal_I2C.h>
#endif


#ifdef OFFICE
// const char* ssid = "What_is_it";
// const char* pswd = "Touran2017";
const char* ssid = "TP-Link_C4E8";
const char* pswd = "95125930";
#else  //домашняя сеть
// const char* ssid = "TP-Link_3AAA";  // Название Вашей WiFi сети
const char* ssid = "MT-PON-LITA";
const char* pswd = "06019013";
#endif

#define ON true
#define OFF false
#define RELAY_ON LOW
#define RELAY_OFF HIGH

// ***************** Relays ***************************/
#define PIN_GK_RELAY 1 //tx

//реле блокировки комнатного датчика - нормально замкнуто
#define PIN_GK_BLOCK 3 // rx
#define PIN_TTK_RELAY 16

//реле бойлера - нормально замкнуто
#define PIN_BOILER_RELAY 0
// кнопка включения управления от контроллера on/off
#define PIN_BUTTON_ON 14  // при нажатии на пин подается земля

// кнопка включения насоса ТТК on/off
#define PIN_BUTTON_TTK 12  // при нажатии на пин подается земля
// когда светодиод горит - управление от контроллера
#define LED_ON 13  // GREEN
// когда светодиод горит - включен насос ТТК
#define LED_TTK 15  //

Bounce2::Button but_on = Bounce2::Button();
Bounce2::Button but_ttk = Bounce2::Button();

// Управление от комнатного датчика = false. Управление через MQTT = true
bool controller_on = false;
bool controller_wait = false;  //контроллер ждет остывания ТТК
bool ttk_wait = false;  //насос ТТК  ждет остывания ТТК

//температуры начальные
int room_current = 0;
int ttk_current = 0;
int gk_current = 0;
int ttk_cold = 35;  // Ниже этой температуры можно выключать работающий насос ТТК

//Реле управления котлами  и бойлером
bool ttk_relay;
bool gk_relay;
bool boiler_relay;

// ***************** sensors DS18B20 ***************************/
OneWire oneWire(2);
// Термо датчики

DallasTemperature tempSensors(&oneWire);
DeviceAddress GKSensorAddress, TTKSensorAddress;

// ******************* LCD 1602 *****************************
#ifdef LCD_ENABLE
/*
питание +5v
SDA	-> A4
SCL	-> A5
*/
LiquidCrystal_I2C lcd(0x27, 16, 2);
#endif

// Use WiFiClient class to create TCP connections
WiFiClient wi_fi_client;
PubSubClient client(wi_fi_client);

unsigned long prevTimer = 0;    // Variable used to keep track of the previous timer value
unsigned long actualTimer = 0;  // Variable used to keep track of the current timer value
// const long intTimer = 15000;
const long intTimer = 10000;
// int queue = 0;

void initialization() {
  Serial.println("init");
  delay(3);

  ttk_relay = RELAY_OFF;

  gk_relay = RELAY_OFF;

  boiler_relay = RELAY_OFF;

  //реле выключено - HIGH или RELAY_OFF
  // ГК вкл/откл
  digitalWrite(PIN_GK_RELAY, RELAY_OFF);
  // ГК блокировка комнатного датчика, если контроллер активен

  digitalWrite(PIN_GK_BLOCK, controller_on ? RELAY_ON : RELAY_OFF);
  // ТТК вкл/откл
  digitalWrite(PIN_TTK_RELAY, RELAY_OFF);
  // водонагреватель вкл/откл

  digitalWrite(PIN_BOILER_RELAY, RELAY_ON);
  //
  // кнопки
  // pinMode(PIN_BUTTON_ON, INPUT); по умолчанию INPUT
  // pinMode(PIN_BUTTON_TTK, INPUT); по умолчанию INPUT
}

// ********************************************************
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // if (client.connect("main-controller", "alita60", "11071960")) {
    if (client.connect("main-controller")) {
      Serial.println("MQTT main-controller connected");
      // Once connected, publish an announcement...
      // client.publish(topic, ("connected " + composeClientID()).c_str() , true );
      // client.publish("room-sensor", ("connected " + composeClientID()).c_str() , true );
      delay(5000);
    }
  }
}

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
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
}

// ***********************************************
void setup() {
  pinMode(PIN_GK_RELAY, OUTPUT);
  pinMode(PIN_GK_BLOCK, OUTPUT);
  pinMode(PIN_TTK_RELAY, OUTPUT);
  pinMode(PIN_BOILER_RELAY, OUTPUT);


  Serial.begin(115200);
  // подавление дребезга
  but_on.attach(PIN_BUTTON_ON, INPUT);
  but_on.interval(5);
  but_on.setPressedState(LOW);

  but_ttk.attach(PIN_BUTTON_TTK, INPUT);
  but_ttk.interval(5);
  but_ttk.setPressedState(LOW);

  // #ifdef LCD_ENABLE
  //   lcd.init();
  //   lcd.backlight();
  //   lcd.setCursor(0, 0);
  //   lcd.print("initialization");
  //   delay(1000);
  // #endif

  initialization();

  Serial.print("Датчиков = ");
  tempSensors.begin();
  Serial.println(tempSensors.getDeviceCount(), DEC);
  if (!tempSensors.getAddress(TTKSensorAddress, 0)) Serial.println("Unable to find address for Device 1");
  if (!tempSensors.getAddress(GKSensorAddress, 1)) Serial.println("Unable to find address for Device 0");

  setup_wifi();
  client.setServer("dev.rightech.io", 1883);
  client.setCallback(handle_message);
}

// ***********************************************
void handle_btn_on(String action = "off") {
  // обработка нажатия кнопки включения выключения котроллера или команды от брокера
  if (action == "on" && controller_on == OFF) {
    Serial.println("Controller starting = ON");
    controller_on = ON;
    controller_wait = false;
    digitalWrite(PIN_GK_BLOCK, RELAY_ON);  // блокировка комнатного термодатчика - включение
  } else if (action == "off" && controller_on == ON) {

    Serial.println("Controller to be OFF");
    Serial.print("ttk_current = ");
    Serial.println(ttk_current);
    Serial.print("ttk_cold = ");
    Serial.println(ttk_cold);
    if (ttk_current < ttk_cold) {  // нельзя выключать разогретый ТТК
    Serial.println("Controller allow to be OFF");
      ttk_relay = OFF;
      gk_relay = OFF;
      boiler_relay = ON;
      controller_wait = false;
      controller_on = OFF;
      // отключение ГК
      digitalWrite(PIN_GK_RELAY, RELAY_OFF);     // реле ГК - выключение
      digitalWrite(PIN_GK_BLOCK, RELAY_OFF);     // блокировка комнатного термодатчика
      digitalWrite(PIN_TTK_RELAY, RELAY_OFF);    // реле насоса ТТК
      digitalWrite(PIN_BOILER_RELAY, RELAY_ON);  // реле бойлера включено - бойлер на саморегулировании
    }
  }
  if (controller_wait) {
    client.publish("main/controller", "is waiting");
  } else {
    client.publish("main/controller", controller_on ? "on" : "off");
  }
}

// ***********************************************
void handle_btn_ttk(String action = "off") {
  // обработка нажатия кнопки включения выключения насоса ТТК или команды от брокера
  // нельзя выключить контроллер, если насос ТТК включен и температура ТТК > ttk_cold градусов
  Serial.println("Button TTK ON/OFF pressed or broker sent command");
  if (controller_on) {
    if (ttk_current < ttk_cold) {
      controller_on = !controller_on;
      digitalWrite(PIN_GK_BLOCK, controller_on ? LOW : HIGH);  //блокируем комнатный датчик
      digitalWrite(PIN_TTK_RELAY, LOW);                        // реле насоса ТТК
    } else {
      ttk_wait = true;
    }
  }
}

// ***********************************************
void check_waiting() {
  if (ttk_wait) {
    if (ttk_current < ttk_cold) {
      ttk_wait = false;
      ttk_relay = RELAY_OFF;
      digitalWrite(PIN_GK_BLOCK, RELAY_ON);    //блокируем комнатный датчик
      digitalWrite(PIN_TTK_RELAY, RELAY_OFF);  // реле насоса ТТК
    }
  }

  if (controller_wait) {
    if (ttk_current < ttk_cold) {
      controller_wait = false;
      controller_on = false;
      initialization();
    }
  }
}

// ***********************************************
void loop() {
  // confirm still connected to mqtt server
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  client.subscribe("main/boiler");
  // client.subscribe("room-sensor/#");

  // кнопка включения ON
  but_on.update();
  if (but_on.pressed()) {
    handle_btn_on();
  }

  // кнопка включения TTK
  but_ttk.update();
  if (but_ttk.pressed()) {
    handle_btn_ttk();
  }


  // опрос состояния выводов реле ГКб Бойлера и ТТК
  bool gk_status = !digitalRead(PIN_GK_RELAY);
  bool ttk_status = !digitalRead(PIN_TTK_RELAY);
  bool boiler_status = !digitalRead(PIN_BOILER_RELAY);
  // опрос датчиков температуры каждые  intTimer/10000 секунд
  actualTimer = millis();
  if (actualTimer - prevTimer >= intTimer) {
    prevTimer = actualTimer;
    check_waiting();
    //светодиоды в соответствии со статусами
    client.publish("main/controller", controller_on ? "on" : "off");
    digitalWrite(LED_ON, controller_on ? HIGH : LOW);
    client.publish("main/gk-status", gk_status ? "on" : "off");
    digitalWrite(LED_TTK, ttk_status ? HIGH : LOW);
    client.publish("main/ttk-status", ttk_status ? "on" : "off");
    client.publish("main/boiler-status", boiler_status ? "on" : "off");


    tempSensors.requestTemperatures();  // Просим ds18b20 собрать данные
    delay(300);

    gk_current = get_temp(GKSensorAddress, 0);
    char gk_payload[10];
    dtostrf(gk_current, 6, 2, gk_payload);

    ttk_current = get_temp(TTKSensorAddress, 1);
    char ttk_payload[10];
    dtostrf(ttk_current, 6, 2, ttk_payload);

    client.publish("main/ttk-current-temp", ttk_payload);

    lcd_print(0, 0, "TTK =" + ttk_current);

    client.publish("main/gk-current-temp", gk_payload);

    lcd_print(0, 1, "GK =" + gk_current);
  }
}

// ***********************************************
void handle_message(char* topic, byte* payload, unsigned int length) {

  String str_topic = String(topic);
  Serial.println();
  Serial.print("message topic= ");
  Serial.println(topic);
  Serial.println(str_topic);
  Serial.println();
  Serial.print("message payload= ");
  String pl;

  for (int i = 0; i < length; i++) {
    pl += (char)payload[i];
  }
  Serial.print(pl);

  //--------------------------------
  if (String(topic) == "main/ttk") {
    if (pl.indexOf("on") >= 0) {
      ttk_wait = ttk_relay == ON ? false : true;
      handle_btn_ttk("on");
    } else if (pl.indexOf("off") >= 0) {
      ttk_wait = ttk_relay == OFF ? false : true;
      handle_btn_ttk("off");
    }
    
  }
  //--------------------------------
  if (String(topic) == "main/gk") {
    if (pl.indexOf("on") >= 0) {
      gk_relay = RELAY_ON;
    } else if (pl.indexOf("off") >= 0) {
      gk_relay = RELAY_OFF;
    }
    digitalWrite(PIN_GK_RELAY, gk_relay);
  }
  //--------------------------------
  if (String(topic) == "main/boiler") {
    if (pl.indexOf("on") >= 0) {
      boiler_relay = RELAY_ON;
    } else if (pl.indexOf("off") >= 0) {
      boiler_relay = RELAY_OFF;
    }
    digitalWrite(PIN_BOILER_RELAY, boiler_relay);
  }
  //--------------------------------
  if (String(topic) == "room-sensor/current-temp") {
    //string to float
    room_current = pl.toFloat();
  }

  //--------------------------------
  if (String(topic) == "main/controller") {
    if (pl.indexOf("on") >= 0) {
      controller_wait = controller_on == ON ? false : true;
      handle_btn_on("on");
    } else if (pl.indexOf("off") >= 0) {
      controller_wait = controller_on == OFF ? false : true;
      handle_btn_on("off");
    }
  }
}

// ***********************************************
float get_temp(DeviceAddress sensor_addr, int num) {
  // считывание термодатчика
  float tempC = tempSensors.getTempC(sensor_addr);
  if (tempC == DEVICE_DISCONNECTED_C) {
    // Serial.println(num);
    // Serial.println("Error: temperature data");
    // Serial.println(sensor_addr);
  }
  // Serial.println("Sensor id=");
  // Serial.println(num);
  return tempC;
}

// ***********************************************
#ifdef LCD_ENABLE
void lcd_print(byte row, byte line, String text) {
  lcd.setCursor(row, line);
  lcd.print(text);
}
#else
void lcd_print(byte row, byte line, String text) {
  //nothing
}
#endif
