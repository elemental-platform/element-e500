//*****************************************************************************************************************************
// Element E500 v1.2 firmware
// Developed by AKstudios

// Updated: 01/30/2020

//*****************************************************************************************************************************
// libraries in use

#include <RFM69.h>  //  https://github.com/LowPowerLab/RFM69
#include <SPI.h>
#include <Arduino.h>
#include <Wire.h> 
#include <avr/sleep.h>
#include <avr/wdt.h>

//*****************************************************************************************************************************
// configurable global variables - define node parameters

#define NODEID                116 // must be unique for each node on same network (supports 10bit addresses (up to 1023 node IDs))
#define NETWORKID             110
#define ROOM_GATEWAYID        110
#define GATEWAYID             1
#define GATEWAY_NETWORKID     1
#define ENCRYPTKEY            "RgUjXn2r5u8x/A?D" //has to be same 16 characters/bytes on all nodes, not more not less!
#define FREQUENCY_EXACT       905000000   // change the frequency in areas of interference (default: 915MHz)
#define FREQUENCY             RF69_915MHZ //Match this with the version of your Moteino! (others: RF69_433MHZ, RF69_868MHZ)
#define IS_RFM69HW            //uncomment only for RFM69HW! Leave out if you have RFM69W!
#define LED                   9 // led pin

// other global objects and variables
RFM69 radio;
int state, d, wake_interval = 0;
char dataPacket[150], _dataPacket[150];

//*****************************************************************************************************************************
// Interrupt Service Routines (ISR)

ISR(PCINT0_vect)  // ISR for PCINT0 vector (pin 8)
{
  PCMSK0 = 0x00;   // Disable all PCInt Interrupts
  //asm("nop");  // do nothing
}

ISR(WDT_vect)  // ISR for WatchDog Timer
{
  wdt_disable();  // disable watchdog
}

//*****************************************************************************************************************************

void setup()
{
  Serial.begin(115200);
  pinMode(LED, OUTPUT);  // pin 9 controls LED
  
  radio.initialize(FREQUENCY,NODEID,NETWORKID);
  radio.encrypt(ENCRYPTKEY);
#ifdef IS_RFM69HW
  radio.setHighPower(); //uncomment only for RFM69HW!
#endif
#ifdef FREQUENCY_EXACT
  radio.setFrequency(FREQUENCY_EXACT); //set frequency to some custom frequency
#endif

  fadeLED(LED);
}

//*****************************************************************************************************************************
// this function puts the node to sleep with pin change interrupt enabled

void PCINT_sleep()
{
  Serial.flush(); // empty the send buffer, before continue with; going to sleep
  radio.sleep();
  delay(1);
  
  cli();          // stop interrupts
  //DDRB |= ~(1<<DDB0); // set pin D8 as INPUT
  //PORTD |= (1<<PORTD7); //Activate pullup on pin D7
  PCIFR  |= bit (PCIF0);   // clear any outstanding interrupts
  PCICR |= (1<<PCIE0); // enable interrupts on PCINT[0:7]
  PCMSK0 |= (1<<PCINT0); // pin change mask register for pin D8
  sei();  // enable global interrupts

  byte _ADCSRA = ADCSRA;  // save ADC state
  ADCSRA &= ~(1 << ADEN);

  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();       

  sleep_enable();  
  sleep_bod_disable();
  sei();       
  sleep_cpu();   
    
  sleep_disable();   
  sei();  // enable global interrupts  

  ADCSRA = _ADCSRA; // restore ADC state (enable ADC)
  delay(1);
}

//*****************************************************************************************************************************
// this function puts the node to sleep with watch dog timer enabled

void WDT_sleep()
{
  Serial.flush(); // empty the send buffer, before continue with; going to sleep
  radio.sleep();
  delay(1);
  
  cli();          // stop interrupts
  MCUSR = 0;
  WDTCSR  = (1<<WDCE | 1<<WDE);     // watchdog change enable
  WDTCSR  = 1<<WDIE | (1<<WDP3) | (0<<WDP2) | (0<<WDP1) | (1<<WDP0); // set  prescaler to 8 second
  sei();  // enable global interrupts

  byte _ADCSRA = ADCSRA;  // save ADC state
  ADCSRA &= ~(1 << ADEN);

  asm("wdr");
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();       

  sleep_enable();  
  sleep_bod_disable();
  sei();       
  sleep_cpu();   
    
  sleep_disable();   
  sei();  

  ADCSRA = _ADCSRA; // restore ADC state (enable ADC)
  delay(1);
}

//*****************************************************************************************************************************

void loop() 
{
  if(state==1)
  {
    sendData();
    PCINT_sleep();
    checkPin();
    sendData();
  }

  while(state==0)
  {
    checkPin(); // read current state of reed switch. 1 = door/window closed, 0 = door/window open.
    WDT_sleep();
  }
}

//*****************************************************************************************************************************
// this function sends data

void sendData()
{
  readSensors();
  
  // send datapacket
  radio.sendWithRetry(ROOM_GATEWAYID, dataPacket, strlen(dataPacket));  // send data
  WDT_sleep();   // sleep 4 seconds before sending data to main gateway
  radio.setNetwork(GATEWAY_NETWORKID);
  radio.send(GATEWAYID, dataPacket, strlen(dataPacket));
  radio.setNetwork(NETWORKID);

  memset(dataPacket, 0, sizeof dataPacket);   // clear array
  blinkLED(LED, 3);
}

//*****************************************************************************************************************************
// This function reads all sensors and creates a datapacket to transmit

void readSensors()
{  
  // read battery level
  float avg=0.0;
  for(int i=0; i<5; i++)
  {
    avg = avg + analogRead(A7);
  }
  float adc_a7 = avg / 5.0;
  float batt = (adc_a7/1023) * 2 * 3.3;
  
  // define character arrays for all variables
  char _d[3];
  char _b[5];
  
  // convert all flaoting point and integer variables into character arrays
  dtostrf(d, 1, 0, _d);  // this function converts float into char array. 3 is minimum width, 2 is decimal precision
  dtostrf(batt, 4, 2, _b);
  delay(1);
  
  dataPacket[0] = 0;  // first value of dataPacket should be a 0
  
  // create datapacket by combining all character arrays into a large character array
  strcat(dataPacket, "d:");
  strcat(dataPacket, _d);
  strcat(dataPacket, ",b:");
  strcat(dataPacket, _b);
  delay(1);
}

//*****************************************************************************************************************************
// this function reads current state of reed switch. 1 = door/window closed, 0 = door/window open

void checkPin()
{
  state = digitalRead(8); 
  // for normally open switch, have to flip the value of pin so door close = 0, open = 1.
  if(state==0)
    d == 1;
  if(state==1)
    d == 0;
}


//*****************************************************************************************************************************
// Fade LED

void fadeLED(int pin)
{
  int brightness = 0;
  int fadeAmount = 5;
  for(int i=0; i<510; i=i+5)  // 255 is max analog value, 255 * 2 = 510
  {
    analogWrite(pin, brightness);  // pin 9 is LED
  
    // change the brightness for next time through the loop:
    brightness = brightness + fadeAmount;  // increment brightness level by 5 each time (0 is lowest, 255 is highest)
  
    // reverse the direction of the fading at the ends of the fade:
    if (brightness <= 0 || brightness >= 255)
    {
      fadeAmount = -fadeAmount;
    }
    // wait for 20-30 milliseconds to see the dimming effect
    delay(10);
  }
  digitalWrite(pin, LOW); // switch LED off at the end of fade
}

//*****************************************************************************************************************************
// blink LED

void blinkLED(int pin, int blinkDelay)
{
  digitalWrite(pin, HIGH);
  delay(blinkDelay);
  digitalWrite(pin, LOW);
}

//*****************************************************************************************************************************
// bruh
