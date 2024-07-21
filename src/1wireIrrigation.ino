#include "Relay.h"
#include "OneWireHub.h"
#include "DS2408.h"
#include "RCSwitch.h"

// v.4 2024.07.20 -> Prüfung des Kommandos 0x5A ob Daten korrekt invertiert wurden (DS2408.cpp)

#define DEBUG                   0
#define TEST_RELAYS             0
#define TEST_RC_SENDER          0

#define NUMBER_OF_DS2408_PINS   8
#define NUMBER_OF_RELAYS        16
#define MAX_RELAYS_ENABLED      5

// Io Pin number, port 13 is reserved by L_LED
Relay relays[NUMBER_OF_RELAYS] = {A5, A4, A3, A2, 12, 11, 10, 9, A1, A6, 3, 4, 5, 6, 7, 8};

uint8_t u8EnabledRelayCounter;

constexpr uint8_t  onewire_pin          { A0 };

constexpr uint8_t  rcSwitch_pin         { 2 };
constexpr uint16_t rcSwitchPulseLength  { 350 };
#define   RC_SWITCH_ON                  "000000FFFF0F" // for Channel A
#define   RC_SWITCH_OFF                 "000000FFFFF0" 

auto hub      = OneWireHub(onewire_pin);
auto ds2408_1 = DS2408( DS2408::family_code, 0x00, 0x00, 0x08, 0x24, 0xDA, 0x00 ); // primary for Relay [0..7]
auto ds2408_2 = DS2408( DS2408::family_code, 0x00, 0x00, 0x09, 0x24, 0xDA, 0x00 ); // sencondary for Relay [8..15]
auto ds2408_3 = DS2408( DS2408::family_code, 0x00, 0x00, 0x0A, 0x24, 0xDA, 0x00 ); // used for enable/disable RcSwitch

RCSwitch rcSender = RCSwitch();

bool      bEnableBlining;
uint8_t   u8BlinkingCounter;
uint8_t   u8CurrentBlinkingCounter;
uint8_t   u8LedState;
uint8_t   u8LedTiming;
uint32_t  u32NextMillis;

void      SetupFinishedBlinking(void);
void      StatemachineBlinking(void);
void      IndicateCmdBlinking(uint8_t u8NewCmd);

void      RcSwitchOff(void);
void      RcSwitchOn(void);

void setup() 
{
#if DEBUG
  Serial.begin(115200);
  Serial.println("setup started.");
#endif

  // Setup Rc Switch
  rcSender.enableTransmit(rcSwitch_pin);
  rcSender.setProtocol(1);
  rcSender.setPulseLength(rcSwitchPulseLength);
  RcSwitchOff();
  
  // Setup OneWire
  for (int j = 0; j < NUMBER_OF_DS2408_PINS; j++)
  {
    ds2408_1.setPinState(j, false);
    ds2408_2.setPinState(j, false);
    ds2408_3.setPinState(j, false);    
  }
 
  hub.attach(ds2408_1);
  hub.attach(ds2408_2);
  hub.attach(ds2408_3);
  
  // Setup Relays
  for (int i = 0; i < NUMBER_OF_RELAYS; i++)
    relays[i].init();

  u8EnabledRelayCounter = 0;

#if DEBUG
  String slaveId_1 = "ds2408_1 Id: 0x" + String(ds2408_1.ID[0], HEX) + " 0x"
                                   + String(ds2408_1.ID[1], HEX) + " 0x"
                                   + String(ds2408_1.ID[2], HEX) + " 0x"
                                   + String(ds2408_1.ID[3], HEX) + " 0x"
                                   + String(ds2408_1.ID[4], HEX) + " 0x"
                                   + String(ds2408_1.ID[5], HEX) + " 0x"
                                   + String(ds2408_1.ID[6], HEX) + " 0x"
                                   + String(ds2408_1.ID[7], HEX);
                                   
  String slaveId_2 = "ds2408_2 Id: 0x" + String(ds2408_2.ID[0], HEX) + " 0x"
                                   + String(ds2408_2.ID[1], HEX) + " 0x"
                                   + String(ds2408_2.ID[2], HEX) + " 0x"
                                   + String(ds2408_2.ID[3], HEX) + " 0x"
                                   + String(ds2408_2.ID[4], HEX) + " 0x"
                                   + String(ds2408_2.ID[5], HEX) + " 0x"
                                   + String(ds2408_2.ID[6], HEX) + " 0x"
                                   + String(ds2408_2.ID[7], HEX);    

  String slaveId_3 = "ds2408_3 Id: 0x" + String(ds2408_3.ID[0], HEX) + " 0x"
                                   + String(ds2408_3.ID[1], HEX) + " 0x"
                                   + String(ds2408_3.ID[2], HEX) + " 0x"
                                   + String(ds2408_3.ID[3], HEX) + " 0x"
                                   + String(ds2408_3.ID[4], HEX) + " 0x"
                                   + String(ds2408_3.ID[5], HEX) + " 0x"
                                   + String(ds2408_3.ID[6], HEX) + " 0x"
                                   + String(ds2408_3.ID[7], HEX);                                                                       
  Serial.println(slaveId_1);
  Serial.println(slaveId_2);  
  Serial.println(slaveId_3);  
#endif

#if TEST_RELAYS
  for (int i = 0; i < NUMBER_OF_RELAYS; i++)
  {
    relays[i].on();
    delay(500);
    relays[i].off(); 
  }
#endif

#if TEST_RC_SENDER
  Serial.println("TEST_RC_SENDER start.");
  delay(rcSwitchPulseLength); // wait until RcSwitchOff() is finished because interrupting is not handled by lib
  RcSwitchOn();
  delay(1000);
  RcSwitchOff();
  Serial.println("TEST_RC_SENDER done.");
#endif

  // L_LED (Red)
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);
  bEnableBlining = false;

  SetupFinishedBlinking();
  
#if DEBUG
  Serial.println("setup finished.");
#endif 
}

void loop() 
{
  // following function must be called periodically
  hub.poll();
  // this part is just for debugging (USE_SERIAL_DEBUG in OneWire.h must be enabled for output)
  if (hub.hasError()) hub.printError();

  // DS2408_1 -> Relay Index 0..7
  // DS2408_2 -> Relay Index 8..15
  // DS2408_3 -> RcSwitch     
  uint8_t u8Ds1LastReceivedCmd = ds2408_1.getLastReceivedCmd();
  uint8_t u8Ds2LastReceivedCmd = ds2408_2.getLastReceivedCmd();
  uint8_t u8Ds3LastReceivedCmd = ds2408_3.getLastReceivedCmd();
   
  if (u8Ds1LastReceivedCmd == 0x5A || u8Ds2LastReceivedCmd == 0x5A)
  {
#if DEBUG
    if (u8Ds1LastReceivedCmd)
    { 
      String sCmd = "ds2408_1 cmd 0x" + String(u8Ds1LastReceivedCmd, HEX);
      Serial.println(sCmd);
    }
    else if (u8Ds2LastReceivedCmd)
    { 
      String sCmd = "ds2408_2 cmd 0x" + String(u8Ds2LastReceivedCmd, HEX);
      Serial.println(sCmd);
    }    
#endif
    uint8_t u8Ds1NewPinState = ds2408_1.getPinState(); 
    uint8_t u8Ds2NewPinState = ds2408_2.getPinState();
    uint8_t u8NumberOfActiveRelays = 0;    
    uint8_t u8PinBit = 1;
    uint8_t u8PinMask1 = 0;
    uint8_t u8PinMask2 = 0;
    
    for (int i = 0; i < NUMBER_OF_DS2408_PINS; i++)
    {
      if ((u8Ds1NewPinState & u8PinBit) && u8NumberOfActiveRelays < MAX_RELAYS_ENABLED)
      {
        u8NumberOfActiveRelays++;
        u8PinMask1 |= u8PinBit;
      }
      else
      {
        relays[i].setRelay(false);        
      }
      if ((u8Ds2NewPinState & u8PinBit) && u8NumberOfActiveRelays < MAX_RELAYS_ENABLED)
      {
        u8NumberOfActiveRelays++;
        u8PinMask2 |= u8PinBit;
      }
      else
      {
        relays[8+i].setRelay(false);        
      }
      u8PinBit <<= 1;
    }

    ds2408_1.setPinState(u8PinMask1);
    ds2408_2.setPinState(u8PinMask2);

    // enable RcSwitch automatically
    if (u8PinMask1 || u8PinMask2)
      RcSwitchOn();

    // enable Relays
    for (int i = 0; i < NUMBER_OF_DS2408_PINS; i++)
    {
      relays[i].setRelay(u8PinMask1 & 0x01);      
      relays[8+i].setRelay(u8PinMask2 & 0x01);      
      u8PinMask1 >>= 1;
      u8PinMask2 >>= 1;
    }   
  }

  // RcSwitch
  if (u8Ds3LastReceivedCmd == 0x5A)
  {
    if (ds2408_3.getPinState(0))
      RcSwitchOn();
    else
    {
      if (ds2408_1.getPinState() == 0 && ds2408_2.getPinState() == 0) // only permitted if all relays are disabled
        RcSwitchOff();
      else
        ds2408_3.setPinState(0, true); // PinState back to true because not permitted
    }
  }
  
  IndicateCmdBlinking(u8Ds1LastReceivedCmd);
  IndicateCmdBlinking(u8Ds2LastReceivedCmd);       
  IndicateCmdBlinking(u8Ds3LastReceivedCmd);  
  StatemachineBlinking();
}

void SetupFinishedBlinking()
{
#if DEBUG
  Serial.println("SetupFinishedBlinking()");
#endif  
  for (uint8_t i = 0; i < 4; i++)
  {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(50);
  }
}

void IndicateCmdBlinking(uint8_t u8NewCmd)
{
  if (bEnableBlining == false) // is finished?
  {
    if (u8NewCmd == 0xF0 || u8NewCmd == 0xF5) // OneWire Read PIO Registers or Channel-Access Read
    {
#if DEBUG
  Serial.println("IndicateCmdBlinking() read");
#endif       
      u8LedTiming = 50;      
      u8BlinkingCounter = 4;
      bEnableBlining = true;
    }
    else if (u8NewCmd == 0x5A) // OneWire Channel-Access Write
    {
#if DEBUG
  Serial.println("IndicateCmdBlinking() write");
#endif             
      u8LedTiming = 200;      
      u8BlinkingCounter = 2;
      bEnableBlining = true;
    }

    if (bEnableBlining == true)
    {
      u32NextMillis = millis() + u8LedTiming; 
      u8LedState = HIGH;
      digitalWrite(LED_BUILTIN, u8LedState);
      u8CurrentBlinkingCounter = 0;
      bEnableBlining = true;    
    }
  }  
}

void StatemachineBlinking(void)
{
  if (bEnableBlining == true)
  {
    if (millis() > u32NextMillis)
    {
      if (u8LedState == HIGH)
        u8LedState = LOW;
      else
        u8LedState = HIGH;
      
      u32NextMillis = millis() + u8LedTiming;
      u8CurrentBlinkingCounter++;
      if (u8CurrentBlinkingCounter > u8BlinkingCounter)
      {
        u8LedState = LOW;
        bEnableBlining = false; // finished
      }
      digitalWrite(LED_BUILTIN, u8LedState);
    }
  }
}

void RcSwitchOn(void)
{
#if DEBUG
  Serial.println("RcSwitchOn()");
#endif  
  rcSender.sendTriState(RC_SWITCH_ON); 
}

void RcSwitchOff(void)
{
#if DEBUG
  Serial.println("RcSwitchOff()");
#endif  
  rcSender.sendTriState(RC_SWITCH_OFF); 
}
