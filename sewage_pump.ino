
#include <Adafruit_ADS1X15.h>

Adafruit_ADS1115 ads;


uint32_t number_of_samples = 240;
uint32_t trigger = 100;
int no_more_samples = 0;
int no_more_output = 0;

float multiplier = 0.0625f;

int samples[240];
int samples_index = 0;

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  delay(1000);
  Serial.println(F("Hello"));

  Wire.begin();
  ads.begin();

  //                                                                ADS1015  ADS1115
  //                                                                -------  -------
  // ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit = 3mV      0.1875mV (default)
  // ads.setGain(GAIN_ONE);        // 1x gain   +/- 4.096V  1 bit = 2mV      0.125mV
  // ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit = 1mV      0.0625mV
  // ads.setGain(GAIN_FOUR);       // 4x gain   +/- 1.024V  1 bit = 0.5mV    0.03125mV
  // ads.setGain(GAIN_EIGHT);      // 8x gain   +/- 0.512V  1 bit = 0.25mV   0.015625mV
  // ads.setGain(GAIN_SIXTEEN);    // 16x gain  +/- 0.256V  1 bit = 0.125mV  0.0078125mV
  // The above from taken from https://github.com/GreenPonik/Adafruit_ADS1X15/blob/7fce1e43c48ae32c40f806362060d91b34de3318/examples/differential/differential.pde
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

void loop() {

  //digitalWrite(LED_BUILTIN, LOW);

  delay(8);

  // Read current
  a0_a1 = ads.readADC_Differential_0_1();

  if (a0_a1 != 0 && a0_a1 != -1)
  {
    mv = a0_a1 * multiplier;
    delta = mv - prev_mv;
    vector = delta > 0 ? 1 : -1;

    if (vector == -1 && prev_vector == 1)
    {
      // Voltage is dropping from its positive peak.
      // This means the last mv value is the peak.
      // Calculate rms of the peak voltage. Keep it simple.
      current_rms = prev_mv / 1000 * 100 * 0.707f;
      Serial.println(current_rms);
    }
    prev_vector = vector;
    prev_mv = mv;
  }
}























