/*
  Simple wemos D1 mini  MQTT example

  This sketch demonstrates the capabilities of the pubsub library in combination
  with the ESP8266 board/library.

  It connects to the provided access point using dhcp, using ssid and pswd

  It connects to an MQTT server ( using mqtt_server ) then:
  - publishes "connected"+uniqueID to the [root topic] ( using topic ) 
  - subscribes to the topic "[root topic]/composeClientID()/in"  with a callback to handle
  - If the first character of the topic "[root topic]/composeClientID()/in" is an 1, 
    switch ON the ESP Led, else switch it off

  - after a delay of "[root topic]/composeClientID()/in" minimum, it will publish 
    a composed payload to 
  It will reconnect to the server if the connection is lost using a blocking
  reconnect function. 
  
*/

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
// #include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <Bounce2.h>

// Update these with values suitable for your network.

#ifdef OFFICE
const char* ssid = "What_is_it";
const char* pswd = "Touran2017";
#else //домашняя сеть
const char* ssid = "MPON";
const char* pswd = "06019013";
#endif
const char* mqtt_server = "dev.rightech.io";
const char* topic = "boiler";    // rhis is the [root topic]


#define  LCD_ENABLE
#define DEBUG_ENABLE

// ***************** Relays ***************************/
#define PIN_GK_ON 15
#define PIN_GK_BLOCK 13
#define PIN_TTK_ON 12
#define PIN_BOILER_ON 14
// кнопка включения управления от контроллера on/off
#define PIN_BUTTON_ON 0  // при нажатии на пин подается земля

// кнопка включения насоса ТТК on/off
#define PIN_BUTTON_TTK 2  // (14) при нажатии на пин подается земля
// когда светодиод горит - управление от контроллера
#define LED_ON PIN_A1  1// (15) green
// когда светодиод горит - включен насос ТТК
#define LED_TTK 2  // (16)

Bounce2::Button but_on = Bounce2::Button();
Bounce2::Button but_ttk = Bounce2::Button();


// Управление от комнатного датчика = false. Управление через MQTT = true
bool controller_on = true;
bool ttk_on = false;
int gk_current = 0;
int ttk_current = 0;

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

WiFiClient espClient;
PubSubClient client(espClient);
long lastMsg = 0;
int value = 0;

int status = WL_IDLE_STATUS;     // the starting Wifi radio's status

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
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    digitalWrite(BUILTIN_LED, LOW);   // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is acive low on the ESP-01)
  } else {
    digitalWrite(BUILTIN_LED, HIGH);  // Turn the LED off by making the voltage HIGH
  }
}


String macToStr(const uint8_t* mac)
{
  String result;
  for (int i = 0; i < 6; ++i) {
    result += String(mac[i], 16);
    if (i < 5)
      result += ':';
  }
  return result;
}

String composeClientID() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  String clientId;
  clientId += "pc-remote-control";
  clientId += macToStr(mac);
  Serial.println(clientId);
  return clientId;
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");

    // String clientId = composeClientID() ;
    // clientId += "-";
    // clientId += String(micros() & 0xff, 16); // to randomise. sort of

    String clientId = "pc-remote-control";
    Serial.print("clientId= ");
    Serial.println(clientId);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish(topic, ("connected " + composeClientID()).c_str() , true );
      // ... and resubscribe
      // topic + clientID + in
      String subscription;
      subscription += topic;
      subscription += "/";
      subscription += composeClientID() ;
      subscription += "/in";
      client.subscribe(subscription.c_str() );
      Serial.print("subscribed to : ");
      Serial.println(subscription);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.print(" wifi=");
      Serial.print(WiFi.status());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  // confirm still connected to mqtt server
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}