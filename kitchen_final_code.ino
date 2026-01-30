#include <WiFi.h>
#include <PubSubClient.h>
#include <LiquidCrystal_I2C.h>
#include <ESP32Servo.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Pins
const int firePin = 5, mq2Pin = 35, gasPin = 34, tempPin = 23;
const int windowServoPin = 12, gasCutoffPin = 14;
const int fanPin = 19, pumpPin = 18, ledRed = 33, buzzer = 4;

// Config
const char* ssid = "iot";
const char* password = "xxxxxxx";
const char* mqtt_server = "broker.emqx.io";
const char* phone1 = "Your_Number";
const char* phone2 = "Your_Number";

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo windowServo, gasServo;
OneWire oneWire(tempPin);
DallasTemperature sensors(&oneWire);
WiFiClient espClient;
PubSubClient client(espClient);
HardwareSerial sim800(2);

unsigned long lastMqtt = 0;
String currentStatus = "Safe";  // Variable to track system status

// 1. Read All Sensors
int fire;
int mq2;
int mq6;
int temp;

// --- Function to send alert data to mobile app via MQTT ---
void sendToMobileApp(String statusMsg, int m2, int m6, float t, bool f) {
  // We publish immediately when an alert happens
  String payload = String(m2) + "," + String(m6) + "," + String(f) + "," + String(t) + "," + statusMsg;
  client.publish("safety1172026", payload.c_str());
  Serial.println("Mobile App Alert Sent: " + payload);
}

void setup() {
  Serial.begin(115200);
  sim800.begin(9600, SERIAL_8N1, 16, 17);

  pinMode(firePin, INPUT);
  pinMode(mq2Pin, INPUT);
  pinMode(gasPin, INPUT);
  pinMode(fanPin, OUTPUT);
  pinMode(pumpPin, OUTPUT);
  pinMode(ledRed, OUTPUT);
  pinMode(buzzer, OUTPUT);

  digitalWrite(fanPin, HIGH);   // OFF
  digitalWrite(pumpPin, HIGH);  // OFF
  digitalWrite(ledRed, LOW);

  windowServo.attach(windowServoPin);
  gasServo.attach(gasCutoffPin);
  windowServo.write(0);
  gasServo.write(0);

  lcd.begin();
  lcd.backlight();
  sensors.begin();

  // WiFi Connection with LCD Status
  lcd.print("Connecting WiFi");
  WiFi.begin(ssid, password);
  unsigned long startWiFi = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startWiFi < 10000) {
    delay(500);
    Serial.print(".");
  }
  lcd.clear();
  if (WiFi.status() == WL_CONNECTED) {
    lcd.print("WiFi: Connected");
    Serial.println("\nConnected!");
  } else {
    lcd.print("WiFi: Failed");
    Serial.println("\nFailed!");
  }
  delay(2000);
  client.setServer(mqtt_server, 1883);
  delay(5000);
}

void loop() {
  // MQTT Reconnect
  if (WiFi.status() == WL_CONNECTED && !client.connected()) {
    if (client.connect("ESP32Kitchen")) {
      client.subscribe("safety1172026");
    }
  }
  client.loop();

  // 1. Read All Sensors
  fire = (digitalRead(firePin) == LOW);
  mq2 = analogRead(mq2Pin);
  mq6 = analogRead(gasPin);
  sensors.requestTemperatures();
  int t = sensors.getTempCByIndex(0);

  if (t != 85.0 && t != -127.0) {  // DS18B20 error values
    temp = t;
  }

  String alert = "";

  // 2. Check Every Sensor (Separate If Blocks)
  if (fire) {
    alert = "FIRE!";
    currentStatus = "Fire Detected";
    digitalWrite(pumpPin, LOW);  // Relay ON
    digitalWrite(ledRed, HIGH);
    Serial.println("SMS Sent: Fire!");
  }

  if (mq6 > 1800) {
    currentStatus = "Gas Leakage";
    alert = "GAS!";
    digitalWrite(fanPin, LOW);  // Relay ON
    digitalWrite(ledRed, HIGH);
    Serial.println("SMS Sent: Gas Leak!");
  }

  if (mq2 > 100) {
    currentStatus = "Smoke Detected";
    alert = "SMOKE!"; 
    digitalWrite(fanPin, LOW);  // Relay ON
    digitalWrite(ledRed, HIGH);
    Serial.println("SMS Sent: Smoke!");
  }

  if (temp > 50.0) {
    currentStatus = "High Temp";
    alert = "TEMP!";
    digitalWrite(ledRed, HIGH);
    Serial.println("Temp High!");
  }

  // 3. Action if Alert Detected
  if (alert != "") {
    lcd.clear();
    lcd.print("ALERT: ");
    lcd.print(alert);

    // Call function to send alert to mobile app
    sendToMobileApp(currentStatus, mq2, mq6, temp, fire);

    digitalWrite(buzzer, HIGH);

    // Slow Servo Movement (Opening/Closing)
    for (int i = 0; i <= 90; i++) {
      gasServo.write(i);
      windowServo.write(i);  // 0 to 180
      delay(20);
    }

    // SIM800 SMS
    sim800.println("AT+CMGF=1");
    delay(200);
    sim800.println("AT+CMGS=\"" + String(phone1) + "\"");
    delay(200);
    sim800.print("Emergency: " + alert);
    sim800.write(26);
    delay(5000);

        // SIM800 SMS
    sim800.println("AT+CMGF=1");
    delay(200);
    sim800.println("AT+CMGS=\"" + String(phone2) + "\"");
    delay(200);
    sim800.print("Emergency: " + alert);
    sim800.write(26);
    delay(2000);

    // Call function to send alert to mobile app
    sendToMobileApp(currentStatus, mq2, mq6, temp, fire);

    digitalWrite(buzzer, LOW);

    // 10 Second Delay
    Serial.println("System Busy - 10s delay...");
    delay(5000);

    // Reset Slow
    Serial.println("Resetting System...");
    digitalWrite(pumpPin, HIGH);  // OFF
    digitalWrite(fanPin, HIGH);   // OFF
    digitalWrite(ledRed, LOW);
    // Call function to send alert to mobile app
    sendToMobileApp(currentStatus, mq2, mq6, temp, fire);
    for (int i = 90; i >= 0; i--) {
      gasServo.write(i);
      windowServo.write(i);
      delay(20);
    }
  }

  // 4. Send Data every 1s
  if (millis() - lastMqtt >= 1000) {
    // LCD Layout for 4 Values:
    // T:25.5 G:1200
    // S:450  F:0 (Safe)
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("T:");
    lcd.print(temp);
    lcd.print(char(223));
    lcd.print(" Gas:");
    lcd.print(mq6);
    lcd.setCursor(0, 1);
    lcd.print("Smoke:");
    lcd.print(mq2);
    lcd.print(" F:");
    lcd.print(fire);

    // Payload: mq2,mq6,fire,temp,status
    String payload = String(mq2) + "," + String(mq6) + "," + String(fire) + "," + String(temp) + "," + currentStatus;
    client.publish("safety1172026", payload.c_str());
    Serial.println("MQTT Sent: " + payload);

    lastMqtt = millis();

    currentStatus = "Safe";
  }
}
