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

const char* ssid = "JTI-POLINEMA";
const char* password = "jtifast!";
const char* mqtt_server = "test.mosquitto.org";
LiquidCrystal_I2C lcd(0x27, 16, 2);
#define ONE_WIRE_BUS 5  // Digitalpin where Temp sensor is connected
#define TdsSensorPin 35 // Where Analog pin of TDS sensor is connected to arduino
#define SOUND_SPEED 0.034

OneWire oneWire(ONE_WIRE_BUS);
GravityTDS gravityTds;
DallasTemperature sensors(&oneWire);

const int trigPin = 4;
const int echoPin = 18;
long duration;
float distanceCm = 0;
float tdsValue = 0;
float suhu = 0;
float calibration_value = 21.34 - 0.7;
int phval = 0;
unsigned long int avgval;
int buffer_arr[10], temp;
float ph_act;

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE	(50)
char msg[MSG_BUFFER_SIZE];
int value = 0;

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
    // it is active low on the ESP-01)
  } 
  if ((char)payload[0] == '0') {
    digitalWrite(BUILTIN_LED, HIGH);  // Turn the LED off by making the voltage HIGH
  }

}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "HidroponikClient-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("hidroponik/esp32/suhu", String(suhu).c_str());
      // ... and resubscribe
      client.subscribe("hidroponik/esp32/suhu");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}


void setup()
{
  // put your setup code here, to run once:
  Serial.begin(115200);
  pinMode(trigPin, OUTPUT); // Sets the trigPin as an Output
  pinMode(echoPin, INPUT);  // Sets the echoPin as an Input
  sensors.begin();
  gravityTds.setPin(TdsSensorPin);
  gravityTds.setAref(3.3);      // reference voltage on ADC, default 5.0V on Arduino UNO
  gravityTds.setAdcRange(4096); // 1024 for 10bit ADC;4096 for 12bit ADC
  gravityTds.begin();           // initialization
  Wire.begin();
  // wifi
  WiFi.begin(ssid, password);
  Serial.print("Connecting...");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.print("Connected: ");
  Serial.println(WiFi.localIP());

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  lcd.init();
  lcd.backlight();
}

void loop()
{
   if (!client.connected()) {
    reconnect();
  }
  client.loop();
  // put your main code here, to run repeatedly:
  // start nutrisi
  sensors.requestTemperatures();
  gravityTds.setTemperature(sensors.getTempCByIndex(0)); // grab the temperature from sensor and execute temperature compensation
  gravityTds.update();                                   // calculation done here from gravity library
  tdsValue = gravityTds.getTdsValue();                   // then get the TDS value

  lcd.setCursor(0, 0);
  lcd.print("Nutrisi: ");
  lcd.print(tdsValue, 0);
  lcd.print(" ppm");

  delay(2500);
  lcd.clear();
  // end nutrisi

  // start suhu
  sensors.requestTemperatures();
  suhu = sensors.getTempCByIndex(0);
  Serial.printf("Suhu: %f\n", suhu);

  lcd.setCursor(0, 1);
  lcd.printf("Suhu: %f C", suhu);

  delay(2500);
  lcd.clear();
  // end suhu

  // start pH
  for (int i = 0; i < 10; i++)
  {
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
    avgval += buffer_arr[i];
  float volt = (float)avgval * 3.3 / 4095 / 6;
  ph_act = -5.70 * volt + calibration_value;
  Serial.printf("pH: %f\n", ph_act);
  lcd.setCursor(3, 0);
  lcd.printf("pH: %f ", ph_act);

  delay(2500);
  lcd.clear();
  // end pH

  // start distance
  // Clears the trigPin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  // Sets the trigPin on HIGH state for 10 micro seconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // Reads the echoPin, returns the sound wave travel time in microseconds
  duration = pulseIn(echoPin, HIGH);

  // Calculate the distance
  distanceCm = duration * SOUND_SPEED / 2;

  // Prints the distance in the Serial Monitor
  Serial.printf("Distance (cm): %f\n", distanceCm);

  delay(2500);
  lcd.clear();
  // end distance

  // start send data
   String url;
  url ="http://192.168.72.107/hidroponik/webapi/api/create.php?nutrisi=";
  url += String(tdsValue);
  url += "&suhu=";
  url += String(suhu);
  url += "&pH=";
  url += String(ph_act);
  url += "&air=";
  url += String(distanceCm);
  
  HTTPClient http;  
  http.begin(url);  //Specify request destination
  int httpCode = http.GET();//Send the request
  String payload;  
  if (httpCode > 0) { //Check the returning code    
      payload = http.getString();   //Get the request response payload
      payload.trim();
      if( payload.length() > 0 ){
         Serial.println(payload + "\n");
      }
  }
  
  http.end();   //Close connection
  delay(60000); //interval 60s
}