#define LCD_ENABLE

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <EEPROM.h>

#ifdef LCD_ENABLE
#include <LiquidCrystal_I2C.h>
#endif


#define ON true
#define OFF false
#define RELAY_ON LOW
#define RELAY_OFF HIGH

// ***************** Relays ***************************/
// #define PIN_GK_RELAY 5
#define PIN_GK_RELAY 14

//реле блокировки комнатного датчика - нормально замкнуто
// #define PIN_GK_BLOCK 4
#define PIN_GK_BLOCK 16

#define PIN_TTK_RELAY 13

//реле бойлера - нормально замкнуто
#define PIN_BOILER_RELAY 12

// кнопка включения насоса ТТК on/off
#define PIN_BUTTON_TTK 0  // при нажатии на пин подается земля
// когда светодиод горит - управление от контроллера
#define LED_ON 15  // GREEN


const char* ssid = "TP-Link_3AAA";  // Название Вашей WiFi сети
// const char* ssid = "MT-PON-LITA";
const char* pswd = "06019013";

// Use WiFiClient class to create TCP connections
WiFiClient wi_fi_client;
PubSubClient client(wi_fi_client);


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

//температуры начальные
int8_t room_current = 0;
int8_t room_target = 17;
int8_t ttk_current = 0;
int8_t gk_current = 0;
int8_t ttk_cold = 35;  // Ниже этой температуры можно выключать работающий насос ТТК
int8_t ttk_max = 70;   // Выше этой температуры - ТРЕВОГА

unsigned long prevTimer = 0;      // Variable used to keep track of the previous timer value
unsigned long actualTimer = 0;    // Variable used to keep track of the current timer value
unsigned long actualTimerGk = 0;  // Variable used to keep track of the current timer value
const long intTimer = 15000;

unsigned long gkPrevTimer = 0;
const long gkWaitTimer = 120000;  // время для выбега насоса ГК после его выключения

// const long ttkWaitTimer = 120000;
bool controller_is_active = false;
String controller_is_waiting = "";
String ttk_is_waiting = "";
String gk_is_waiting = "";
String boiler_is_waiting = "";
bool ttk_is_on = false;
bool gk_is_on = false;
bool boiler_is_on = false;


// ********************************************************
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    // Attempt to connect
    // if (client.connect("main-controller", "alita60", "11071960")) {
    if (client.connect("main-controller")) {
      Serial.println("Connected MQTT 'main-controller'");
      if (!client.subscribe("main/#")) {
        Serial.println("subscribe 'main' - false");
      }
      if (!client.subscribe("room-sensor/#")) {
        Serial.println("subscribe room-sensor - false");
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

#ifdef LCD_ENABLE
  lcd.clear();
  lcd.setCursor(1, 0);
  lcd.print("WiFi connected");
  lcd.setCursor(3, 1);
  lcd.print(WiFi.localIP());
  // delay(1000);
#endif

  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  delay(3000);
}

// ***********************************************
void handle_controller() {

  if (controller_is_waiting == "on") {
    controller_is_active = true;
    // Включить LED
    controller_is_waiting = "";
  } else if (controller_is_waiting == "off") {
    //Выключить контроллер можно только при холодном ТТК
    ttk_is_waiting = "off";
    gk_is_waiting = "off";
    digitalWrite(PIN_BOILER_RELAY, RELAY_OFF);  // Бойлер работает автономно, реле бойлера нормально замкнуто
    if (ttk_current < ttk_cold) {
      controller_is_active = false;
      controller_is_waiting = "";
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
  digitalWrite(LED_ON, controller_is_active ? LOW : HIGH);
  digitalWrite(PIN_GK_BLOCK, controller_is_active ? RELAY_ON : RELAY_OFF);
  client.publish("main/controller-status", controller_is_active ? "on" : "off");
  Serial.println(controller_is_active ? "Controller is ENABLED" : "Controller is DISABLED");

#ifdef LCD_ENABLE
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Controller is");
  lcd.setCursor(0, 1);
  lcd.print(controller_is_active ? "ENABLED" : "DISABLED");
  // delay(1000);
#endif
}

// ***********************************************
void handle_gk() {
  /*
  при включении ГК:
    - температура ТТК менее 35, иначе продолжать ждать остывания
    - насос ТТК отключить
  */
  if (gk_is_waiting == "on") {
    if (ttk_current < ttk_cold) {
      // gk_relay = RELAY_ON;
      ttk_is_on = false;
      gk_is_on = true;
      digitalWrite(PIN_TTK_RELAY, RELAY_OFF);
      digitalWrite(PIN_GK_RELAY, RELAY_ON);
      // Serial.println("gk_relay = RELAY_ON");
      gk_is_waiting = "";
    }
  } else {
    // gk_relay = RELAY_OFF;
    gk_is_on = false;
    digitalWrite(PIN_GK_RELAY, RELAY_OFF);
    gk_is_waiting = "";
    // Serial.println("gk_relay = RELAY_OFF");
  }
  client.publish("main/gk-status", gk_is_on ? "on" : "off");

#ifdef LCD_ENABLE
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(gk_is_on ? "GK is ON" : "GK is OFF");
  // delay(1000);
#endif
}

// ***********************************************
void handle_ttk() {
  if (ttk_is_waiting == "on") {
    /*
  выключить  ГК, выждать 2 мин
  */
    if (!gk_is_on) {
      if (actualTimer - gkPrevTimer >= gkWaitTimer) {
        gkPrevTimer = actualTimer;
        //можно включать насос ТТК
        ttk_is_on = true;
        digitalWrite(PIN_TTK_RELAY, RELAY_ON);
        ttk_is_waiting = "";
      }
    }
  } else if (ttk_is_waiting == "off") {
    if (ttk_current < ttk_cold) {
      //если ТТК остыл - можно выключать
      ttk_is_on = false;
      digitalWrite(PIN_TTK_RELAY, RELAY_OFF);
      ttk_is_waiting = "";
    }
  }
  client.publish("main/ttk-status", ttk_is_on ? "on" : "off");

#ifdef LCD_ENABLE
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(ttk_is_on ? "TTK is ON" : "TTK is OFF");
  // delay(1000);
#endif
}

// ***********************************************
void handle_boiler() {
  if (boiler_is_waiting == "on") {
    digitalWrite(PIN_BOILER_RELAY, RELAY_OFF);
    boiler_is_waiting = "";
    boiler_is_on = true;
  } else if (boiler_is_waiting == "off") {
    digitalWrite(PIN_BOILER_RELAY, RELAY_ON);
    boiler_is_waiting = "";
    boiler_is_on = false;
  }
  client.publish("main/boiler-status", boiler_is_on ? "on" : "off");

#ifdef LCD_ENABLE
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(boiler_is_on ? "Boiler is ON" : "Boiler is OFF");
  // delay(1000);
#endif
}

// ***********************************************
void handle_message(char* topic, byte* payload, unsigned int length) {

  String str_topic = String(topic);
  String pl;

  for (int i = 0; i < length; i++) {
    pl += (char)payload[i];
  }

  //================= Room =====================
  if (String(topic) == "room-sensor/current-temp") {


#ifdef LCD_ENABLE
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("ROOM   GK   TTK");
    lcd.setCursor(1, 1);
    lcd.print(room_current);
    lcd.setCursor(6, 1);
    lcd.print(gk_current);
    lcd.setCursor(12, 1);
    lcd.print(ttk_current);
    // delay(1000);
#endif

    room_current = pl.toInt();
    if (controller_is_active) {
      if (room_current < room_target) {
        if (!gk_is_on) {
          gk_is_waiting = "on";
        }
      } else {
        if (gk_is_on) {
          gk_is_waiting = "off";
        }
      }
    }
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
      gk_is_waiting = "off";
      // сразу выключить ГК
      digitalWrite(PIN_GK_RELAY, RELAY_OFF);
      // выжидать 2 мин до включения ТТК

    } else if (pl.indexOf("off") >= 0) {
      ttk_is_waiting = "off";
    }
  }

  //=================ГК=====================
  if (String(topic) == "main/gk") {
    // gk_is_waiting = true;
    if (pl.indexOf("on") >= 0) {
      ttk_is_waiting = "off";
      gk_is_waiting = "on";
    } else if (pl.indexOf("off") >= 0) {
      gk_is_waiting = "off";
    }
  }
  //=================Бойлер=====================
  if (String(topic) == "main/boiler") {
    if (pl.indexOf("on") >= 0) {
      //выключено реле - бойлер работает
      boiler_is_on = true;
      digitalWrite(PIN_BOILER_RELAY, RELAY_OFF);
    } else if (pl.indexOf("off") >= 0) {
      //ВКЛЮЧЕНО реле - бойлер отключен
      boiler_is_on = false;
      digitalWrite(PIN_BOILER_RELAY, RELAY_ON);
    }
    client.publish("main/boiler-status", boiler_is_on ? "on" : "off");
  }

  //=================Контроллер=====================
  if (String(topic) == "main/controller") {
    if (pl.indexOf("on") >= 0) {
      controller_is_waiting = "on";
      //LED controller active = ON
    } else if (pl.indexOf("off") >= 0) {
      controller_is_waiting = "off";
      if (ttk_is_on) {
        ttk_is_waiting = "off";
      }
      if (gk_is_on) {
        gk_is_waiting = "off";
      }
    }
  }
  //=================statuses=====================
  if (String(topic) == "main/controller") {
    if (pl.indexOf("get-status") >= 0) {
      client.publish("main/controller-status", controller_is_active ? "on" : "off");
      client.publish("main/gk-status", gk_is_on ? "on" : "off");
      client.publish("main/ttk-status", ttk_is_on ? "on" : "off");
      client.publish("main/boiler-status", boiler_is_on ? "on" : "off");
      
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

  //реле выключено - HIGH или RELAY_OFF
  // ГК вкл/откл
  digitalWrite(PIN_GK_RELAY, RELAY_OFF);
  // ГК блокировка комнатного датчика, если контроллер активен
  digitalWrite(PIN_GK_BLOCK, RELAY_OFF);
  // ТТК вкл/откл
  digitalWrite(PIN_TTK_RELAY, RELAY_OFF);
  // водонагреватель реле нормально замкнутые контакты
  digitalWrite(PIN_BOILER_RELAY, RELAY_OFF);

#ifdef LCD_ENABLE
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("initialization");
  delay(1000);
#endif
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
  if (!tempSensors.getAddress(TTKSensorAddress, 0)) Serial.println("Unable to find address for Device 1");
  if (!tempSensors.getAddress(GKSensorAddress, 1)) Serial.println("Unable to find address for Device 0");

  client.setServer("dev.rightech.io", 1883);
  client.setCallback(handle_message);

  Serial.println("Attempting MQTT connection...");
  reconnect();

  Serial.println();
  EEPROM.begin(8);  // для esp8266/esp32
  // считывание последнего состояния Когтроллера, если в EEPROM ахинея - записываем false
  EEPROM.get(0, controller_is_active);
  if (controller_is_active != 0 && controller_is_active != 1) {
    controller_is_active = false;
    EEPROM.put(0, 0);
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

  String str_room_target;
  char ch_room_target[4];
  str_room_target = String(room_target);
  str_room_target.toCharArray(ch_room_target, 4);

  client.publish("main/room-target-temp", ch_room_target);

  client.publish("main/controller-status", controller_is_active ? "on" : "off");
  client.publish("main/gk-status", gk_is_on ? "on" : "off");
  client.publish("main/ttk-status", ttk_is_on ? "on" : "off");
  client.publish("main/boiler-status", boiler_is_on ? "on" : "off");

  // ГК блокировка комнатного датчика, если контроллер активен
  digitalWrite(PIN_GK_BLOCK, controller_is_active ? RELAY_ON : RELAY_OFF);
  digitalWrite(LED_ON, controller_is_active ? LOW : HIGH);
}

// ***********************************************
void loop() {
  // put your main code here, to run repeatedly:
  if (WiFi.status() != WL_CONNECTED) {

    Serial.print("WiFi.status() = ");
    Serial.print(WiFi.status());
  }

  if (!client.connected()) {
    Serial.println("Lost MQTT connection");
    reconnect();
  }
  // цикл
  client.loop();

  actualTimer = millis();

  if (actualTimer - prevTimer >= intTimer) {
    prevTimer = actualTimer;
    // Serial.println("...loop");


    if (controller_is_waiting != "") {

      Serial.print("contoller is waiting to = ");
      Serial.println(controller_is_waiting);
      Serial.println();
      handle_controller();
    }

    if (controller_is_active) {
      if (!ttk_is_on) {
        if (room_current < room_target) {
          gk_is_waiting = "on";
        } else {
          gk_is_waiting = "off";
        }
      }
    }

    if (gk_is_waiting != "") {
      Serial.println(gk_is_on ? "GK is ON" : "GK is OFF");
      Serial.print("gk is waiting to = ");
      Serial.println(gk_is_waiting);
      Serial.println();
      handle_gk();
    }

    if (ttk_is_waiting != "") {
      Serial.println(ttk_is_on ? "TTK is ON" : "TTK is OFF");
      Serial.print("TTK is waiting to = ");
      Serial.println(ttk_is_waiting);
      Serial.println();
      handle_ttk();
    }
    tempSensors.requestTemperatures();  // Просим ds18b20 собрать данные
    delay(300);

    gk_current = get_temp(GKSensorAddress, 0);
    char gk_payload[10];
    dtostrf(gk_current, 6, 2, gk_payload);

    ttk_current = get_temp(TTKSensorAddress, 1);
    char ttk_payload[10];
    dtostrf(ttk_current, 6, 2, ttk_payload);

    client.publish("main/ttk-current-temp", ttk_payload);

    // lcd_print(0, 0, "TTK =" + ttk_current);

    client.publish("main/gk-current-temp", gk_payload);

    // lcd_print(0, 1, "GK =" + gk_current);


    client.publish("main/controller-satus", gk_is_on ? "on" : "off");
    if (controller_is_active) {
      client.publish("main/gk-satus", gk_is_on ? "on" : "off");
      client.publish("main/ttk-satus", ttk_is_on ? "on" : "off");
    }
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
