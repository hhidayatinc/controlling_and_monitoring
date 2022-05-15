#include <Arduino.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <EEPROM.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <GravityTDS.h>
#include <Wire.h>
#include <Adafruit_I2CDevice.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

const char *ssid = ".";
const char *password = "12345678";
const char *mqtt_server = "test.mosquitto.org";
LiquidCrystal_I2C lcd(0x27, 16, 2); // lcd sda 21, scl 22
#define ONE_WIRE_BUS 5              // Digitalpin where Temp sensor is connected
#define TdsSensorPin 35             // Where Analog pin of TDS sensor is connected to arduino
#define SOUND_SPEED 0.034

OneWire oneWire(ONE_WIRE_BUS);
GravityTDS gravityTds;
DallasTemperature sensors(&oneWire);

const int trigPin = 4;
const int echoPin = 18;
long duration;
float calibration_value = 21.34 - 0.7;
int phval = 0;
unsigned long int avgval;
int buffer_arr[10], temp;
float ph_act;
int water = 0;
int tdsValue = 0;
int suhu = 0;
uint8_t relay1 = 27; // nutrisiA
uint8_t relay2 = 26; // nutrisiB
uint8_t relay3 = 14; // air
bool pump_water = false;
bool pump_nutrisi = false;

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (50)
char msg[MSG_BUFFER_SIZE];
int value = 0;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  
  pinMode(relay1, OUTPUT);
  pinMode(relay2, OUTPUT);
  pinMode(relay3, OUTPUT);
  digitalWrite(relay1, HIGH);
  digitalWrite(relay2, HIGH);
  digitalWrite(relay3, HIGH);

  sensors.begin();
  gravityTds.setPin(TdsSensorPin);
  gravityTds.setAref(3.3);
  gravityTds.setAdcRange(4096);
  gravityTds.begin();
  Wire.begin();

  lcd.init();
  lcd.backlight();
}

void save_data(){
  EEPROM.write(0, phval);
  EEPROM.write(1, tdsValue);
  EEPROM.write(2, suhu);
  EEPROM.write(3, water);
  EEPROM.write(4, pump_water);
  EEPROM.write(5, pump_nutrisi);
  EEPROM.commit();

  Serial.println("Data saved");
  //delay(1000);
}

void read_sensor(){
  //suhu
  sensors.requestTemperatures();
  suhu = sensors.getTempCByIndex(0);
  Serial.printf("suhu: %d\n", suhu);
  lcd.setCursor(0, 0);
  lcd.printf("Suhu: %d", suhu);

  //ph
  for (int i = 0; i < 9; i++){
    buffer_arr[i] = analogRead(32);
    delay(30);
  }
  for (int i = 0; i < 9; i++)
  {
    for (int j = i + 1; j < 10; j++)
    {
      if (buffer_arr[i] > buffer_arr[j])
      {
        temp = buffer_arr[i];
        buffer_arr[i] = buffer_arr[j];
        buffer_arr[j] = temp;
      }
    }
  }
  avgval = 0;
  for (int i = 2; i < 8; i++)
  {
    avgval += buffer_arr[i];
    float volt = (float)avgval * 3.3 / 4096 / 6;
    ph_act = -5.70 * volt + calibration_value;
  }
  phval = (int)ph_act;
  Serial.printf("ph: %d\n", phval);
  lcd.setCursor(0, 1);
  lcd.printf("Ph: %d", phval);

  //tds
  sensors.requestTemperatures();
  gravityTds.setTemperature(sensors.getTempCByIndex(0));
  gravityTds.update();
  tdsValue = gravityTds.getTdsValue();
  Serial.printf("tds: %d\n", tdsValue);
  lcd.setCursor(2, 0);
  lcd.printf("TDS: %d", tdsValue);

  //water
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  duration = pulseIn(echoPin, HIGH);
  water = duration * SOUND_SPEED / 2;
  Serial.printf("water: %d\n", water);
  lcd.setCursor(2, 1);
  lcd.printf("Water: %d", water);

  delay(2500);
}

void setup_wifi(){
  WiFi.begin(ssid, password);
  Serial.print("Connecting...");
  while (WiFi.status() != WL_CONNECTED){
    //Serial.print(".");
    Serial.println("Failed connect internet!");
    Serial.println("Start to read sensor ....");
    read_sensor();
    Serial.println("Checking water & nutrient ....");
    water_condition();
    Serial.println("Saving all data ....");
    save_data();
    //read_sensor();
    
    delay(1000);
  }
  Serial.println();
  Serial.print("Connected: ");
  Serial.println(WiFi.localIP());
}


void water_condition(){
  if (water >= 60){
    digitalWrite(relay3, LOW);
    pump_water = true;
    Serial.printf("Pump water: %d\n", pump_water);
    lcd.setCursor(3, 0);
    lcd.print("Air: ON");
  }
  else{
    digitalWrite(relay3, HIGH);
    pump_water = false;
    Serial.printf("Pump water: %d\n", pump_water);
    lcd.setCursor(3, 1);
    lcd.print("Air: OFF");
    nutrient_condition();
  }
}

void nutrient_condition(){
  if (tdsValue <= 140){
    digitalWrite(relay1, LOW);
    digitalWrite(relay2, LOW);
    digitalWrite(relay3, HIGH);
    pump_nutrisi = true;
    Serial.printf("Pump nutrisi: %d\n", pump_nutrisi);
    lcd.setCursor(4, 0);
    lcd.print("Nutrient: ON");
  }
  else{
    digitalWrite(relay1, HIGH);
    digitalWrite(relay2, HIGH);
    digitalWrite(relay3, HIGH);
    pump_nutrisi = false;
    Serial.printf("Pump nutrisi: %d\n", pump_nutrisi);
    lcd.setCursor(4, 1);
    lcd.print("Nutrient: OFF");
  }
}

void publish_sensor(){
  client.publish("/sensor/ph", String(phval).c_str());
  client.publish("/sensor/tds", String(tdsValue).c_str());
  client.publish("/sensor/suhu", String(suhu).c_str());
  client.publish("/sensor/water", String(water).c_str());
}

void subscribe_control(){
  client.subscribe("/control/water");
  client.subscribe("/control/tds");
} 

void publish_control(){
  client.publish("/control/water", String(pump_water).c_str());
  client.publish("/control/tds", String(pump_nutrisi).c_str());
}

void reconnect() {
  // Loop until we're reconnected
  //setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("HidroponikClient-")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
      Serial.println("Start to read sensor ....");
      read_sensor();
      Serial.println("Checking water & nutrient ....");
      water_condition();
      Serial.println("Saving all data ....");
      save_data();
      //Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

//void save_data(){
//  File dataFile = SPIFFS.open("/data.txt", "w");
//  if (!dataFile) {
//    Serial.println("Failed to open data file for writing");
//    return;
//  }
//  dataFile.print(phval);
//  dataFile.print("\n");
//  dataFile.print(tdsValue);
//  dataFile.print("\n");
//  dataFile.print(suhu);
//  dataFile.print("\n");
//  dataFile.print(water);
//  dataFile.print("\n");
//  dataFile.print(pump_water);
//  dataFile.print("\n");
//  dataFile.print(pump_nutrisi);
//  dataFile.print("\n");
//  dataFile.close();
//
//  Serial.println("Data saved");
//  delay(1000);
//}

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  String messageTemp;

  for (int i = 0; i < length; i++)
  {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

  if (topic == "/control/water"){
    if (messageTemp == "true"){
      pump_water = true;
      Serial.printf("Pump water: %d\n", pump_water);
      lcd.setCursor(3, 0);
      lcd.print("Air: ON");
    }
    else if (messageTemp == "false"){
      pump_water = false;
      Serial.printf("Pump water: %d\n", pump_water);
      lcd.setCursor(3, 1);
      lcd.print("Air: OFF");
    }
  }
  else if (topic == "/control/tds"){
    if (messageTemp == "true"){
      pump_nutrisi = true;
      Serial.printf("Pump nutrisi: %d\n", pump_nutrisi);
      lcd.setCursor(4, 0);
      lcd.print("Nutrient: ON");
    }
    else if (messageTemp == "false"){
      pump_nutrisi = false;
      Serial.printf("Pump nutrisi: %d\n", pump_nutrisi);
      lcd.setCursor(4, 1);
      lcd.print("Nutrient: OFF");
    }
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  read_sensor();
  water_condition();
  //nutrient_condition();
  //save_data();
  setup_wifi();
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  long now = millis();
  if (now - lastMsg > 5000) {
    lastMsg = now;

    publish_sensor();
    subscribe_control();
    publish_control(); 
  }

  delay(2500);
}