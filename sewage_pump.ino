//
// This monitors the operation of the sewage pump
//
#include <Adafruit_ADS1X15.h> // from package "Adafruit ADS1X15" by Adafruit
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

#define MSG_SIZE_MAX    16
#define FLOAT_SIZE_MAX  8
#define MAX_WIFI_WAIT   10

const char* ssid = "AmesHouse";
const char* password = "Thunderbird1";
const char* ota_hostname = "sewage_pump";

const char* host = "optiplex";
const uint16_t port = 27910;

Adafruit_ADS1115 ads;
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

float multiplier = 0.0625f;

void connectToWifi()
{
  int wifiRetries = 0;
  WiFi.mode(WIFI_STA);
  Serial.print("WiFi is down. Connecting");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED && wifiRetries < MAX_WIFI_WAIT) {
    delay(1000);
    wifiRetries++;
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
     Serial.println("WiFi failed to connect");
  }
}


void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  delay(1000);
  Serial.println(F("Hello"));

  Serial.println(); Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  connectToWifi();

  Wire.begin();
  ads.begin();

  MDNS.begin(ota_hostname);

  httpUpdater.setup(&httpServer);
  httpServer.begin();

  MDNS.addService("http", "tcp", 80);

  //                                                                ADS1015  ADS1115
  //                                                                -------  -------
  // ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit = 3mV      0.1875mV (default)
  // ads.setGain(GAIN_ONE);        // 1x gain   +/- 4.096V  1 bit = 2mV      0.125mV
  // ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit = 1mV      0.0625mV
  // ads.setGain(GAIN_FOUR);       // 4x gain   +/- 1.024V  1 bit = 0.5mV    0.03125mV
  // ads.setGain(GAIN_EIGHT);      // 8x gain   +/- 0.512V  1 bit = 0.25mV   0.015625mV
  // ads.setGain(GAIN_SIXTEEN);    // 16x gain  +/- 0.256V  1 bit = 0.125mV  0.0078125mV
  
  // The above is taken from https://github.com/GreenPonik/Adafruit_ADS1X15/blob/7fce1e43c48ae32c40f806362060d91b34de3318/examples/differential/differential.pde
  // The output of the SCT-013-000V (which I'm pretty sure is what i have) will be between 0V-1V.
  // 1V means 100A which we should never see, BUT, anything can happen sometimes. i don't wanna damage my ADS.
  // So go with GAIN_TWO. That should keep us safe.
  ads.setGain((adsGain_t)GAIN_TWO);
}

int16_t  a0_a1;
float prev_mv = 0.0f;
float mv = 0.0f;

float current_rms = 0.0f;

int prev_vector = 0;
int vector = 0;

int delta = 0;

WiFiClient client;
char msg[MSG_SIZE_MAX];
char tempFloat[FLOAT_SIZE_MAX];

void loop() {

  //digitalWrite(LED_BUILTIN, LOW);
  if (WiFi.status() != WL_CONNECTED) {
    // wifi died. try to reconnect
    connectToWifi();
  } else {
    httpServer.handleClient();
    MDNS.update();
  }

  delay(8);

  // Read current
  a0_a1 = ads.readADC_Differential_0_1();

  if (a0_a1 != 0 && a0_a1 != -1) {
    // Because you're going to come back in here years later and not know wth this is doing, here's a bone.
    // Remember that a0_a1 is a reading of the differential voltage between A0 and A1 of the ADC.
    // We set the gain at "GAIN_TWO", which means the adc is reading voltage between +2.048V and -2.048V,
    // at 16 bits of resolution (65535 possible values). That's a full peak to peak range of (2.048 * 2 = 4.096).
    // 4.096 / 65535 = .0625. So 16 bits can tell us a value between +-2.048v within .0625v of accuracy.
    // To calculate the actual voltage value, you can think of it like divisions on an oscilliscope.
    // Whatever it spits out, you have to multiply it by whatever each division represents.
    // In our case, .0625. If you change the gain in the future, you gotta see what that full pk2pk range is,
    // divide it by the resolution of the adc (16 bits [65535] for the ADS 1115), and use that as your 'multiplier'.
    mv = a0_a1 * multiplier;
    delta = mv - prev_mv;
    vector = delta > 0 ? 1 : -1;

    if (vector == -1 && prev_vector == 1) {
      // Voltage is dropping from its positive peak.
      // This means the last mv value is the peak.
      // Calculate rms of the peak voltage. Keep it simple.
      // prev_mv is in millivolts, so divide by 1000 to turn it back into whole Volts.
      // Then x100 because the SCT-013-000V puts out 100A per 1V.
      // Then x.707 to get rough rms.
      current_rms = prev_mv / 1000 * 100 * 0.707f;
      if (current_rms > 0 && WiFi.status() == WL_CONNECTED) {
        if (client.connect(host, port)) {
          memset(tempFloat, 0, FLOAT_SIZE_MAX);
          memset(msg, 0, MSG_SIZE_MAX);
          dtostrf(current_rms, 3, 2, tempFloat);
          snprintf(msg, MSG_SIZE_MAX, "sp:%s", tempFloat);
          if (client.connected()) { client.println(msg); }
          Serial.println(msg);
          client.stop();
        } else {
          Serial.println("connection failed");
          delay(500);
        }
      }
    }
    prev_vector = vector;
    prev_mv = mv;
  }
}
