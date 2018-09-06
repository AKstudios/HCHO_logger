// OSBSS T/RH/L/HCHO data logger based on Plantower DS-HCHO sensor, AMS TSL2591 and Sensirion SHT31/35 sensor
// read interval stored in config.txt, data stored in data.csv
// Last edited on September 6, 2018 - Arduino IDE 1.8.5, SdFat library (Aug 2018 commit)
// Use NeoSWSerial library instead of SoftwareSerial - handles interrupts much faster and way better so clock doesn't error out after alarm is triggered.

#include <EEPROM.h>
#include <NeoSWSerial.h>
#include <Wire.h>
#include <SPI.h>
#include <DS3234lib3.h>         // https://github.com/OSBSS/DS3234lib3
#include <PowerSaver.h>         // https://github.com/OSBSS/PowerSaver
#include "SdFat.h"              // http://www.osbss.com/wp-content/uploads/2015/03/SdFat.zip
#include "Adafruit_SHT31.h"     // https://github.com/adafruit/Adafruit_SHT31
#include <Adafruit_Sensor.h>  // https://github.com/adafruit/Adafruit_Sensor
#include <Adafruit_TSL2591.h> // https://github.com/adafruit/Adafruit_TSL2591_Library

// Launch Variables   ******************************
long interval = 30;  // set logging interval in SECONDS, eg: set 300 seconds for an interval of 5 mins
int dayStart = 14, hourStart = 13, minStart = 41;    // define logger start time: day of the month, hour, minute
char filename[15] = "data.csv";    // Set filename Format: "12345678.123". Cannot be more than 8 characters in length, contain spaces or begin with a number
int data; // read each byte of data from sd card and store here

// Global objects and variables   ******************************
Adafruit_SHT31 sht31 = Adafruit_SHT31();
Adafruit_TSL2591 tsl = Adafruit_TSL2591(2591); // pass in a number for the sensor identifier (for your use later)
PowerSaver chip;  	// declare object for PowerSaver class
DS3234 RTC;    // declare object for DS3234 class
SdFat sd;
SdFile myFile;
NeoSWSerial mySerial(4, 3); // RX, TX
byte cmd[7] = {0x42,0x4d,0x01,0x00,0x00,0x00,0x90};
unsigned char response[18]; // expect a 9 bytes response, give twice the size just in case things go crazy, it reduces likelyhood of crash/buffer overrun
char buf[31];

//#define POWER 3    // pin 3 supplies power to microSD card breakout
#define LED 7  // pin 7 controls LED
int chipSelect = 9; // pin 9 is CS pin for MicroSD breakout

// IMP: DON'T DECLARE ISR HERE IF USING SOFTWARE SERIAL LIBRARIES THAT DEPEND ON PIN CHANGE INTERRUPTS!
// ISR ****************************************************************
//ISR(PCINT0_vect)  // Interrupt Vector Routine to be executed when pin 8 receives an interrupt.
//{
//  //PORTB ^= (1<<PORTB1);
//  asm("nop");
//}


// setup ****************************************************************
void setup()
{
  Serial.begin(19200); // open serial at 19200 bps
  mySerial.begin(9600);  // initialize HCHO sensor in UART mode
  Serial.flush(); // clear the buffer
  delay(1);
  Serial.println(RTC.timeStamp());    // get date and time from RTC
  delay(10);
  
  pinMode(LED, OUTPUT); // set output pins
  Wire.begin();  // initialize I2C using Wire.h library
  delay(1);    // give some delay to ensure things are initialized properly

  if (! sht31.begin(0x44))  // Set to 0x45 for alternate i2c addr
  {   
    Serial.println("Couldn't find SHT31/35");
    delay(10);
  }
  
  if (! tsl.begin())
  {
    Serial.println("Couldn't find TSL2591");
    delay(10);
  }
  else
  {
    tsl.setGain(TSL2591_GAIN_MED);      // 25x gain
    tsl.setTiming(TSL2591_INTEGRATIONTIME_100MS);  // shortest integration time (bright light)
  }


  if(!sd.begin(chipSelect, SPI_HALF_SPEED))  // initialize microSD card
  {
    SDcardError(3);
  }
  else
  {
    // open the file for write at end like the Native SD library
    if(!myFile.open(filename, O_RDWR | O_CREAT | O_AT_END))
    {
      SDcardError(2);
    }

    else
    {    
      myFile.println();
      myFile.println("Date/Time,Temp(C),RH(%),Light(lux),HCHO(mg/m3)");
      //myFile.println();
      myFile.close();
      
      digitalWrite(LED, HIGH);
      delay(10);
      digitalWrite(LED, LOW);
    }
  }
  
  if(!myFile.open("config.txt", O_READ))  // open config.txt and read logging interval
  {
    Serial.println("config.txt file error. Setting default interval to 30 seconds.");
    delay(50);
  }

  else
  {
    char interval_array[5]; // should be able to store 5 digit maximum (86,400 seconds = 24 hours)
    int i=0; // index of array
    long _interval;  // logging interval read from file
    boolean error_flag = false;
    
    while(1)
    {
      data = myFile.read();  // read next byte in file
      
      if(data < 0)  // if no bytes remaining, myFile.read() returns -1
        break;      // exit loop

      if(data >= 48 && data <= 57) // check if data is digits ONLY (between 0-9). ASCII value of 0 = 48; 9 = 57
        interval_array[i] = (char)data;  // store digit in char array
      else  // if any other charecter other than numbers 0-9
      {
        error_flag = true;
        break;
      }
      
      i++; // increment index of array by 1
      if(i>5)  // only read the first 5 digits in file. Ignore all other values
        break;
    }
    myFile.close();  // close file - very important!
    sscanf(interval_array, "%ld", &_interval);  // "%ld" will convert array to long (use "%d" to convert to int)
    
    if(_interval <= 0 || _interval > 86400 || error_flag == true)
    {
      Serial.println("Incorrect interval format or out of bounds. Must be a number between 1 and 86,400 seconds. Setting default interval to 30 seconds.");
      delay(50);
    }
    else
    {
      interval = _interval;
      Serial.print("New logging interval set: ");
      Serial.println(interval);
      delay(50);
    }
  }
  Serial.println();
  RTC.setNewAlarm(interval);
  chip.sleepInterruptSetup();    // setup sleep function on the ATmega328p. Power-down mode is used here
  delay(10);
}


// loop ****************************************************************
void loop()
{  
  chip.turnOffADC();    // turn off ADC to save power
  chip.turnOffSPI();  // turn off SPI bus to save power
  chip.turnOffWDT();  // turn off WatchDog Timer to save power (does not work for Pro Mini - only works for Uno)
  chip.turnOffBOD();    // turn off Brown-out detection to save power
  
  
  chip.goodNight();    // put processor in extreme power down mode - GOODNIGHT!
                       // this function saves previous states of analog pins and sets them to LOW INPUTS
                       // average current draw on Mini Pro should now be around 0.195 mA (with both onboard LEDs taken out)
                       // Processor will only wake up with an interrupt generated from the RTC, which occurs every logging interval
  
  chip.turnOnADC();    // enable ADC after processor wakes up
  chip.turnOnSPI();   // turn on SPI bus once the processor wakes up
  delay(1);    // important delay to ensure SPI bus is properly activated
  
  RTC.alarmFlagClear();    // clear alarm flag
  
  RTC.checkDST();  // check and account for Daylight Saving Time in US

  String time = RTC.timeStamp();    // get date and time from RTC
  
  Serial.println(time);
  delay(10);
  
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();
  float l = 0;
  
  sensors_event_t event;
  tsl.getEvent(&event);
  if ((event.light == 0) | (event.light > 4294966000.0) | (event.light <-4294966000.0))
  {
    l = 0;  // invalid value; replace with 'NAN' if needed
  }
  else
  {
    l = event.light;
  }
  
  mySerial.listen();
  delay(1);
  while(mySerial.available() > 0)  // clear out buffer
    char x = mySerial.read();
  mySerial.write(cmd,7);  // send command to HCHO sensor
  delay(1);
  mySerial.readBytes(buf, 10);
  float hcho = ((buf[6] << 8) + buf[7])/100.0;
  
//  For some weird reason, printing to serial here is causing the clock to .. clock out?
//  Serial.print("Temperature: ");
//  Serial.print(t);
//  Serial.println(" C");
//  
//  Serial.print("Humidity: ");
//  Serial.print(h); 
//  Serial.println(" %");
//  
//  Serial.print("Light: ");
//  Serial.print(l); 
//  Serial.println(" lux");
//  
//  Serial.print("HCHO: ");
//  Serial.print(hcho, 3);
//  Serial.println(" mg/m3");
//
//  Serial.println();
//  Serial.println("---------------------");
//  delay(50); // give some delay to ensure data is properly sent over serial
  

  if(!sd.begin(chipSelect, SPI_HALF_SPEED))
  {
    SDcardError(3);
  }
  else
  {
    if(!myFile.open(filename, O_RDWR | O_CREAT | O_AT_END))
    {
      SDcardError(2);
    }
  
    else
    {
      myFile.print(time);
      myFile.print(",");
      myFile.print(t, 3);
      myFile.print(",");
      myFile.print(h, 3);
      myFile.print(",");
      myFile.print(l);
      myFile.print(",");
      myFile.println(hcho, 3);
      myFile.close();
      
      digitalWrite(LED, HIGH);
      delay(10);
      digitalWrite(LED, LOW);
    }
  }
  
  RTC.setNextAlarm();      //set next alarm before sleeping
  delay(1);
}


// SD card Error response ****************************************************************
void SDcardError(int n)
{
    for(int i=0;i<n;i++)   // blink LED 3 times to indicate SD card write error; 2 to indicate file read error
    {
      digitalWrite(LED, HIGH);
      delay(50);
      digitalWrite(LED, LOW);
      delay(150);
    }
}

//****************************************************************


