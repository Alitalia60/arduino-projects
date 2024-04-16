#define DEBUG_ENABLE

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <EEPROM.h>

#include <LiquidCrystal_I2C.h>

#include <EMailSender.h>

#define ON true
#define OFF false
#define RELAY_ON LOW
#define RELAY_OFF HIGH

// ***************** Relays ***************************/
// #define PIN_GK_RELAY 5
#define PIN_GK_RELAY 14

// реле блокировки комнатного датчика - нормально замкнуто
//  #define PIN_GK_BLOCK 4
#define PIN_GK_BLOCK 16

#define PIN_TTK_RELAY 13

// реле бойлера - нормально замкнуто
#define PIN_BOILER_RELAY 12

// кнопка включения насоса ТТК on/off
#define PIN_BUTTON_TTK 0  // при нажатии на пин подается земля
// когда светодиод горит - управление от контроллера
#define LED_ON 15  // GREEN

// const char *ssid = "TP-Link_3AAA";  // Название Вашей WiFi сети
const char *ssid = "MT-PON-LITA";
const char *pswd = "06019013";

// Use WiFiClient class to create TCP connections
WiFiClient wi_fi_client;
PubSubClient client(wi_fi_client);

// ***************** sensors DS18B20 ***************************/
OneWire oneWire(2);
// Термо датчики

DallasTemperature tempSensors(&oneWire);
DeviceAddress GKSensorAddress, TTKSensorAddress;

// ******************* LCD 1602 *****************************
/*
питание +5v
SDA	-> A4
SCL	-> A5
*/
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ******************* emailSend *****************************
EMailSender emailSend("lita.private@bk.ru", "9EVNjcMrrc513mN9eKBk", "lita.private@bk.ru", "smtp.mail.ru", 465);

// температуры начальные
int8_t room_current = 0;
int8_t room_target = 17;
int8_t ttk_current = 0;
int8_t gk_current = 0;
int8_t ttk_cold = 35;  // Ниже этой температуры можно выключать работающий насос ТТК
int8_t ttk_max = 70;   // Выше этой температуры - ТРЕВОГА

unsigned long prevTimer = 0;      // Variable used to keep track of the previous timer value
unsigned long actualTimer = 0;    // Variable used to keep track of the current timer value
unsigned long actualTimerGk = 0;  // Variable used to keep track of the current timer value
const long intTimer = 15000;      // Период опроса датчиков

unsigned long prevBrokerTimer = 0;   //
const long intBrokerTimer = 600000;  // Период попытки подключения  брокера  10 мин

unsigned long gkPrevTimer = 0;
const long gkWaitTimer = 30000;  // время для выбега насоса ГК после его выключения

bool controller_is_active = false;
String controller_is_waiting = "";
String ttk_is_waiting = "";
String gk_is_waiting = "";
String boiler_is_waiting = "";
bool ttk_is_on = false;
bool gk_is_on = false;
bool boiler_is_on = false;

// int8_t attemps = 50;  //макс число попыток соединиться с брокером
int8_t attempt = 0;       //макс число попыток соединиться с брокером
int8_t max_attempts = 5;  //макс число попыток соединиться с брокером
// int8_t max_attempts = 2;  //макс число попыток соединиться с брокером
bool broker_is_online = false;

const char *broker_url = "localhost";


// ********************************************************
void send_email() {
#ifndef DEBUG_ENABLE
  EMailSender::EMailMessage message;
  message.subject = "MQQT broker";
  message.message = "controller Lost MQQT broker";

  EMailSender::Response resp = emailSend.send("litaservice@bk.ru", message);

  Serial.println("Sending status: ");

  Serial.println(resp.status);
  Serial.println(resp.code);
  Serial.println(resp.desc);
#endif
}
// ********************************************************
void reconnect() {
  // Loop until we're reconnected
  Serial.println("Lost MQTT connection");
  while (!client.connected() && attempt > 0) {
    Serial.print("Attempt remained =");
    Serial.println(attempt);
    // Attempt to connect
    if (client.connect("main-controller", "alita60", "11071960")) {
      // if (client.connect("main-controller")) {
      Serial.println("Connected MQTT 'main-controller'");
      if (!client.subscribe("main/#")) {
        Serial.println("subscribe 'main' - false");
      }
      if (!client.subscribe("room-sensor/#")) {
        Serial.println("subscribe room-sensor - false");
      }
      broker_is_online = true;
    } else {
      attempt -= 1;
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
    delay(2000);
  }
  if (attempt <= 0) {
    broker_is_online = false;
    Serial.println("reconnect: Broker is not found");
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

  lcd.clear();
  lcd.setCursor(1, 0);
  lcd.print("WiFi connected");
  lcd.setCursor(1, 1);
  lcd.print(WiFi.localIP());
  // delay(1000);

  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  delay(3000);
}

// ***********************************************
void handle_controller() {
  // Serial.println("DEBUG: handle_controller");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Controller is waiting: ");
  lcd.setCursor(4, 1);
  lcd.print(controller_is_waiting);

  if (controller_is_waiting == "on") {
    controller_is_active = true;
    controller_is_waiting = "";
    digitalWrite(PIN_GK_BLOCK, RELAY_ON);
  } else if (controller_is_waiting == "off") {
    // Выключить контроллер можно только при холодном ТТК
    ttk_is_waiting = "off";
    // ГК выключаем сразу;
    // gk_is_on = false;
    gk_is_waiting = "off";
    // бойлер д.б. включен
    if (!boiler_is_on) {
      digitalWrite(PIN_BOILER_RELAY, RELAY_OFF);  // Бойлер работает автономно, реле бойлера нормально замкнуто
    }
    if (ttk_current < ttk_cold) {  // Котел ТТК остыл
      controller_is_active = false;
      controller_is_waiting = "";
      digitalWrite(PIN_GK_BLOCK, RELAY_OFF);
      if (ttk_is_on) {
        ttk_is_on = false;
        digitalWrite(PIN_TTK_RELAY, RELAY_OFF);
        ttk_is_waiting = "";
      }
    }
  }
  bool is_active = false;
  EEPROM.get(0, is_active);
  if (is_active != controller_is_active) {
    EEPROM.put(0, controller_is_active);
    EEPROM.commit();  // для esp8266/esp32
    Serial.print("put EEPROM controller_is_active=");
    Serial.println(controller_is_active);
  }

  digitalWrite(LED_ON, !controller_is_active ? LOW : HIGH);
  if (broker_is_online) {
    client.publish("main/controller-status", controller_is_active ? "on" : "off");
  }
  Serial.println(controller_is_active ? "Controller is ENABLED" : "Controller is DISABLED");

  // delay(1000);
}

// ***********************************************
void handle_gk() {
  // Serial.println("DEBUG: handle_gk");
  /* при включении ГК:
    - температура ТТК менее 35, иначе продолжать ждать остывания
    - насос ТТК отключить
   */
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("GK is waiting: ");
  lcd.setCursor(4, 1);
  lcd.print(gk_is_waiting);

  if (gk_is_waiting == "on") {
    if (!ttk_is_on) {
      gk_is_on = true;
      digitalWrite(PIN_GK_RELAY, RELAY_ON);
      gk_is_waiting = "";
    } else {
      // Работает ТТК, проверяем остывание
      if (ttk_current < ttk_cold) {
        // gk_relay = RELAY_ON;
        Serial.println("ttk_current < ttk_cold. It is allowed GK to be ON  ");
        ttk_is_on = false;
        gk_is_on = true;

        digitalWrite(PIN_TTK_RELAY, RELAY_OFF);
        digitalWrite(PIN_GK_RELAY, RELAY_ON);
        // Serial.println("gk_relay = RELAY_ON");
        gk_is_waiting = "";
      }
    }
  } else if (gk_is_waiting == "off") {
    // gk_relay = RELAY_OFF;
    gk_is_on = false;
    digitalWrite(PIN_GK_RELAY, RELAY_OFF);
    gk_is_waiting = "";
    // Serial.println("gk_relay = RELAY_OFF");
  }

  Serial.print("handle_gk: GK is ");
  Serial.println(gk_is_on ? "ON" : "OFF");
  if (broker_is_online) {
    client.publish("main/gk-status", gk_is_on ? "on" : "off");
  }

  // lcd.clear();
  // lcd.setCursor(0, 0);
  // lcd.print(gk_is_on ? "GK is ON" : "GK is OFF");
  // delay(1000);
}

// ***********************************************
void handle_ttk() {
  // Serial.println("DEBUG: handle_ttk");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("TTK is waiting: ");
  lcd.setCursor(4, 1);
  lcd.print(ttk_is_waiting);

  if (ttk_is_waiting == "on") {
    // Serial.println("DEBUG: handle_ttk. 1");
    /* При активном контроллере выключить  ГК, выждать 1 мин  */
    if (controller_is_active) {
      // Serial.println("DEBUG: handle_ttk. 2");
      if (!gk_is_on) {
        // Serial.println("DEBUG: handle_ttk. 3");
        if (actualTimer - gkPrevTimer >= gkWaitTimer) {
          // Serial.println("DEBUG: handle_ttk. 4");
          gkPrevTimer = actualTimer;
          // можно включать насос ТТК
          ttk_is_on = true;
          digitalWrite(PIN_TTK_RELAY, RELAY_ON);
          ttk_is_waiting = "";
        }
      } else {
        // Serial.println("DEBUG: handle_ttk. 5");
        // Сначала отключим ГК
        gk_is_waiting = "off";
      }
      // Контроллер не активен- отказано включать ТТК
    } else {
      // Serial.println("DEBUG: handle_ttk. 6");
      ttk_is_waiting = "";
    }
  } else if (ttk_is_waiting == "off") {
    // Serial.println("DEBUG: handle_ttk. 7");
    if (ttk_current < ttk_cold) {
      // Serial.println("DEBUG: handle_ttk. 8");
      Serial.print("handle_ttk: ttk_current < ttk_cold ");
      // если ТТК остыл - можно выключать
      ttk_is_on = false;
      digitalWrite(PIN_TTK_RELAY, RELAY_OFF);
      ttk_is_waiting = "";
    } else {
      // Serial.println("DEBUG: handle_ttk. 9");
      // Serial.print("handle_ttk: ttk HOT - waiting to OFF");
    }
  }

  Serial.print("handle_ttk: TTK is ");
  Serial.println(ttk_is_on ? "ON" : "OFF");
  if (broker_is_online) {
    // Serial.println("DEBUG: handle_ttk. 10");
    client.publish("main/ttk-status", ttk_is_on ? "on" : "off");
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(ttk_is_on ? "TTK is ON" : "TTK is OFF");
  // delay(1000);
}

// ***********************************************
void handle_boiler() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Boiler is waiting: ");
  lcd.setCursor(4, 1);
  lcd.print(boiler_is_waiting);

  if (boiler_is_waiting == "on") {
    digitalWrite(PIN_BOILER_RELAY, RELAY_OFF);
    boiler_is_waiting = "";
    boiler_is_on = true;
  } else if (boiler_is_waiting == "off") {
    digitalWrite(PIN_BOILER_RELAY, RELAY_ON);
    boiler_is_waiting = "";
    boiler_is_on = false;
  }
  if (broker_is_online) {
    client.publish("main/boiler-status", boiler_is_on ? "on" : "off");
  }


  // lcd.clear();
  // lcd.setCursor(0, 0);
  // lcd.print(boiler_is_on ? "Boiler is ON" : "Boiler is OFF");
  // delay(1000);
}

// ***********************************************
void publish_statuses() {
  String str_room_target;
  char ch_room_target[4];
  str_room_target = String(room_target);
  str_room_target.toCharArray(ch_room_target, 4);

  client.publish("main/room-target-temp", ch_room_target);
  client.publish("main/controller-status", controller_is_active ? "on" : "off");
  client.publish("main/gk-status", gk_is_on ? "on" : "off");
  client.publish("main/ttk-status", ttk_is_on ? "on" : "off");
  client.publish("main/boiler-status", boiler_is_on ? "on" : "off");
}
// ***********************************************
void handle_message(char *topic, byte *payload, unsigned int length) {

  String str_topic = String(topic);
  String pl;

  for (int i = 0; i < length; i++) {
    pl += (char)payload[i];
  }

  // Serial.print("topic = ");
  // Serial.println(topic);
  // Serial.print("payload = ");
  // Serial.println(pl);
  // Serial.println("****");

  //================= Room =====================
  if (String(topic) == "room-sensor/current-temp") {
    if (controller_is_waiting == "" && ttk_is_waiting == "" && gk_is_waiting == "") {

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("ROOM   GK   TTK");
      lcd.setCursor(1, 1);
      lcd.print(room_current);
      lcd.setCursor(6, 1);
      lcd.print(gk_current);
      lcd.setCursor(12, 1);
      lcd.print(ttk_current);
    }
    room_current = pl.toInt();
  }

  //================= Room target temp=====================
  if (String(topic) == "main/room-target-temp") {

    int recieved_room_target = pl.toInt();
    recieved_room_target = min(recieved_room_target, 25);
    recieved_room_target = max(recieved_room_target, 10);

    int8_t _room_target = 0;
    EEPROM.get(4, _room_target);

    if (_room_target != recieved_room_target) {
      EEPROM.put(4, recieved_room_target);
      EEPROM.commit();  // для esp8266/esp32
      room_target = recieved_room_target;
    }
  }

  //================= ТТК =====================
  if (String(topic) == "main/ttk") {
    if (pl.indexOf("on") >= 0) {
      ttk_is_waiting = "on";
    } else if (pl.indexOf("off") >= 0) {
      ttk_is_waiting = "off";
    }
  }

  //=================ГК=====================
  if (String(topic) == "main/gk") {
    // Нельзя включать ГК вручную, если управление под контроллером
    if (pl.indexOf("on") >= 0) {
      gk_is_waiting = "on";
    } else if (pl.indexOf("off") >= 0) {
      gk_is_waiting = "off";
    }
  }
  //=================Бойлер=====================
  if (String(topic) == "main/boiler") {
    if (pl.indexOf("on") >= 0) {
      boiler_is_waiting = "on";
    } else if (pl.indexOf("off") >= 0) {
      boiler_is_waiting = "off";
    }
  }

  //=================Контроллер=====================
  if (String(topic) == "main/controller") {
    Serial.println("main/controller");
    if (pl.indexOf("on") >= 0) {
      controller_is_waiting = "on";
    } else if (pl.indexOf("off") >= 0) {
      controller_is_waiting = "off";
      ttk_is_waiting = "off";
      gk_is_waiting = "off";

      //=================statuses=====================
    } else if (pl.indexOf("get-status") >= 0) {
      Serial.println(pl);
      publish_statuses();

      char _payload[10];
      dtostrf(gk_current, 6, 2, _payload);
      client.publish("main/gk-current-temp", _payload);
      dtostrf(room_target, 6, 2, _payload);
      client.publish("main/room-target-temp", _payload);
      dtostrf(ttk_current, 6, 2, _payload);
      client.publish("main/ttk-current", _payload);
    }
  }
}

// ***********************************************
void initialization() {
  pinMode(PIN_GK_RELAY, OUTPUT);
  pinMode(PIN_GK_BLOCK, OUTPUT);
  pinMode(PIN_TTK_RELAY, OUTPUT);
  pinMode(PIN_BOILER_RELAY, OUTPUT);
  pinMode(LED_ON, OUTPUT);
  digitalWrite(LED_ON, HIGH);

  // реле выключено - HIGH или RELAY_OFF
  //  ГК вкл/откл
  digitalWrite(PIN_GK_RELAY, RELAY_OFF);
  // ГК блокировка комнатного датчика, если контроллер активен
  digitalWrite(PIN_GK_BLOCK, RELAY_OFF);
  // ТТК вкл/откл
  digitalWrite(PIN_TTK_RELAY, RELAY_OFF);
  // водонагреватель реле нормально замкнутые контакты
  digitalWrite(PIN_BOILER_RELAY, RELAY_OFF);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("initialization");
  delay(1000);
  attempt = max_attempts;
}
// ***********************************************
void setup() {
  initialization();
  Serial.begin(115200);
  delay(1000);

  Serial.println("------------------------------");
  Serial.println("setup WiFi");
  setup_wifi();

  tempSensors.begin();
  Serial.println(tempSensors.getDeviceCount(), DEC);
  if (!tempSensors.getAddress(TTKSensorAddress, 0))
    Serial.println("Unable to find address for Device 1");
  if (!tempSensors.getAddress(GKSensorAddress, 1))
    Serial.println("Unable to find address for Device 0");

  client.setServer("localhost", 1883);

  // client.setServer("dev.rightech.io", 1883);
  client.setCallback(handle_message);

  Serial.println("Attempting MQTT connection...");
  Serial.println(broker_url);
  reconnect();

  Serial.println();
  EEPROM.begin(8);  // для esp8266/esp32
  // считывание последнего состояния Когтроллера, если в EEPROM ахинея - записываем false
  EEPROM.get(0, controller_is_active);
  if (controller_is_active != 0 && controller_is_active != 1) {
    controller_is_active = false;
    EEPROM.put(0, 0);
  }
  if (!broker_is_online) {
    controller_is_active = false;
    Serial.println("setup: Broker is not found");
    send_email();
  }
  Serial.println(controller_is_active ? "Controller is ENABLED" : "Controller is DISABLED");
  Serial.println("------------------------------");

  // первоначальная установка / чтение заданной температуры для комнаты
  int8_t _room_target = 0;
  EEPROM.get(4, _room_target);
  if (_room_target <= 10) {
    // Serial.println("setup: EEPROM write room_target");
    EEPROM.put(4, room_target);
    EEPROM.commit();  // для esp8266/esp32
  } else {
    room_target = _room_target;
  }

  if (!controller_is_active) {
    digitalWrite(PIN_TTK_RELAY, RELAY_OFF);
    digitalWrite(PIN_GK_BLOCK, RELAY_OFF);
    digitalWrite(PIN_GK_RELAY, RELAY_OFF);
    digitalWrite(PIN_BOILER_RELAY, RELAY_OFF);
  } else {
    // ГК блокировка комнатного датчика, если контроллер активен
    handle_boiler();
    // digitalWrite(PIN_GK_BLOCK, RELAY_ON);
  }
  digitalWrite(LED_ON, !controller_is_active ? LOW : HIGH);
  if (broker_is_online) {
    publish_statuses();
  }
}

// ***********************************************
void loop() {

  bool statuses_change = false;
  // put your main code here, to run repeatedly:
  if (WiFi.status() != WL_CONNECTED) {

    Serial.print("WiFi.status() = ");
    Serial.print(WiFi.status());
    setup_wifi();
  }

  actualTimer = millis();

  if (actualTimer - prevBrokerTimer >= intBrokerTimer) {
    prevBrokerTimer = actualTimer;
    if (!client.connected()) {
      reconnect();
    }
    attempt = max_attempts;  // соединение с брокером установлено, число попыток восстанавливаем
  }

  // цикл
  if (broker_is_online) client.loop();


  if (actualTimer - prevTimer >= intTimer) {
    // Serial.println("****************Cycle *************************");
    prevTimer = actualTimer;
    // Serial.println("...loop");

    // Регулирование температуры в комнате - контроллер активен и ТТК не включен и не ожидает включения
    if (controller_is_active && !ttk_is_on && ttk_is_waiting == "") {
      // Serial.println("LOOP: check room temp if Controller ACTIVE");
      if (room_current < room_target) {
        gk_is_waiting = "on";
      } else {
        gk_is_waiting = "off";
      }
      statuses_change = true;
    }

    tempSensors.requestTemperatures();  // Просим ds18b20 собрать данные
    delay(300);

    gk_current = get_temp(GKSensorAddress, 0);
    // Serial.print("gk_current = ");
    // Serial.println(gk_current);
    // Serial.println();
    if (broker_is_online) {
      char gk_payload[10];
      dtostrf(gk_current, 6, 2, gk_payload);
      client.publish("main/gk-current-temp", gk_payload);
    }

    ttk_current = get_temp(TTKSensorAddress, 1);
    // Serial.print("ttk_current = ");
    // Serial.println(ttk_current);
    // Serial.println();

    if (ttk_is_on) {
      if (broker_is_online) {
        char ttk_payload[10];
        dtostrf(ttk_current, 6, 2, ttk_payload);
        client.publish("main/ttk-current-temp", ttk_payload);
      }
    }

    if (controller_is_waiting != "") {
      statuses_change = true;
      Serial.print(controller_is_active ? "Controller ACTIVE" : "Controller non ACTIVE");
      Serial.print("...  Is waiting to = ");
      Serial.println(controller_is_waiting);
      Serial.println();
      handle_controller();
    }

    if (gk_is_waiting != "") {
      statuses_change = true;
      Serial.print("LOOP: GK is ");
      Serial.print(gk_is_on ? "ON" : "OFF");
      Serial.print("...  Is waiting to = ");
      Serial.println(gk_is_waiting);
      Serial.println();
      handle_gk();
    }

    if (ttk_is_waiting != "") {
      statuses_change = true;
      Serial.print("LOOP: TTK is ");
      Serial.print(ttk_is_on ? "ON" : "OFF");
      Serial.print("...  Is waiting to = ");
      Serial.println(ttk_is_waiting);
      Serial.println();
      handle_ttk();
    }

    if (boiler_is_waiting != "") {
      statuses_change = true;
      Serial.print("LOOP: Boiler is ");
      Serial.print(boiler_is_on ? "ON" : "OFF");
      Serial.print("...  Is waiting to = ");
      Serial.println(boiler_is_waiting);
      Serial.println();
      handle_boiler();
    }

    if (statuses_change && broker_is_online) {
      publish_statuses();
    }

    // lcd_print(0, 0, "TTK =" + ttk_current);
    // lcd_print(0, 1, "GK =" + gk_current);

    // client.publish("main/controller-satus", gk_is_on ? "on" : "off");
    // if (controller_is_active) {
    //   client.publish("main/gk-satus", gk_is_on ? "on" : "off");
    //   client.publish("main/ttk-satus", ttk_is_on ? "on" : "off");
    // }
  }
}

// ***********************************************
float get_temp(DeviceAddress sensor_addr, int num) {
  // считывание термодатчика
  float tempC = tempSensors.getTempC(sensor_addr);
  if (tempC == DEVICE_DISCONNECTED_C) {
  }
  return tempC;
}
