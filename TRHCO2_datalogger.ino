// OSBSS T/RH/CO2 data logger based on SenseAir K-30 1% CO2 sensor and Sensirion SHT35 sensor
// read interval stored in config.txt
// Last edited on June 15, 2017 - Arduino IDE 1.8.2, Latest SdFat library

#include <EEPROM.h>
#include <Wire.h>
#include <SPI.h>
#include <DS3234lib3.h>         // https://github.com/OSBSS/DS3234lib3
#include <PowerSaver.h>         // https://github.com/OSBSS/PowerSaver
#include "SdFat.h"              // https://github.com/greiman/SdFat
#include "Adafruit_SHT31.h"     // https://github.com/adafruit/Adafruit_SHT31

// Launch Variables   ******************************
long interval = 30;  // set logging interval in SECONDS, eg: set 300 seconds for an interval of 5 mins
int dayStart = 14, hourStart = 13, minStart = 41;    // define logger start time: day of the month, hour, minute
char filename[15] = "data.csv";    // Set filename Format: "12345678.123". Cannot be more than 8 characters in length, contain spaces or begin with a number
int data; // read each byte of data from sd card and store here

// Global objects and variables   ******************************
Adafruit_SHT31 sht31 = Adafruit_SHT31();
PowerSaver chip;  	// declare object for PowerSaver class
DS3234 RTC;    // declare object for DS3234 class
SdFat sd;
SdFile myFile;

//#define POWER 3    // pin 3 supplies power to microSD card breakout
#define LED 7  // pin 7 controls LED
int chipSelect = 9; // pin 9 is CS pin for MicroSD breakout

int CO2ppm = 0;
int _CO2ppm;

// ISR ****************************************************************
ISR(PCINT0_vect)  // Interrupt Vector Routine to be executed when pin 8 receives an interrupt.
{
  //PORTB ^= (1<<PORTB1);
  asm("nop");
}

// setup ****************************************************************
void setup()
{
  Serial.begin(19200); // open serial at 19200 bps
  Serial.flush(); // clear the buffer
  delay(5);
  
  Serial.println("Setup");
  delay(10);
  
  pinMode(LED, OUTPUT); // set output pins
  Wire.begin();  // initialize I2C using Wire.h library
  delay(1);    // give some delay to ensure things are initialized properly

  if (! sht31.begin(0x44))  // Set to 0x45 for alternate i2c addr
  {   
    Serial.println("Couldn't find SHT31/35");
    delay(10);
  }
  
  Serial.println(RTC.timeStamp());    // get date and time from RTC
  delay(10);

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
      myFile.println("Date/Time,Temp(C),RH(%),CO2(ppm)");
      myFile.println();
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
      Serial.println("Incorrect format or out of bounds. Must be a number between 1 and 86,400 seconds. Setting default interval to 30 seconds.");
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
  
  CO2ppm = GetCO2(0x68); // default address for K-30 CO2 sensor is 0x68
  
  // account for dropped values
  if(CO2ppm > 0)
    _CO2ppm = CO2ppm;
  if(CO2ppm <=0)
    CO2ppm = _CO2ppm;

  Serial.print("Temperature: ");
  Serial.print(t);
  Serial.println(" C");
  
  Serial.print("Humidity: ");
  Serial.print(h); 
  Serial.println(" %");
  
  Serial.print("CO2: ");
  Serial.print(CO2ppm);
  Serial.println(" ppm");
  
  Serial.println();
  delay(50); // give some delay to ensure CO2 data is properly received from sensor
  

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
      myFile.print(t);
      myFile.print(",");
      myFile.print(h);
      myFile.print(",");
      myFile.println(CO2ppm);
      myFile.close();
      
      digitalWrite(LED, HIGH);
      delay(10);
      digitalWrite(LED, LOW);
    }
  }
  
  RTC.setNextAlarm();      //set next alarm before sleeping
  delay(1);
}

// Get CO2 concentration ****************************************************************
int GetCO2(int address)
{
  byte recieved[4] = {0,0,0,0}; // create an array to store bytes received from sensor
  
  Wire.beginTransmission(address);
  Wire.write(0x22);
  Wire.write(0x00);
  Wire.write(0x08);
  Wire.write(0x2A);
  Wire.endTransmission();
  delay(20); // give delay to ensure transmission is complete
  
  Wire.requestFrom(address,4);
  delay(10);
  
  byte i=0;
  while(Wire.available())
  {
    recieved[i] = Wire.read();
    i++;
  }
  
  byte checkSum = recieved[0] + recieved[1] + recieved[2];
  CO2ppm = (recieved[1] << 8) + recieved[2];
  
  if(checkSum == recieved[3])
    return CO2ppm;
  else
    return -1;
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

