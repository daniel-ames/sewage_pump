//
// This monitors the operation of the sewage pump
//
#include <Adafruit_ADS1X15.h> // from package "Adafruit ADS1X15" by Adafruit
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <time.h>
#include <TZ.h>
#include "street_cred.h"

#define MSG_SIZE_MAX    32
#define FLOAT_SIZE_MAX  8
#define MAX_WIFI_WAIT   10

// the builtin led is active low for some dipshit reason
#define ON  LOW
#define OFF HIGH

const char* host = "optiplex";
//const char* host = "192.168.1.212";
const uint16_t port = 27910;

Adafruit_ADS1115 ads;
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

#define SYS_STATUS_PAGE_STR_LEN 2560
char systemStatusPageStr[SYS_STATUS_PAGE_STR_LEN];
char httpStr[256] = {0};
char uptime[41] = {0};
char current_time[41] = {0};
char last_flush_time[41] = {0};

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

// Converts milliseconds into natural language
void millisToDaysHoursMinutes(unsigned long milliseconds, char* str, int length)
{
  uint seconds = milliseconds / 1000;
  memset(str, 0, length);

  if (seconds <= 60) {
    // It's only been a few seconds
    // Longest string example, 11 chars: 59 seconds\0
    snprintf(str, 11, "%d second%s", seconds, seconds == 1 ? "" : "s");
    return;
  }
  uint minutes = seconds / 60;
  if (minutes <= 60) {
    // It's only been a few minutes
    // Longest string example, 11 chars: 59 minutes\0
    snprintf(str, 11, "%d minute%s", minutes, minutes == 1 ? "" : "s");
    return;
  }
  uint hours = minutes / 60;
  minutes -= hours * 60;
  if (hours <= 24) {
    // It's only been a few hours
    if (minutes == 0)
      // Longest string example, 9 chars: 23 hours\0
      snprintf(str, 9, "%d hour%s", hours, hours == 1 ? "" : "s");
    else
      // Longest string example, 24 chars: 23 hours and 59 minutes\0
      snprintf(str, 24, "%d hour%s and %d minute%s", hours, hours == 1 ? "" : "s", minutes, minutes == 1 ? "" : "s");
    return;
  }

  // It's been more than a day
  uint days = hours / 24;
  hours -= days * 24;
  if (minutes == 0)
    // Longest string example, 23 chars: 9999 days and 23 hours\0
    snprintf(str, 23, "%d day%s and %d hour%s", days, days == 1 ? "" : "s", hours, hours == 1 ? "" : "s");
  else
    // Longest string example, 35 chars: 9999 days, 23 hours and 59 minutes\0
    snprintf(str, 35, "%d day%s, %d hour%s and %d minute%s", days, days == 1 ? "" : "s", hours, hours == 1 ? "" : "s", minutes, minutes == 1 ? "" : "s");
}

void get_time(char *time_buf, int size)
{
  int time_retries = 40;  // try for about 10 seconds

  memset(time_buf, 0, size);
  configTime(TZ_America_Chicago, "pool.ntp.org");

  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2 && time_retries) {   // basically "still 1970?"
    delay(250);
    now = time(nullptr);
    time_retries--;
  }

  if (time_retries) {
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    strftime(time_buf, size, "%m-%d-%Y %I:%M:%S %p", &tm_now);
  } else {
    snprintf(time_buf, size, "unknown");
  }
}

char* getSystemStatus()
{
  String html;
  
  get_time(current_time, sizeof(current_time));

  // Pardon the html mess. Gotta tell the browser to not make the text super tiny.
  html = "<!DOCTYPE html><html><head><title>Sewage Pump</title></head><body><p style=\"font-size:36px\">";
  html += "<span style=\"font-size:90px\">";

  // Longest string example, 82 chars: Notifications are <span id='lights_span' style="color:Green;">ON</span>
  snprintf(httpStr, 100, "RSSI: %d", WiFi.RSSI());
  html += httpStr;
  html += "</br>";
  snprintf(httpStr, 60, "System time: %s", current_time);
  html += httpStr;
  html += "</br>";
  millisToDaysHoursMinutes(millis(), uptime, 40);
  snprintf(httpStr, 60, "Uptime: %s", uptime);
  html += httpStr;
  html += "</br>";
  snprintf(httpStr, 60, "Last flush: %s", last_flush_time);
  html += httpStr;
  html += "</br>";
  html += "</span></br>";
  
  // Close it off
  html += "</p></body></html>";

  memset(systemStatusPageStr, 0, SYS_STATUS_PAGE_STR_LEN);
  html.toCharArray(systemStatusPageStr, html.length() + 1);
  return systemStatusPageStr;
}


void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, OFF);

  Serial.begin(115200);
  delay(1000);
  Serial.println("Hello");

  Serial.println(); Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  connectToWifi();

  Wire.begin();
  ads.begin();

  MDNS.begin(ota_hostname);

  httpServer.on("/", HTTP_GET, []() {
    httpServer.sendHeader("Connection", "close");
    httpServer.send(200, "text/html", getSystemStatus());
  });

  httpUpdater.setup(&httpServer);
  httpServer.begin();

  MDNS.addService("http", "tcp", 80);

  snprintf(last_flush_time, sizeof(last_flush_time), "No flushes yet");

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
  // 1V means 100A which we should never see, BUT, anything can happen sometimes. i don't wanna damage my ADC.
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

int led_timer = 0;
bool led_on = false;

void loop() {

  if (WiFi.status() != WL_CONNECTED) {
    // wifi died. try to reconnect
    connectToWifi();
  } else {
    httpServer.handleClient();
    MDNS.update();
  }

  if (led_timer > 0) {
    if (!led_on) {
      digitalWrite(LED_BUILTIN, ON);
      led_on = true;
    }
    led_timer--;
  } else {
    if (led_on) {
      digitalWrite(LED_BUILTIN, OFF);
      led_on = false;
      get_time(last_flush_time, sizeof(last_flush_time));
    }
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
      // Then x100 because the SCT-013-000V puts out 1V per 100A.
      // Then x.707 to get rough rms.
      current_rms = prev_mv / 1000 * 100 * 0.707f;
      if (current_rms > 0 && WiFi.status() == WL_CONNECTED) {
        if (client.connect(host, port)) {
          memset(tempFloat, 0, FLOAT_SIZE_MAX);
          memset(msg, 0, MSG_SIZE_MAX);
          dtostrf(current_rms, 3, 2, tempFloat);
          snprintf(msg, MSG_SIZE_MAX, "dev=1 amps=%s\n", tempFloat);
          if (client.connected()) { client.println(msg); }
          //Serial.println(msg);
          client.stop();
        } else {
          //Serial.println("connection failed");
          delay(500);
        }
      }
      led_timer = 20;
    }
    prev_vector = vector;
    prev_mv = mv;
  }
}
