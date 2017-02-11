/**
 * Includes Core Arduino functionality 
 **/
char foo; 
#if ARDUINO < 100
  #include <WProgram.h>
#else
  #include <Arduino.h>
#endif

#define DEBUG 1
#define MTYPE_MEGA 1
#define MTYPE_MICRO 2
#define MODULETYPE MTYPE_MEGA
//#define MODULETYPE MTYPE_MICRO

#if MODULETYPE == MTYPE_MEGA
#define MODULE_MAX_PINS 58
#endif

#if MODULETYPE == MTYPE_MICRO
#define MODULE_MAX_PINS 20
#endif

#define STEPS 64
#define STEPPER_SPEED 200 // 300 already worked, 467, too?
#define STEPPER_ACCEL 100

#define MAX_OUTPUTS     10
#define MAX_BUTTONS     10
#define MAX_LEDSEGMENTS 2
#define MAX_ENCODERS    2
#define MAX_STEPPERS    2
#define MAX_SERVOS      2

#include <EEPROMex.h>
#include <CmdMessenger.h>  // CmdMessenger
#include <LedControl.h>    //  need the library
#include <Button.h>
#include <TicksPerSecond.h>
#include <RotaryEncoderAcelleration.h>
#include <AccelStepper.h>
#include <Servo.h>
#include <MFSegments.h> //  need the library
#include <MFButton.h>
#include <MFEncoder.h>
#include <MFServo.h>
#include <MFOutput.h>

const byte MEM_OFFSET_NAME   = 0;
const byte MEM_LEN_NAME      = 48;
const byte MEM_OFFSET_SERIAL = MEM_OFFSET_NAME + MEM_LEN_NAME + 1;
const byte MEM_LEN_SERIAL    = 11;
const byte MEM_OFFSET_CONFIG = MEM_OFFSET_SERIAL + MEM_LEN_SERIAL + 1;
const int  MEM_LEN_CONFIG    = 768;

#if MODULETYPE == MTYPE_MEGA
char type[20]               = "MobiFlight Mega";
char serial[MEM_LEN_SERIAL] = "SN12345678";
char name[MEM_LEN_NAME]     = "MobiFlight Mega";
int eepromSize = EEPROMSizeMega;
#endif

#if MODULETYPE == MTYPE_MICRO
char type[20]               = "MobiFlight Micro";
char serial[MEM_LEN_SERIAL] = "SN87654321";
char name[MEM_LEN_NAME]     = "MobiFlight Micro";
int eepromSize = EEPROMSizeUno;
#endif

int eepromOffset = 0;

char configBuffer[768] = "";
int configLength = 0;
boolean configActivated = false;

bool powerSavingMode = false;
byte pinsRegistered[MODULE_MAX_PINS];
const unsigned long POWER_SAVING_TIME = 60*15; // in seconds

CmdMessenger cmdMessenger = CmdMessenger(Serial);
unsigned long lastCommand;

MFOutput outputs[MAX_OUTPUTS];
byte outputsRegistered = 0;

MFButton buttons[MAX_BUTTONS];
byte buttonsRegistered = 0;

MFSegments ledSegments[MAX_LEDSEGMENTS];
byte ledSegmentsRegistered = 0;

AccelStepper *steppers[MAX_STEPPERS]; //
byte steppersRegistered = 0;

MFServo servos[MAX_SERVOS];
byte servosRegistered = 0;

MFEncoder encoders[MAX_ENCODERS];
byte encodersRegistered = 0;

enum
{
  kTypeNotSet,      // 0 
  kTypeButton,      // 1
  kTypeEncoder,     // 2
  kTypeOutput,      // 3
  kTypeLedSegment,  // 4
  kTypeStepper,     // 5
  kTypeServo,       // 6
};  

// This is the list of recognized commands. These can be commands that can either be sent or received. 
// In order to receive, attach a callback function to these events
enum
{
  kInitModule,         // 0
  kSetModule,          // 1
  kSetPin,             // 2
  kSetStepper,         // 3
  kSetServo,           // 4
  kStatus,             // 5, Command to report status
  kEncoderChange,      // 6  
  kButtonChange,       // 7
  kStepperChange,      // 8
  kGetInfo,            // 9
  kInfo,               // 10
  kSetConfig,          // 11
  kGetConfig,          // 12
  kResetConfig,        // 13
  kSaveConfig,         // 14
  kConfigSaved,        // 15
  kActivateConfig,     // 16
  kConfigActivated,    // 17
  kSetPowerSavingMode, // 18  
};

// Callbacks define on which received commands we take action
void attachCommandCallbacks()
{
  // Attach callback methods
  cmdMessenger.attach(OnUnknownCommand);
  cmdMessenger.attach(kInitModule, OnInitModule);
  cmdMessenger.attach(kSetModule, OnSetModule);
  cmdMessenger.attach(kSetPin, OnSetPin);
  cmdMessenger.attach(kSetStepper, OnSetStepper);
  cmdMessenger.attach(kSetServo, OnSetServo);  
  cmdMessenger.attach(kGetInfo, OnGetInfo);
  cmdMessenger.attach(kGetConfig, OnGetConfig);
  cmdMessenger.attach(kSetConfig, OnSetConfig);
  cmdMessenger.attach(kResetConfig, OnResetConfig);
  cmdMessenger.attach(kSaveConfig, OnSaveConfig);
  cmdMessenger.attach(kActivateConfig, OnActivateConfig);  
#ifdef DEBUG  
  cmdMessenger.sendCmd(kStatus,"Attached callbacks");
#endif  
}

// Setup function
void setup() 
{
  EEPROM.setMaxAllowedWrites(1000);
  EEPROM.setMemPool(0, eepromSize);
  
  configBuffer[0]='\0';  
  generateSerial(); 
  Serial.begin(115200);  
  clearRegisteredPins();
  cmdMessenger.printLfCr();   
  attachCommandCallbacks();
  lastCommand = millis();
  loadConfig();
  cmdMessenger.sendCmd(kStatus, "MFModule has started!");
}

void generateSerial() 
{
  int bytes = EEPROM.readBlock<char>(MEM_OFFSET_SERIAL, serial, MEM_LEN_SERIAL); 
  if (serial[0]=='S'&&serial[1]=='N') return;
  randomSeed(analogRead(0));
  sprintf(serial,"SN-%03x-",random(4095));
  sprintf(&serial[7],"%03x",random(4095));
  EEPROM.writeBlock<char>(MEM_OFFSET_SERIAL, serial, MEM_LEN_SERIAL);
}

void loadConfig()
{
  int bytes = EEPROM.readBlock<char>(MEM_OFFSET_CONFIG, configBuffer, MEM_LEN_CONFIG);  
  cmdMessenger.sendCmd(kStatus, "Restored config");
  cmdMessenger.sendCmd(kStatus, configBuffer);  
  configLength = 0;
  for(configLength=0;configLength!=MEM_LEN_CONFIG;configLength++) {
    if (configBuffer[configLength]!='\0') continue;
    break;
  }
  readConfig(configBuffer);
  activateConfig();
}

void storeConfig() 
{
  cmdMessenger.sendCmd(kStatus, "Storing config");
  EEPROM.writeBlock<char>(MEM_OFFSET_CONFIG, configBuffer, MEM_LEN_CONFIG);
}

void SetPowerSavingMode(bool state) 
{
  // disable the lights ;)
  powerSavingMode = state;
  PowerSaveLedSegment(state);
#ifdef DEBUG  
  if (state)
    cmdMessenger.sendCmd(kStatus, "PwrSave On");
  else
    cmdMessenger.sendCmd(kStatus, "PwrSave Off");    
#endif
  //PowerSaveOutputs(state);
}

// Loop function
void loop() 
{  
  cmdMessenger.feedinSerialData();
  #if DEBUG == 2
  cmdMessenger.sendCmdStart(kStatus);
  cmdMessenger.sendCmdArg(millis());
  cmdMessenger.sendCmdArg(lastCommand);
  cmdMessenger.sendCmdArg(millis()-lastCommand);
  cmdMessenger.sendCmdArg(POWER_SAVING_TIME * 1000);
  cmdMessenger.sendCmdEnd();
  #endif   
  if (!powerSavingMode && ((millis() - lastCommand) > (POWER_SAVING_TIME * 1000))) {
    // enable power saving
    SetPowerSavingMode(true);
  } else if (powerSavingMode && ((millis() - lastCommand) < (POWER_SAVING_TIME * 1000))) {
    // disable power saving
    SetPowerSavingMode(false);
  }
  
  // if config has been reset
  // and still is not activated
  // do not perform updates
  // to prevent mangling input for config (shared buffers)
  if (!configActivated) return;
  
  // Process incoming serial data, and perform callbacks  
  readButtons();
  readEncoder();
  // segments do not need update
  updateSteppers();  
  // servos do not need update
}

bool isPinRegistered(byte pin) {
  return pinsRegistered[pin] != kTypeNotSet;
}

bool isPinRegisteredForType(byte pin, byte type) {
  return pinsRegistered[pin] == type;
}

bool registerPin(byte pin, byte type) {
  pinsRegistered[pin] = type;
}

bool clearRegisteredPins(byte type) {
  for(int i=0; i!=MODULE_MAX_PINS;++i)
    if (pinsRegistered[i] == type)
      pinsRegistered[i] = kTypeNotSet;
}

bool clearRegisteredPins() {
  for(int i=0; i!=MODULE_MAX_PINS;++i)
    pinsRegistered[i] = kTypeNotSet;
}

//// OUTPUT /////
void AddOutput(uint8_t pin = 1, String name = "Output")
{
  if (outputsRegistered == MAX_OUTPUTS) return;
  if (isPinRegistered(pin)) return;
  
  outputs[outputsRegistered] = MFOutput(pin);
  registerPin(pin, kTypeOutput);
  outputsRegistered++;
#ifdef DEBUG  
  //cmdMessenger.sendCmd(kStatus, "Added output " + name);
#endif  
}

void ClearOutputs() 
{
  clearRegisteredPins(kTypeOutput);
  outputsRegistered = 0;
#ifdef DEBUG  
  cmdMessenger.sendCmd(kStatus,"Cleared outputs");
#endif  
}

//// BUTTONS /////
void AddButton(uint8_t pin = 1, String name = "Button")
{  
  if (buttonsRegistered == MAX_BUTTONS) return;
  
  if (isPinRegistered(pin)) return;
  
  buttons[buttonsRegistered] = MFButton(pin, name);
  buttons[buttonsRegistered].attachHandler(btnOnRelease, handlerOnRelease);
  buttons[buttonsRegistered].attachHandler(btnOnPress, handlerOnRelease);

  registerPin(pin, kTypeButton);
  buttonsRegistered++;
#ifdef DEBUG  
  //cmdMessenger.sendCmd(kStatus, "Added button " + name);
#endif
}

void ClearButtons() 
{
  clearRegisteredPins(kTypeButton);
  buttonsRegistered = 0;
#ifdef DEBUG  
  cmdMessenger.sendCmd(kStatus,"Cleared buttons");
#endif  
}

//// ENCODERS /////
void AddEncoder(uint8_t pin1 = 1, uint8_t pin2 = 2, String name = "Encoder")
{  
  if (encodersRegistered == MAX_ENCODERS) return;
  if (isPinRegistered(pin1) || isPinRegistered(pin2)) return;
  
  encoders[encodersRegistered] = MFEncoder();
  encoders[encodersRegistered].attach(pin1, pin2, name);
  encoders[encodersRegistered].attachHandler(encLeft, handlerOnRelease);
  encoders[encodersRegistered].attachHandler(encLeftFast, handlerOnRelease);
  encoders[encodersRegistered].attachHandler(encRight, handlerOnRelease);
  encoders[encodersRegistered].attachHandler(encRightFast, handlerOnRelease);

  registerPin(pin1, kTypeEncoder); registerPin(pin2, kTypeEncoder);    
  encodersRegistered++;
#ifdef DEBUG  
  //cmdMessenger.sendCmd(kStatus,"Added encoder");
#endif
}

void ClearEncoders() 
{
  clearRegisteredPins(kTypeEncoder);
  encodersRegistered = 0;
#ifdef DEBUG  
  cmdMessenger.sendCmd(kStatus,"Cleared encoders");
#endif  
}

//// OUTPUTS /////

//// SEGMENTS /////
void AddLedSegment(int dataPin, int clkPin, int csPin, int numDevices, int brightness)
{
  if (ledSegmentsRegistered == MAX_LEDSEGMENTS) return;
  
  if (isPinRegistered(dataPin) || isPinRegistered(clkPin) || isPinRegistered(csPin)) return;
  
  ledSegments[ledSegmentsRegistered].attach(dataPin,clkPin,csPin,numDevices,brightness); // lc is our object
  //ledSegments[ledSegmentsRegistered].test();
  
  registerPin(dataPin, kTypeLedSegment);
  registerPin(clkPin, kTypeLedSegment);
  registerPin(csPin, kTypeLedSegment);  
  ledSegmentsRegistered++;
#ifdef DEBUG  
  //cmdMessenger.sendCmd(kStatus,"Added Led Segment");
#endif  
}

void ClearLedSegments()
{
  clearRegisteredPins(kTypeLedSegment);
  for (int i=0; i!=ledSegmentsRegistered; i++) {
    ledSegments[ledSegmentsRegistered].detach();
  }
  ledSegmentsRegistered = 0;
#ifdef DEBUG  
  cmdMessenger.sendCmd(kStatus,"Cleared segments");
#endif  
}

void PowerSaveLedSegment(bool state)
{
  for (int i=0; i!= ledSegmentsRegistered; ++i) {
    ledSegments[i].powerSavingMode(state);
  }
  
  for (int i=0; i!= outputsRegistered; ++i) {
    outputs[i].powerSavingMode(state);
  }
}

//// STEPPER ////
void AddStepper(int pin1, int pin2, int pin3, int pin4)
{
  if (steppersRegistered == MAX_STEPPERS) return;
  if (isPinRegistered(pin1) || isPinRegistered(pin2) || isPinRegistered(pin3) || isPinRegistered(pin4)) return;
  
  steppers[steppersRegistered] = new AccelStepper(AccelStepper::FULL4WIRE, pin4, pin2, pin1, pin3); // lc is our object 
  steppers[steppersRegistered]->setSpeed(STEPPER_SPEED);
  steppers[steppersRegistered]->setAcceleration(STEPPER_ACCEL);
  registerPin(pin1, kTypeStepper); registerPin(pin2, kTypeStepper); registerPin(pin3, kTypeStepper); registerPin(pin4, kTypeStepper);       
  steppersRegistered++;
  
#ifdef DEBUG  
  //cmdMessenger.sendCmd(kStatus,"Added stepper");
#endif 
}

void ClearSteppers()
{
  for (int i=0; i!=steppersRegistered; i++) 
  {
    delete steppers[steppersRegistered];
  }  
  clearRegisteredPins(kTypeStepper);
  steppersRegistered = 0;
#ifdef DEBUG  
  cmdMessenger.sendCmd(kStatus,"Cleared steppers");
#endif 
}

//// SERVOS /////
void AddServo(int pin)
{
  if (servosRegistered == MAX_SERVOS) return;
  if (isPinRegistered(pin)) return;
  
  servos[servosRegistered] = MFServo(pin, true);
  registerPin(pin, kTypeServo);
  servosRegistered++;
}

void ClearServos()
{
  clearRegisteredPins(kTypeServo);
  servosRegistered = 0;
#ifdef DEBUG  
  cmdMessenger.sendCmd(kStatus,"Cleared servos");
#endif 
}


//// EVENT HANDLER /////
void handlerOnRelease(byte eventId, uint8_t pin, String name)
{
  cmdMessenger.sendCmdStart(kButtonChange);
  cmdMessenger.sendCmdArg(name);
  cmdMessenger.sendCmdArg(eventId);
  cmdMessenger.sendCmdEnd();
};

/**
 ** config stuff
 **/
void OnSetConfig() 
{
#ifdef DEBUG  
  cmdMessenger.sendCmd(kStatus,"Setting config start");
#endif    
  lastCommand = millis();
  String cfg = cmdMessenger.readStringArg();
  int bufferSize = MEM_LEN_CONFIG - configLength;
  cfg.toCharArray(&configBuffer[configLength], bufferSize);
  configLength += cfg.length();
  readConfig(cfg);
  cmdMessenger.sendCmd(kStatus,"Setting config end");
#ifdef DEBUG
  cmdMessenger.sendCmd(kStatus,"Setting config end");
#endif    
}

void resetConfig()
{
  ClearButtons();
  ClearOutputs();
  ClearLedSegments();
  ClearServos();
  ClearSteppers();
  for(int i=0;i!=MEM_LEN_CONFIG;i++) {
    configBuffer[i]='\0';
  }
  configLength = 0;
  configActivated = false;
}

void OnResetConfig()
{
  resetConfig();
}

void OnSaveConfig()
{
  storeConfig();
  cmdMessenger.sendCmd(kConfigSaved, "OK");
}

void OnActivateConfig() 
{
  activateConfig();
  cmdMessenger.sendCmd(kConfigActivated, "OK");
}

void activateConfig() {
  configActivated = true;
}

void readConfig(String cfg) {
  char bCfg[MEM_LEN_CONFIG+1];
  char *p = NULL;
  cfg.toCharArray(bCfg, MEM_LEN_CONFIG);

  bool hasNext = false;
  char *command = strtok_r(bCfg, ".", &p);
  char *params[5];
  if (*command == NULL) return;

  do {    
    switch (atoi(command)) {
      case kTypeButton:
        params[0] = strtok_r(NULL, ".", &p); // pin
        params[1] = strtok_r(NULL, ":", &p); // name
        AddButton(atoi(params[0]), params[1]);
      break;
      
      case kTypeOutput:
        params[0] = strtok_r(NULL, ".", &p); // pin
        params[1] = strtok_r(NULL, ":", &p); // Name
        AddOutput(atoi(params[0]), params[1]);
      break;
      
      case kTypeLedSegment:       
        params[0] = strtok_r(NULL, ".", &p); // pin Data
        params[1] = strtok_r(NULL, ".", &p); // pin Clk
        params[2] = strtok_r(NULL, ".", &p); // pin Cs
        params[3] = strtok_r(NULL, ".", &p); // numModules
        params[4] = strtok_r(NULL, ".", &p); // brightness
        params[5] = strtok_r(NULL, ":", &p); // Name
        // int dataPin, int clkPin, int csPin, int numDevices, int brightness
        AddLedSegment(atoi(params[0]), atoi(params[1]), atoi(params[2]), atoi(params[3]), atoi(params[4]));
      break;
      
      case kTypeStepper:
        // AddStepper(int pin1, int pin2, int pin3, int pin4)
        params[0] = strtok_r(NULL, ".", &p); // pin1
        params[1] = strtok_r(NULL, ".", &p); // pin2
        params[2] = strtok_r(NULL, ".", &p); // pin3
        params[3] = strtok_r(NULL, ".", &p); // pin4
        params[4] = strtok_r(NULL, ":", &p); // Name
        AddStepper(atoi(params[0]), atoi(params[1]), atoi(params[2]), atoi(params[3]));
      break;
      
      case kTypeServo:
        // AddServo(int pin)
        params[0] = strtok_r(NULL, ".", &p); // pin1
        params[0] = strtok_r(NULL, ":", &p); // Name
        AddServo(atoi(params[0]));
      break;
      
      case kTypeEncoder:
        // AddEncoder(uint8_t pin1 = 1, uint8_t pin2 = 2, String name = "Encoder")
        params[0] = strtok_r(NULL, ".", &p); // pin1
        params[1] = strtok_r(NULL, ".", &p); // pin2
        params[2] = strtok_r(NULL, ".", &p); // pin3
        params[3] = strtok_r(NULL, ".", &p); // pin4
        params[4] = strtok_r(NULL, ":", &p); // Name
        AddStepper(atoi(params[0]), atoi(params[1]), atoi(params[2]), atoi(params[3]));
      break;
        
      default:
        // read to the end of the current command which is
        // apparently not understood
        params[0] = strtok_r(NULL, ":", &p); // read to end of unknown command
    }    
    command = strtok_r(NULL, ".", &p);  
  } while (command!=NULL);
}

// Called when a received command has no attached function
void OnUnknownCommand()
{
  lastCommand = millis();
  cmdMessenger.sendCmd(kStatus,"Command without attached callback");
}

void OnGetInfo() {
  lastCommand = millis();
  cmdMessenger.sendCmdStart(kInfo);
  cmdMessenger.sendCmdArg(type);
  cmdMessenger.sendCmdArg(name);
  cmdMessenger.sendCmdArg(serial);
  cmdMessenger.sendCmdArg(configBuffer);
  cmdMessenger.sendCmdEnd();
//  cmdMessenger.sendCmd(kInfo, type + "," + name);
}

void OnGetConfig() 
{
  cmdMessenger.sendCmdStart(kInfo);
  cmdMessenger.sendCmdArg(configBuffer);
  cmdMessenger.sendCmdEnd();
}

// Callback function that sets led on or off
void OnSetPin()
{
  // Read led state argument, interpret string as boolean
  int pin = cmdMessenger.readIntArg();
  int state = cmdMessenger.readIntArg();
  // Set led
  digitalWrite(pin, state > 0 ? HIGH : LOW);
  lastCommand = millis();
}

void OnInitModule()
{
  int module = cmdMessenger.readIntArg();
  int subModule = cmdMessenger.readIntArg();  
  int brightness = cmdMessenger.readIntArg();
  ledSegments[module].setBrightness(subModule,brightness);
  lastCommand = millis();
}

void OnSetModule()
{
  int module = cmdMessenger.readIntArg();
  int subModule = cmdMessenger.readIntArg();  
  char * value = cmdMessenger.readStringArg();
#ifdef DEBUG  
  cmdMessenger.sendCmdStart(kStatus);
  cmdMessenger.sendCmdArg(module);
  cmdMessenger.sendCmdArg(subModule);
  cmdMessenger.sendCmdArg(value);
  cmdMessenger.sendCmdEnd();
#endif    
  ledSegments[module].display(subModule, value);
  lastCommand = millis();
}

void OnSetStepper()
{
  int stepper    = cmdMessenger.readIntArg();
  int newPos     = cmdMessenger.readIntArg();
  if (stepper >= steppersRegistered) return;
  steppers[steppersRegistered]->moveTo(newPos);
  lastCommand = millis();
}

void OnSetServo()
{ 
  int servo    = cmdMessenger.readIntArg();
  int newValue = cmdMessenger.readIntArg();
  if (servo >= servosRegistered) return;
  servos[servo].moveTo(newValue);  
  lastCommand = millis();
}

void readButtons()
{
  for(int i=0; i!=buttonsRegistered; i++) {
    buttons[i].update();
  }  
}

void updateSteppers()
{
  for (int i=0; i!=steppersRegistered; i++) {
    steppers[i]->run();
  }
}
  
void readEncoder() 
{
  for(int i=0; i!=encodersRegistered; i++) {
    encoders[i].update();
  }
}


