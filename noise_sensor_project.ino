#include <ESP8266WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>

// Watson IoT connection details
// Here it is necessary to substitute a few variables
// Organization ID: <XXX>
// Device Type: <YYY>
// Device ID: <ZZZ>
// Authentication key: <PPP>  
#define MQTT_HOST "<XXX>.messaging.internetofthings.ibmcloud.com"
#define MQTT_PORT 1883
#define MQTT_DEVICEID "d:<XXX>:<YYY>:<ZZZ>"
#define MQTT_USER "use-token-auth"
#define MQTT_TOKEN "<PPP>"
#define MQTT_TOPIC "iot-2/evt/status/fmt/json"
#define MQTT_TOPIC_DISPLAY "iot-2/cmd/display/fmt/json"
#define MQTT_TOPIC_INTERVAL "iot-2/cmd/interval/fmt/json"

// Add GPIO pins used to connect devices
#define GRB_PIN D4 // GPIO pin the data line of GRB LED is connected to

// Specify LED type
#define NEOPIXEL_TYPE NEO_GRB + NEO_KHZ800

// WiFi configuration
char ssid[] = "your-connection";  // your network SSID (name)
char pass[] = "your-password";  // your network password

Adafruit_NeoPixel pixel = Adafruit_NeoPixel(1, GRB_PIN, NEOPIXEL_TYPE);

// MQTT objects
void callback(char* topic, byte* payload, unsigned int length);
WiFiClient wifiClient;
PubSubClient mqtt(MQTT_HOST, MQTT_PORT, callback, wifiClient);

// variables to hold data
StaticJsonDocument<100> jsonDoc;
JsonObject payload = jsonDoc.to<JsonObject>();
JsonObject status = payload.createNestedObject("d");
StaticJsonDocument<100> jsonReceiveDoc;
static char msg[50];

double volts = 0.0; // volts
unsigned char r = 0; // LED RED value
unsigned char g = 0; // LED Green value
unsigned char b = 0; // LED Blue value
int32_t ReportingInterval = 5;  // Reporting Interval seconds


//Defining sample window

const int sampleWindow = 50; // Sample window width in mS (50 mS = 20Hz)
unsigned int sample;

void callback(char* topic, byte* payload, unsigned int length) {
	// handle message arrived
	Serial.print("Message arrived [");
	Serial.print(topic);
	Serial.print("] : ");
  
	payload[length] = 0; // ensure valid content is zero terminated so can treat as c-string
	Serial.println((char *)payload);
	DeserializationError err = deserializeJson(jsonReceiveDoc, (char *)payload);
	
	if (err) {
		Serial.print(F("deserializeJson() failed with code ")); 
		Serial.println(err.c_str());
	}
	else {
		JsonObject cmdData = jsonReceiveDoc.as<JsonObject>();
		if (0 == strcmp(topic, MQTT_TOPIC_DISPLAY)) {
		//valid message received
		r = cmdData["r"].as<unsigned char>(); // this form allows you specify the type of the data you want from the JSON object
		g = cmdData["g"];
		b = cmdData["b"];
		jsonReceiveDoc.clear();
		pixel.setPixelColor(0, r, g, b);
		pixel.show();
		}
		else if (0 == strcmp(topic, MQTT_TOPIC_INTERVAL)) {
			//valid message received
			ReportingInterval = cmdData["Interval"].as<int32_t>(); // this form allows you specify the type of the data you want from the JSON object
			Serial.print("Reporting Interval has been changed:");
			Serial.println(ReportingInterval);
			jsonReceiveDoc.clear();
		}
		else {
			Serial.println("Unknown command received");
		}
	}
}


void setup() {
	// Start serial console
	Serial.begin(115200);
	Serial.setTimeout(2000);
	while (!Serial) { }
	Serial.println();
	Serial.println("ESP8266 Sensor Application");

	// Start WiFi connection
	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, pass);
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}
	Serial.println("");
	Serial.println("WiFi Connected");

	// Start connected devices
	pixel.begin();

	// Connect to MQTT - IBM Watson IoT Platform
	while(! mqtt.connected()){
		if (mqtt.connect(MQTT_DEVICEID, MQTT_USER, MQTT_TOKEN)) { // Token Authentication
			Serial.println("MQTT Connected");
			mqtt.subscribe(MQTT_TOPIC_DISPLAY);
			mqtt.subscribe(MQTT_TOPIC_INTERVAL);
		}
		else {
			Serial.println("MQTT Failed to connect! ... retrying");
			delay(500);
		}
	}
}


void loop() {

	unsigned long startMillis= millis();  // Start of sample window
	unsigned int peakToPeak = 0;   // peak-to-peak level
	unsigned int signalMax = 0;
	unsigned int signalMin = 1024;

	mqtt.loop();
	while (!mqtt.connected()) {
		Serial.print("Attempting MQTT connection...");
		// Attempt to connect
		if (mqtt.connect(MQTT_DEVICEID, MQTT_USER, MQTT_TOKEN)) {
			Serial.println("MQTT Connected");
			mqtt.subscribe(MQTT_TOPIC_DISPLAY);
			mqtt.subscribe(MQTT_TOPIC_INTERVAL);
			mqtt.loop();
		}
		else {
			Serial.println("MQTT Failed to connect!");
			delay(5000);
		}
	}
   
	while (millis() - startMillis < sampleWindow) {
		sample = analogRead(0);
		if (sample < 1024)  // toss out spurious readings {
			if (sample > signalMax) {
				signalMax = sample;  // save just the max levels
			}
			else if (sample < signalMin) {
				signalMin = sample;  // save just the min levels
			}
		}
	}
	peakToPeak = signalMax - signalMin;  // max - min = peak-peak amplitude
	volts = (peakToPeak * 5.0) / 1024;

	status["volts"] = volts;
	serializeJson(jsonDoc, msg, 50);
	Serial.println(msg);
	if (!mqtt.publish(MQTT_TOPIC, msg)) {
		Serial.println("MQTT Publish failed");
	}

	for (int32_t i = 0; i < ReportingInterval; i++) {
		mqtt.loop();
		delay(1000);
	}
}
