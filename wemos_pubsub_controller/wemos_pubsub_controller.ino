#define LCD_ENABLE
#define DEBUG_MODE
#define VERSION "16.04.2024"


#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DallasTemperature.h>
#include <OneWire.h>
#include <EEPROM.h>

#include <LiquidCrystal_I2C.h>

#include <EMailSender.h>

#include <Bounce2.h>

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
#define PIN_BUTTON 0  // при нажатии на пин подается земля
// когда светодиод горит - управление от контроллера
#define LED_ON 15  // GREEN

const char *pswd = "06019013";
#ifdef DEBUG_MODE
const char *ssid = "TP-Link_3AAA";  // Название Вашей WiFi сети
#else
const char *ssid = "MT-PON-LITA";
#endif

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

Bounce debouncer = Bounce();

// температуры начальные
int8_t room_current = -127;
int8_t room_target = 17;
int8_t ttk_current = -127;
int8_t gk_current = -127;
int8_t ttk_cold = 35;  // Ниже этой температуры можно выключать работающий насос ТТК
int8_t ttk_max = 70;   // Выше этой температуры - ТРЕВОГА

bool gk_sensor_ready = false;
bool ttk_sensor_ready = false;
bool room_sensor_ready = false;

unsigned long prevTimer = 0;      // Variable used to keep track of the previous timer value
unsigned long actualTimer = 0;    // Variable used to keep track of the current timer value
unsigned long actualTimerGk = 0;  // Variable used to keep track of the current timer value
const long intTimer = 30000;      // Период опроса датчиков

unsigned long prevBrokerTimer = 0;   //
const long intBrokerTimer = 600000;  // Период попытки подключения  брокера  10 мин

unsigned long prevLedTimer = 0;   // Мигание светодиода
const long blinkInterval = 1000;  // Мигание светодиода

unsigned long gkPrevTimer = 0;
const long gkWaitTimer = 30000;  // время для выбега насоса ГК после его выключения

bool controller_is_active = false;
String controller_is_waiting = "";
String ttk_is_waiting = "";
String gk_is_waiting = "";

bool ttk_is_on = false;
bool gk_is_on = false;
bool boiler_is_on = false;

// int8_t attemps = 50;  //макс число попыток соединиться с брокером
int8_t attempt = 0;       //макс число попыток соединиться с брокером
int8_t max_attempts = 5;  //макс число попыток соединиться с брокером
// int8_t max_attempts = 2;  //макс число попыток соединиться с брокером
bool broker_is_online = false;

#ifdef DEBUG_MODE
const char *broker = "main-o7w3w";
#else
const char *broker = "main-controller";
#endif



// ********************************************************
void send_email() {
  EMailSender::EMailMessage message;
  message.subject = "MQQT broker";
  message.message = "controller Lost MQQT broker";

  EMailSender::Response resp = emailSend.send("litaservice@bk.ru", message);

  Serial.println("Sending status: ");

  Serial.println(resp.status);
  Serial.println(resp.code);
  Serial.println(resp.desc);
}
// ********************************************************
void reconnect() {
  // Loop until we're reconnected
  Serial.println("Lost MQTT connection");
  while (!client.connected() && attempt > 0) {
    Serial.print("Attempt remained =");
    Serial.println(attempt);
    // Attempt to connect
    if (client.connect(broker)) {
      // Serial.println("Connected MQTT 'main-controller-test'");
      Serial.print("Connected MQTT: ");
      Serial.println(broker);
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
  show_lcd_status("Controller", controller_is_waiting);

  if (controller_is_waiting == "on") {
    if (room_sensor_ready){
      controller_is_active = true;
      controller_is_waiting = "";
      Block_roomSensor();
    }
    else {
      Serial.println("room sensor not ready");
      controller_is_active = false;
      controller_is_waiting = "";
    }
    // digitalWrite(PIN_GK_BLOCK, RELAY_ON);
  } else if (controller_is_waiting == "off") {
    // Выключить контроллер можно только при холодном ТТК
    ttk_is_waiting = "off";
    // ГК выключаем сразу;
    // gk_is_on = false;
    gk_is_waiting = "off";
    // бойлер д.б. включен
    boiler_is_on = boiler_on();

    if (ttk_current < ttk_cold) {  // Котел ТТК остыл
      controller_is_active = false;
      controller_is_waiting = "";
      // digitalWrite(PIN_GK_BLOCK, RELAY_OFF);
      UnBlock_roomSensor();
      ttk_is_on = ttk_off();
      ttk_is_waiting = "";
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


  if (broker_is_online) {
    // Serial.println("8. publish");
    client.publish("main/controller-status", controller_is_active ? "on" : "off");
  }
  Serial.println(controller_is_active ? "Controller is ENABLED" : "Controller is DISABLED");
}

// ***********************************************
void handle_gk() {
  // Serial.println("DEBUG: handle_gk");
  /* при включении ГК:
    - температура ТТК менее 35, иначе продолжать ждать остывания
    - насос ТТК отключить
      */

  show_lcd_status("GK", gk_is_waiting);

  if (gk_is_waiting == "on") {

    if (!ttk_is_on) {
      Serial.println("handle_gk: ГК ожидает включения, и если ТТК выключен - включаем ГК");
      gk_is_on = gk_on();
      gk_is_waiting = "";

    } else {
      Serial.println("handle_gk: ГК ожидает включения, и если ТТК работает");
      // Работает ТТК, проверяем остывание
      if (ttk_current < ttk_cold) {
        Serial.println("handle_gk: ГК ожидает включения, и если ТТК работает и темп ТТК ниже порога");
        ttk_is_on = ttk_off();
        gk_is_on = gk_on();
        gk_is_waiting = "";
      }else {
        Serial.println("handle_gk: ГК ожидает включения, и если ТТК работает и ТТК еще горячий - ничего не делаем, ждем");

      }

    }
  } else if (gk_is_waiting == "off") {
    gk_is_on = gk_off();
    gk_is_waiting = "";
  }

  Serial.print("handle_gk: GK is ");
  Serial.println(gk_is_on ? "ON" : "OFF");
  Serial.print("handle_gk: gk_is_waiting = ");
  Serial.println(gk_is_waiting);
  if (broker_is_online) {
    client.publish("main/gk-status", gk_is_on ? "on" : "off");
    // Serial.println("1. publish");
  }
  // Serial.print("handle_gk: TEST gk_is_waiting === ");
  Serial.println(gk_is_waiting);
}

// ***********************************************
void handle_ttk() {
  // Serial.println("DEBUG: handle_ttk");
  show_lcd_status("TTK", ttk_is_waiting);

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
          ttk_is_on = ttk_on();
          ttk_is_waiting = "";
        }
      } else {
        // Serial.println("DEBUG: handle_ttk. 5");
        // Сначала отключим ГК

        gk_is_waiting = "off";
      }
      // Контроллер не активен- отказано включать ТТК
    } else {
      Serial.println("Запрет ВКЛ ТТК. Контроллер не активен");
      // ttk_is_on = ttk_off();
      ttk_is_waiting = "";
      publish_statuses("ttk-status");
    }
  } else if (ttk_is_waiting == "off") {
    Serial.println("DEBUG: handle_ttk. 7");
    if (ttk_current < ttk_cold) {
      Serial.println("DEBUG: handle_ttk. 8");
      Serial.print("handle_ttk: ttk_current < ttk_cold ");
      // если ТТК остыл - можно выключать
      ttk_is_on = ttk_off();
      ttk_is_waiting = "";
    } else {
      Serial.println("DEBUG: handle_ttk. 9");
      // Serial.print("handle_ttk: ttk HOT - waiting to OFF");
    }
  }

  Serial.print("handle_ttk: TTK is ");
  Serial.println(ttk_is_on ? "ON" : "OFF");
  if (broker_is_online) {
    // Serial.println("DEBUG: handle_ttk. 10");
    // Serial.println("2. publish");
    client.publish("main/ttk-status", ttk_is_on ? "on" : "off");
  }
}


// ***********************************************
void publish_statuses(String id) {
  if (id == "all") {
    publish_statuses("controller-status");
    publish_statuses("room-target-temp");
    publish_statuses("gk-status");
    publish_statuses("ttk-status");
    publish_statuses("boiler-status");
  }
  if (id == "controller-status") {
    client.publish("main/controller-status", controller_is_active ? "on" : "off");
  } else if (id == "room-target-temp") {
    String str_room_target;
    char ch_room_target[4];
    str_room_target = String(room_target);
    str_room_target.toCharArray(ch_room_target, 4);
    client.publish("main/room-target-temp", ch_room_target);
  } else if (id == "gk-status") {
    client.publish("main/gk-status", gk_is_on ? "on" : "off");
  } else if (id == "ttk-status") {
    // Serial.println("Публикация статуса ТТК");
    client.publish("main/ttk-status", ttk_is_on ? "on" : "off");
  } else if (id == "boiler-status") {
    client.publish("main/boiler-status", boiler_is_on ? "on" : "off");
  }
}

// ***********************************************
void handle_message(char *topic, byte *payload, unsigned int length) {

  String str_topic = String(topic);
  String pl;

  for (int i = 0; i < length; i++) {
    pl += (char)payload[i];
  }

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
    if (room_current == -127){
      room_sensor_ready = false;
    }
    else{room_sensor_ready = true;}
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
      boiler_is_on = boiler_on();
    } else if (pl.indexOf("off") >= 0) {
      boiler_is_on = boiler_off();
    }
    publish_statuses("boiler-status");
  }

  //=================Контроллер=====================
  if (String(topic) == "main/controller") {
    // Serial.println("main/controller");
    if (pl.indexOf("on") >= 0) {
      controller_is_waiting = "on";
    } else if (pl.indexOf("off") >= 0) {
      controller_is_waiting = "off";
      ttk_is_waiting = "off";
      gk_is_waiting = "off";

      //=================statuses=====================
    } else if (pl.indexOf("get-status") >= 0) {
      publish_statuses("all");
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
  gk_is_on = gk_off();
  // ГК блокировка комнатного датчика, если контроллер активен
  Block_roomSensor();
  // ТТК вкл/откл
  ttk_is_on = ttk_off();
  // водонагреватель реле нормально замкнутые контакты
  boiler_is_on = boiler_on();

  // Даем бибилотеке знать, к какому пину мы подключили кнопку
  debouncer.attach(PIN_BUTTON);
  debouncer.interval(50);  // Интервал, в течение которого мы не будем получать значения с пина

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
  delay(5000);

  Serial.print("Controller. MQTT, PubSub, WeMos D1. ver.");
  Serial.println(VERSION);
  delay(3000);
  lcd.setCursor(0, 0);
  lcd.print("Controller.MQTT, PubSub, WeMos D1. ");
  lcd.setCursor(0, 1);
  // lcd.print("ver.  04.04.2024");
  lcd.print(VERSION);
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

  gk_current = get_temp(GKSensorAddress, 0);
  if (gk_current >= 0){
    gk_sensor_ready = true;
  }
  ttk_current = get_temp(TTKSensorAddress, 1);
  if (ttk_current >= 0){
    ttk_sensor_ready = true;
  }

  Serial.println("Waiting room temp sensor");
  Serial.print("room temp=");
  Serial.println(room_current);
  lcd.setCursor(0, 0);
  lcd.print("Wait room temp");
  lcd.setCursor(0, 1);
  lcd.print(room_current);
  lcd.print(VERSION);


  // client.setServer("dev.test.io", 1883);
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
    ttk_is_on = ttk_off();
    UnBlock_roomSensor();
    gk_is_on = gk_off();
  } else {
    // ГК блокировка комнатного датчика, если контроллер активен
    // handle_boiler();
    // digitalWrite(PIN_GK_BLOCK, RELAY_ON);
    Block_roomSensor();
  }
  digitalWrite(LED_ON, !controller_is_active ? LOW : HIGH);
  if (broker_is_online) {
    publish_statuses("all");
  }
}

// ***********************************************
void loop() {

  bool statuses_change = false;

  // При потере WiFi
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("WiFi.status() = ");
    Serial.print(WiFi.status());
    setup_wifi();
  }

  //Интервал цикла
  actualTimer = millis();
  
  //Мигание светодиода
  bool current_led_status = digitalRead(LED_ON);
  if (actualTimer - prevLedTimer >= blinkInterval) {
    prevLedTimer = actualTimer;
    if (controller_is_waiting != "" || gk_is_waiting != "" || ttk_is_waiting != "" ) {
      digitalWrite(LED_ON, !current_led_status);
    }
  }

  //Попытки соединиться с брокером
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
    prevTimer = actualTimer;

    // Регулирование температуры в комнате - контроллер активен и ТТК не включен и не ожидает включения
    if (controller_is_active && !ttk_is_on && ttk_is_waiting == "") {
      // Serial.println("LOOP: check room temp if Controller ACTIVE");
      if (room_current < room_target) {
        gk_is_waiting = gk_is_on ? gk_is_waiting : "on";
      } else {
        gk_is_waiting = gk_is_on ? "off" : gk_is_waiting;
      }
      statuses_change = true;
    }

    tempSensors.requestTemperatures();  // Просим ds18b20 собрать данные
    delay(300);

    gk_current = get_temp(GKSensorAddress, 0);
    if (gk_current == -127){
      
    };
    //Если контроллер активен, то передаем температуру ГК постоянно
    char gk_payload[10];
    dtostrf(gk_current, 6, 2, gk_payload);
    if (broker_is_online) {
      if (controller_is_active) {
        // Serial.println("6. publish");
        client.publish("main/gk-current-temp", gk_payload);
        //Если контроллер НЕ активен, то передаем температуру ГК если она выше 25 гр
      } else if (gk_current > 25) {
        // Serial.println("7. publish");
        client.publish("main/gk-current-temp", gk_payload);
      }
    }

    ttk_current = get_temp(TTKSensorAddress, 1);
    if (ttk_is_on) {
      if (broker_is_online) {
        char ttk_payload[10];
        dtostrf(ttk_current, 6, 2, ttk_payload);
        // Serial.println("8. publish");
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
      // publish_statuses("controller-status"); - это вызывается в handle_controller()
    }

    if (gk_is_waiting != "") {
      statuses_change = true;
      Serial.print("LOOP: GK is ");
      Serial.print(gk_is_on ? "ON" : "OFF");
      Serial.print("...  Is waiting to = ");
      Serial.println(gk_is_waiting);
      Serial.println();
      handle_gk();
      // publish_statuses("gk-status");
    }

    if (ttk_is_waiting != "") {
      statuses_change = true;
      Serial.print("LOOP: TTK is ");
      Serial.print(ttk_is_on ? "ON" : "OFF");
      Serial.print("...  Is waiting to = ");
      Serial.println(ttk_is_waiting);
      Serial.println();
      handle_ttk();
      // publish_statuses("ttk-status");
    }

    digitalWrite(LED_ON, !controller_is_active ? LOW : HIGH);
  }
    // Даем объекту бибилотеки знать, что надо обновить состояние - мы вошли в новый цкил loop
  debouncer.update();

  // Получаем значение кнопки
  int value = debouncer.read();
  if (value == LOW) {
    if (controller_is_waiting == "") {
      controller_is_waiting = controller_is_active ? "off" : "on";
      Serial.println("Button pressed");
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

// ***********************************************
void show_lcd_status(String unit, String status) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(unit);
  lcd.setCursor(0, 1);
  lcd.print("is going to: ");
  lcd.setCursor(13, 1);
  lcd.print(status);
}

// ***********************************************
bool ttk_on() {
  digitalWrite(PIN_TTK_RELAY, RELAY_ON);
  delay(500);
  return !digitalRead(PIN_TTK_RELAY);
}

// ***********************************************
bool ttk_off() {
  digitalWrite(PIN_TTK_RELAY, RELAY_OFF);
  delay(500);
  return !digitalRead(PIN_TTK_RELAY);
}

// ***********************************************
bool gk_on() {
  digitalWrite(PIN_GK_RELAY, RELAY_ON);
  delay(500);
  return !digitalRead(PIN_GK_RELAY);
}

// ***********************************************
bool gk_off() {
  digitalWrite(PIN_GK_RELAY, RELAY_OFF);
  delay(500);
  return !digitalRead(PIN_GK_RELAY);
}

// ***********************************************
bool boiler_on() {
  digitalWrite(PIN_BOILER_RELAY, RELAY_OFF);
  delay(500);
  return digitalRead(PIN_BOILER_RELAY);
}

// ***********************************************
bool boiler_off() {
  digitalWrite(PIN_BOILER_RELAY, RELAY_ON);
  delay(500);
  return digitalRead(PIN_BOILER_RELAY);
}

// ***********************************************
void Block_roomSensor() {
  digitalWrite(PIN_GK_BLOCK, RELAY_ON);
}

// ***********************************************
void UnBlock_roomSensor() {
  digitalWrite(PIN_GK_BLOCK, RELAY_OFF);
}