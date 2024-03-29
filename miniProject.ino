/* This code works with MAX30102 + 128x32 OLED i2c + Buzzer and Arduino UNO
   It's displays the Average BPM on the screen, with an animation and a buzzer sound
   everytime a heart pulse is detected
   It's a modified version of the HeartRate library example
   Refer to www.surtrtech.com for more details or SurtrTech YouTube channel
*/

#include <Adafruit_GFX.h>       //OLED libraries
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266WiFi.h>
#include "MAX30105.h"           //MAX3010x library
#include "heartRate.h"          //Heart rate calculating algorithm
#include "spo2_algorithm.h"     //SpO2 calculating algorithm
#include "LIS3DSH.h"
#include "ThingSpeak.h"
#include "secrets.h" // Wi-Fi, Thingspeak channel-id, api key


unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;
char ssid[] = SECRET_SSID;   // Wifi account
char pass[] = SECRET_PASS;   // Wifi password

WiFiClient client;
// digitalRead(this->pin) 0 Down or 1 Up
int sw53 = 4; int sw53state = 1;
int sw54 = 5; int sw54state = 1;

MAX30105 particleSensor;

const byte RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE]; //Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; //Time at which the last beat occurred
long lastwifi = 0; // Control Wifi send frequency
long lastacc = 0;
float beatsPerMinute;
int beatAvg;
boolean useWIFI = false;
boolean doublecheck = false;
int16_t x, y, z;
long irValue;

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

//OLED
// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
// Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, i2c Addr, Reset share with 8266 reset);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

static const unsigned char PROGMEM logo2_bmp[] =
{ 0x03, 0xC0, 0xF0, 0x06, 0x71, 0x8C, 0x0C, 0x1B, 0x06, 0x18, 0x0E, 0x02, 0x10, 0x0C, 0x03, 0x10,    //Logo2 and Logo3 are two bmp pictures that display on the OLED if called
  0x04, 0x01, 0x10, 0x04, 0x01, 0x10, 0x40, 0x01, 0x10, 0x40, 0x01, 0x10, 0xC0, 0x03, 0x08, 0x88,
  0x02, 0x08, 0xB8, 0x04, 0xFF, 0x37, 0x08, 0x01, 0x30, 0x18, 0x01, 0x90, 0x30, 0x00, 0xC0, 0x60,
  0x00, 0x60, 0xC0, 0x00, 0x31, 0x80, 0x00, 0x1B, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x04, 0x00,
};

static const unsigned char PROGMEM logo3_bmp[] =
{ 0x01, 0xF0, 0x0F, 0x80, 0x06, 0x1C, 0x38, 0x60, 0x18, 0x06, 0x60, 0x18, 0x10, 0x01, 0x80, 0x08,
  0x20, 0x01, 0x80, 0x04, 0x40, 0x00, 0x00, 0x02, 0x40, 0x00, 0x00, 0x02, 0xC0, 0x00, 0x08, 0x03,
  0x80, 0x00, 0x08, 0x01, 0x80, 0x00, 0x18, 0x01, 0x80, 0x00, 0x1C, 0x01, 0x80, 0x00, 0x14, 0x00,
  0x80, 0x00, 0x14, 0x00, 0x80, 0x00, 0x14, 0x00, 0x40, 0x10, 0x12, 0x00, 0x40, 0x10, 0x12, 0x00,
  0x7E, 0x1F, 0x23, 0xFE, 0x03, 0x31, 0xA0, 0x04, 0x01, 0xA0, 0xA0, 0x0C, 0x00, 0xA0, 0xA0, 0x08,
  0x00, 0x60, 0xE0, 0x10, 0x00, 0x20, 0x60, 0x20, 0x06, 0x00, 0x40, 0x60, 0x03, 0x00, 0x40, 0xC0,
  0x01, 0x80, 0x01, 0x80, 0x00, 0xC0, 0x03, 0x00, 0x00, 0x60, 0x06, 0x00, 0x00, 0x30, 0x0C, 0x00,
  0x00, 0x08, 0x10, 0x00, 0x00, 0x06, 0x60, 0x00, 0x00, 0x03, 0xC0, 0x00, 0x00, 0x01, 0x80, 0x00
};

LIS3DSH accel;

void setup() {
  ESP.wdtDisable();
  ESP.wdtFeed();
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  ThingSpeak.begin(client);
  Wire.begin(2,0);// For OLED
  accel.enableDefault();

  // Initialize sensor
  particleSensor.begin(Wire, I2C_SPEED_FAST); //Use default I2C port, 400kHz speed
  particleSensor.setup(); //Configure sensor with default settings
  particleSensor.setPulseAmplitudeRed(0x0A); //Turn Red LED to low to indicate sensor is running
  particleSensor.setPulseAmplitudeGreen(1); //Turn off Green LED

  //OLED Setup
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }

  //OLED diplay 1st line
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 10);
  display.println("IERG4230 IoT MAX30102"); // Display static text
  display.display();
  delay(500);
}

int i = 0;

void loop() {
  ESP.wdtFeed();
  irValue = particleSensor.getIR();    //Reading the IR value it will permit us to know if there's a finger on the sensor or not
  if (millis()-lastacc>1000){
    accel.readAccel(&x, &y, &z);
    lastacc = millis();
  }
  
  //Also detecting a heartbeat
  
  // Use Switch to Control WiFi
  int sw53new = digitalRead(sw53);
  if (sw53new == 0 && sw53state == 1) {useWIFI = !useWIFI;}
  sw53state = sw53new;
  // Use CheckForBeat or not
  int sw54new = digitalRead(sw54);
  if (sw54new == 0 && sw54state == 1) {doublecheck = !doublecheck;}
  sw54state = sw54new;

//  
//  if (irValue > 7000)
//  {                                         //If a finger is detected
//    display.clearDisplay();                       //Clear the display
//    //OLED diplay 1st line
//    display.setCursor(0, 10);
//    display.println("IERG4230 IoT MAX30102"); // Display static text
//    //OLED diplay 3rd line
//    display.setCursor(50, 30);
//    display.println("BPM:"); // Display static text
//    display.setCursor(74, 30);
//    display.println(beatAvg);
//    //OLED diplay 5th line
//    display.setCursor(50, 50);
////    display.println("SpO2:"); // Display static text
////    display.setCursor(80, 50);
////    display.println(beatAvg);
//    
//    display.drawBitmap(5, 30, logo2_bmp, 24, 21, WHITE);       //Draw the first bmp picture (little heart)
////    display.setTextSize(2);                                 //Near it display the average BPM you can display the BPM if you want
////    display.setTextColor(WHITE);
////    display.setCursor(50, 0);
////    display.println("BPM");
////    display.setCursor(50, 18);
////    display.println(beatAvg);
//    display.display();
//    checkForBeat(irValue)
    boolean checkRes = checkForBeat(irValue);
    if (!doublecheck) {checkRes = true;}
    if ( irValue > 7000 && checkRes)                        //If a heart beat is detected
    {
        display.clearDisplay();     //Clear the display
//        display.setCursor(0, 10);   //OLED diplay 1st line
//        display.println("IERG4230 IoT MAX30102"); // Display static text
        display.setCursor(0, 10);   //OLED diplay 3rd line
        display.println("BPM:");    // Display static text
        display.setCursor(24, 10);
        display.println(beatAvg);
        if (useWIFI) {
          display.setCursor(54,10);
          display.println("WiFi");  
        }
        if (doublecheck){
          display.setCursor(80,10);
          display.println("checkBeat"); 
        }
//        display.setCursor(0, 21);
//        display.println("BPMIns:");
//        display.setCursor(42, 21);
//        display.println(beatsPerMinute);
        display.setCursor(0,32);
        display.println("X:");
        display.setCursor(18,32);
        display.println(x);
        display.setCursor(0,43);
        display.println("Y:");
        display.setCursor(18,43);
        display.println(y);
        display.setCursor(0,54);
        display.println("Z:");
        display.setCursor(18,54);
        display.println(z);
        //OLED diplay 5th line
//        display.setCursor(50, 50);
//        display.println("SpO2:");   // Display static text
//        display.setCursor(80, 50);
//        display.println(beatAvg);
//        display.drawBitmap(0, 25, logo3_bmp, 32, 32, WHITE);    //Draw the second picture (bigger heart)
        display.display();
//      tone(3, 1000);                                       //And tone the buzzer for a 100ms you can reduce it it will be better
//      delay(100);
//      noTone(3);                                          //Deactivate the buzzer to have the effect of a "bip"
      //We sensed a beat!
//      Serial.print("Time=");
//      Serial.println(millis());
//      Serial.print("Accel ");
//      Serial.print("X: ");
//      Serial.print(x);
//      Serial.print(" Y: ");
//      Serial.print(y);
//      Serial.print(" Z: ");
//      Serial.println(z);
      
      long delta = millis() - lastBeat;                   //Measure duration between two beats
      lastBeat = millis();

      beatsPerMinute = 60 / (delta / 1000.0);             //Calculating the BPM

      if (beatsPerMinute < 255 && beatsPerMinute > 20)    //To calculate the average we strore some values (4) then do some math to calculate the average
      {
        rates[rateSpot++] = (byte)beatsPerMinute;         //Store this reading in the array
        rateSpot %= RATE_SIZE; //Wrap variable

        //Take average of readings
        beatAvg = 0;
        for (byte x = 0 ; x < RATE_SIZE ; x++)
          beatAvg += rates[x];
        beatAvg /= RATE_SIZE;
      }
      // send data to Thingspeak
      // connect or re-connect to Wi-Fi
      if (useWIFI){
        if (WiFi.status() != WL_CONNECTED) {
          int tryWIFI = 0;
          while (WiFi.status() != WL_CONNECTED && tryWIFI < 3) {
              WiFi.begin(ssid, pass); 
              tryWIFI += 1;
              delay(5000);
          }
        }
        // Connection Success!
        if (millis() - lastwifi > 10000){
          lastwifi = millis();
          if (beatAvg > 20 && beatAvg < 200){
            //ThingSpeak.setField(1,beatsPerMinute);
            ThingSpeak.setField(2,beatAvg);
            ThingSpeak.setField(3,x);
            ThingSpeak.setField(4,y);
            ThingSpeak.setField(5,z);
            int httpCode = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
            Serial.print("Respond code:");
            Serial.println(httpCode);
          }
        }
      }
      // Update data every second
      // end of WiFi Transmission
    

//    Serial.print("IR=");
//    Serial.print(irValue);
//    Serial.print(", BPM=");
//    Serial.print(beatsPerMinute);
//    Serial.print(", Avg BPM=");
//    Serial.println(beatAvg);
  }
  
  else 
  { //If no finger is detected it inform the user and put the average BPM to 0 or it will be stored for the next measure
    beatAvg = 0;
    display.clearDisplay();   //Clear the display
    display.setCursor(0, 10);
    display.println("IERG4230 IoT MAX30102"); // Display static text

    display.setCursor(0, 30);
    display.println("Please place your finger!"); // Display static text
    display.display();
//    Serial.print("irValue: ");
//    Serial.println(irValue);
//    Serial.print("X: ");
//    Serial.println(x);
//    display.clearDisplay();
//    display.setTextSize(1);
//    display.setTextColor(WHITE);
//    display.setCursor(30, 5);
//    display.println("Please Place ");
//    display.setCursor(30, 15);
//    display.println("your finger ");
//    display.display();
//    noTone(3);
  }
}
