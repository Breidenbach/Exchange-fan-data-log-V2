 /**************************************************************************/
/*

This program controls operation of the air exchange fan, and logs data 
from four temperature sensors as well as the switches on the sliding
deck door and windows in the bedroom and office.  An additional input
to controlling the fan which is also logged is the furnace control.

The harware uses the Adafruit MCP9808 temperature sensors (4), an adafruit Dataloging
board with an SD card drive and real time clock and relay module.

Pins SCL & SDA are used for addressing the temp sensors as well as the Real Time Clock
Pins 10, 11, 12, and 13 are used for communicating between the Arduino
and the data logger SD drive.

The computer is an Arduino 101 with an adafruit Datalogging board to write 
to an SD card and containing a real time clock.

Data is logged to the SD card in csv format which can be read by Excel.
Logging is done at intervals set by the constant interval (in seconds).
The format of the data is:
  Date number (number of days since 1/2/1904, the base used by Mac Excel)
  Time of day (number of seconds since midnight, which must be converted 
    in Excel to a fraction of the day by dividing by 86400)
  Temperature from sensor 1 (Outside air)
  Temperature from sensor 2 (Outside air after exchange)
  Temperature from sensor 3 (Inside air)
  Temperature from sensor 4 (Inside air after exchange)
  % of time movingAvg
      desired %
      actual %
      adjustment enabled
      run mode state
  Status of Bedroom windows (0 = open, 1 = closed)
  Status of Office windows
  Status of Sliding Deck Door
  Status of Furnace signal
  Status of Air Exchange Fan (1 = running, 0 = not running)

The exchange fan is set to running if requested by the furnace and
all windows and the door are closed, except that the door signal is 
delayed to avoid turning the fan on and off excessively when the door
is used for a quick entry or exit from the house.  The delay is set by
constant doorDelay (in seconds).

The time running percent may be adjusted by the computer, and there are
buttons for setting the requested percent, reporting the current
percent, toggling enablement of adjustment.  The percentage calculated
time running per on/off cycle.

File usage:
Log files are put into a directory with the date the log file was created:
D170205 would be created on February 5, 2017
The actual log file is named with the time of creation:
L134423 would be created at 13:44:23
On the SD card, the file would be found as D170205/L134423

Two other files are used to keep data when the device is powered down.
This allows for continuity when power outages occur.

STATFILE.txt contains the name of the latest log file, so that logging can
continue in the same file if there is a power failure.
Byte 1:  1 or 0 indicating if data logging was enabled
Bytes 2-17:  file directory and name

PCTFILE.txt contains the requested run percentage.  This file only
exists if an adjusted percent has been enabled, otherwise the file
does not exist.

Update log:

2.0  1/16/2017
Converted to use Arduino 101, adafruit Datalogger shield
Corrected logic errors, especially in the state machine
Improved code structure

1.2  9/9/2016
Change defines for runState to enum
Insert define to compile out percent adjustment.
Convert to Andee101

1.1  8/29/2016
Correct output string length
Correct clock setting to use iPad values
Add F macros to test print strings.
Correct printing ratios for debug
Remove temperature adjustments

*/
/**************************************************************************/

//#define outputSerial  //  to produce serial port output for debugging
#define PERCENT_ADJ

#include <SPI.h>
#include <Wire.h>
#include "RTClib.h"
#include "Adafruit_MCP9808.h"
#include <SD.h>
#include <CurieBLE.h>
#include <Andee101.h>

File logFile;

const char statFileName[13] = "STATFILE.txt";
const char pctFileName[12] = "PCTFILE.txt";

//  Calibration values

#define ratioFloatInit 0.33
#define percentInit 33
const unsigned long interval = 300; // 300 for 5 min logging interval
const unsigned long doorDelay = 120;   //  120 sec delay
const int maxN = 64;
const int defaultPercent = percentInit;

// Pin definitions

const byte      bedroomSensor = 2;
const byte      officeSensor = 3;
const byte      kitchenSensor = 4;
const byte      furnaceSensor = 5;
const byte      bedroomIndicator = 6;
const byte      officeIndicator = 7;
const byte      furnaceIndicator = 8;
const byte      kitchenIndicator = 9;
const byte      relayControlPin = A0;  // relay for fan control

//  Display definitions

Andee101Helper tempDisplay; // Temp in deg F
const char tempDisplayTitle[56] = "Outside   Outside Adj   Exhaust   Exhaust Adj";

Andee101Helper clockDisplay;  // RTC display
const char clockDisplayTitle[11] = "Time Stamp";

Andee101Helper logStats;   //  logging count
const char logStatsTitle[15] = "Logging Status";

Andee101Helper btnToggleLog; // turn logging on and off
const char btnToggleLogTitleOn[16] = "Turn ON logging";
const char btnToggleLogTitleOff[17] = "Turn OFF logging";

Andee101Helper switchDisplay; // sensors and running
const char switchDisplayTitle[50] = "Bedroom Office  Kitchen Furnace Running";

Andee101Helper btnSetClock;  // set real time clock
const char btnSetClockTitle[15] = "Enter the Time";

Andee101Helper btnSetDate;  // set real time clock
const char btnSetDateTitle[15] = "Enter the Date";

Andee101Helper blowerPercent;  // current actual 
const char blowerPercentTitle[15] = "Blower Percent";

#ifdef PERCENT_ADJ
Andee101Helper sliderAdjustPercent; // adjust desired percent
const char sliderAdjustPercentTitle[18] = "Requested Percent";

Andee101Helper btnEnableAdjustment;  // enable/disable percent adjustment
const char btnEnableAdjustmentTitle[26] = "Enable PercentAdjustment"; 
const char btnDisableAdjustmentTitle[27] = "Disable PercentAdjustment"; 
#endif

// Create the MCP9808 temperature sensor object

Adafruit_MCP9808 tempsensor[4] = Adafruit_MCP9808();
const int sensor_addr = 0x18;  // I2C address, app uses 0x18 through 0x1B
/*  
 *   Sensor locations:
 *       Top left        0   Outsd
 *       Bottom right    1   OutsdAdj
 *       Bottom left     2   Exh
 *       Top right       3   ExhAdj
 */

// Create RealTimeClock object

RTC_DS1307 rtc;  // I2C address is 0x68

// and data format area
char     timeStamp[12] = "not set";
char     todayDate[12];
char     logStatus[35];
char     bedroomDisplayStatus[7];
char     officeDisplayStatus[7];
char     kitchenDisplayStatus[8];
char     furnaceDisplayStatus[7];
char     operatingDisplayStatus[8];

unsigned long logCount = 0;  // number of log entries written

bool      logData = false;
bool      AndeeFlag = false;
bool      firstTime = true;
bool      adjustmentEnabled = false;

// time and date variables

unsigned long currentTime;
unsigned long loggedTime = 0;
unsigned long currentDay;
unsigned long doorComplete = 0;  // time when door delay will complete

 // Switch status
 
bool      SSbedroom = false; 
bool      SSoffice = false;
bool      SSkitchen = false;
bool      SSfurnace = false;
bool      SSrunning = false;
int       bedroomSensorInput = LOW;
int       officeSensorInput = LOW;
int       kitchenSensorInput = LOW;
int       furnaceSensorInput = LOW;

// Variables necessary for SD card read/write

char logFileName[20];
char logDirName[8];
word  filenumber = 0;
unsigned long  offset = 0;
int logOffset;
char outputString[60];
const char CSV_headers[108] = "Date,Time,Outsd,OutsdAdj,Exh,ExhAdj,movingAvg,N, desired,actual,adj enab,run enab,Bed,Off,Kit,Furn,Run\n";


// To store the temperature reading

float degF[4];
char strTemp0[12];
byte ndx;

// Variables for percent operation of fan, always stored as ratio but 
//     displayed as percent

float   actualRatio = ratioFloatInit;
float   movingAverage = ratioFloatInit;
float   desiredRatio = percentInit;
int     desiredPercent = percentInit;
int     priorDsrdPercent = 0;
int     averageCount = 0;

enum     runState_t {notRunning = 0, Running = 1, RunningAdjusting = 2, notRunningAdjusting = 3};
runState_t runState = notRunning;
unsigned long onPeriodStart;
unsigned long totPeriodStart;

// functions and prototypes

#define btoa(x) ((x)?"true":"false")
void adjustClock( void );
void adjustDate( void );
void  savePercent(bool adj, int pct );
bool retrievePercent( int* pct );
void  saveLogStatus( bool stat, char* dirName, char* fileName  );
bool  retrieveLogStatus( char* dirName, char* fileName );
float calcPeriodRatio ( unsigned long currentT, unsigned long onT, unsigned long periodT );
float calcMovingAverage (float mA, float Xi, int & count);
bool  writeHeadersToFile( char* dirName, char* fileName );
void  constructLogFileName( char* dirName, char* fileName );
void  doRunState( void );
void  doNotRunningEntryActions( void );
void  doRunningEntryActions( void );
void  doNotRunningAdjustingEntryActions( void );

//                          setup

void setup() {
  #ifdef outputSerial
  Serial.begin(9600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for Leonardo only, but handy to see starting conditions
  }
  Serial.println(F ("starting sketch"));
  #endif
  
  Andee101.begin(); // Sets up the communication link between Arduino and the Annikken Andee

  pinMode(bedroomSensor, INPUT);  // declare bedroomSensor Pin as input
  pinMode(bedroomIndicator, OUTPUT);  // declare bedroomIndicator pin as output
  pinMode(officeSensor, INPUT);   // declare officeSensor Pin as input
  pinMode(kitchenSensor, INPUT);  // declare kitchenSensor Pin as input
  pinMode(furnaceSensor, INPUT);  // declare furnaceSensor Pin as input
  pinMode(relayControlPin, OUTPUT); // Configures relayControlPin for output.
  
  rtc.begin(); // connect to RealTimeClock
    
  // check clock
  if ( ! rtc.isrunning()) {
    clockDisplay.setData((char*)"no clock");
    clockDisplay.update();
    #ifdef outputSerial
    Serial.println(F ("no clock"));
    #endif
  }

  if (!SD.begin(10)) {
    #ifdef outputSerial
    Serial.println(F("SD card initialization failed!"));
    #endif
    return;
  }
  #ifdef outputSerial
  Serial.println(F("SD card initialization done."));
  #endif

  #ifdef PERCENT_ADJ
  adjustmentEnabled = retrievePercent( &desiredPercent );
  #else
  desiredPercent = percentInit;
  adjustmentEnabled = false;
  #endif
  desiredRatio =  desiredPercent / 100.0;
  logData = retrieveLogStatus( logDirName, logFileName );
  #ifdef outputSerial
    sprintf (outputString, "after retrieveLogStatus %s, %s", btoa(logData), logFileName);
    Serial.println(outputString);
    sprintf (outputString, "  retrieved percent %d", desiredPercent);
    Serial.println(outputString);
  #endif
  if (logData) {
//    logData = writeHeadersToFile( logDirName, logFileName );
    btnToggleLog.setColor(RED);
    btnToggleLog.setTitle(btnToggleLogTitleOff);
    #ifdef outputSerial
    Serial.println(F("Logging is --- ON ---"));
    #endif
  } else {
    btnToggleLog.setColor(GREEN);
    btnToggleLog.setTitle(btnToggleLogTitleOn);
    #ifdef outputSerial
    Serial.println(F("Logging is *** OFF ***"));
    #endif
  }
  btnToggleLog.update();
  #ifdef outputSerial
    sprintf (outputString, "log stat %d : file %s", logData, logFileName);
    Serial.println(outputString);
  #endif
  
  // set initial run state
  if ((digitalRead(bedroomSensor) == LOW) &&
       (digitalRead(officeSensor) == LOW) && 
  	 	 (digitalRead(kitchenSensor) == LOW) &&
  		  (digitalRead(furnaceSensor) == LOW))  {
    #ifdef outputSerial
    Serial.println(F("setting intial to RUN"));
    #endif
  	runState = Running;
    doRunningEntryActions();
  } else {
    runState = notRunning;
    doNotRunningEntryActions();
  }
    #ifdef outputSerial
    Serial.println(F("End of setup"));
    #endif
    
  if (Andee101.isConnected() == 1) AndeeFlag = 1;
  initializeAndeeDisplays();
  
}  // end of setup()

// This is the function meant to define the types and the apperance of
// all the objects on your smartphone or tablet

void initializeAndeeDisplays() {
  #ifdef outputSerial
  Serial.println(F ("     initializeAndeeDisplays"));
  #endif
  
  Andee101.clear(); // Clear the screen of any previous displays

  #ifdef PERCENT_ADJ
  btnEnableAdjustment.setId(0);
  btnEnableAdjustment.setType(BUTTON_IN);
  btnEnableAdjustment.setCoord(67,75,33,24); // Row 4, middle
  if (adjustmentEnabled) {
    btnEnableAdjustment.setTitle(btnDisableAdjustmentTitle);
    btnEnableAdjustment.setColor(RED);
  } else {
    btnEnableAdjustment.setTitle(btnEnableAdjustmentTitle);
    btnEnableAdjustment.setColor(GREEN);
  }
  btnEnableAdjustment.update();

  sliderAdjustPercent.setId(1);
  sliderAdjustPercent.setType(SLIDER_IN);
  sliderAdjustPercent.setSubType(0);
  sliderAdjustPercent.setInputMode(ON_VALUE_CHANGE);
  sliderAdjustPercent.setCoord(33.5,75,33,24); // Row 4, Left
  sliderAdjustPercent.setTitle(sliderAdjustPercentTitle);
  sliderAdjustPercent.setColor(GREEN);
  sliderAdjustPercent.setSliderMinMax(0, 100);
  sliderAdjustPercent.setSliderInitialValue(desiredPercent);
  sliderAdjustPercent.setData(desiredPercent);
  sliderAdjustPercent.setSliderNumIntervals(100);
  sliderAdjustPercent.update();
  #endif
  
  blowerPercent.setId(2);
  blowerPercent.setType(DATA_OUT);
  blowerPercent.setCoord(0,75,33,24); // Row 4, right;
  blowerPercent.setTitle(blowerPercentTitle);
  blowerPercent.setData((char*)"");
  sprintf (outputString, "Moving Average, N=%d", maxN);
  blowerPercent.setUnit((char*)outputString);
  blowerPercent.setColor(LTGRAY);  //  data background color
  blowerPercent.update();

  logStats.setId(3);
  logStats.setType(DATA_OUT);
  logStats.setCoord(50,25,24,24); // Row 2, middle
  logStats.setTitle(logStatsTitle);
  logStats.setData(logCount);
  if (logData) {
    logStats.setUnit("Logging");
    logStats.setColor(LIGHT_GREEN);
  } else {
    logStats.setUnit("Logging OFF");
    logStats.setColor(MISTY_ROSE);
  }
  logStats.update();
  
  btnToggleLog.setId(4);
  btnToggleLog.setType(BUTTON_IN);
  btnToggleLog.setCoord(75,25,24,24); // Row 2, right
  if (logData) {
    btnToggleLog.setTitle(btnToggleLogTitleOff);
    btnToggleLog.setColor(RED);
  }
  else {
    btnToggleLog.setTitle(btnToggleLogTitleOn);
    btnToggleLog.setColor(GREEN);
  }
  btnToggleLog.update();
  
  clockDisplay.setId(5);
  clockDisplay.setType(DATA_OUT);
  clockDisplay.setCoord(0,0,49,24); // Row 1, Left
  clockDisplay.setTitle(clockDisplayTitle);
  clockDisplay.setData((char*)"");
  clockDisplay.setColor(LTGRAY);  //  data background color
  clockDisplay.setUnit((char*)"");
  clockDisplay.update();

  btnSetClock.setId(6);
  btnSetClock.setType(TIME_IN);
  btnSetClock.setCoord(50,0,24,24); // Row 1, middle
  btnSetClock.setTitle(btnSetClockTitle);
  btnSetClock.setColor(BLUE);
  btnSetClock.update();
 
  btnSetDate.setId(7);  // Each object must have a unique ID number
  btnSetDate.setType(DATE_IN);  // This defines your object as a Date Input Button
  btnSetDate.setCoord(75,0,24,24); // Row 1, right
  btnSetDate.setTitle(btnSetDateTitle);
  btnSetDate.setColor(BLUE); 
  btnSetDate.update();

  switchDisplay.setId(8);
  switchDisplay.setType(DATA_OUT);
  switchDisplay.setCoord(0,25,49,24); // Row 2, Left
  switchDisplay.setTitle(switchDisplayTitle);
  switchDisplay.setData((char*)"");
  switchDisplay.setColor(LTGRAY);  //  data background color
  switchDisplay.setUnit((char*)"Switch Status");
  switchDisplay.update();
  
  tempDisplay.setId(9);  // Each object must have a unique ID number
  tempDisplay.setType(DATA_OUT);  // This defines your object as a display box
  tempDisplay.setCoord(0,50,49,24); // Row 3
  tempDisplay.setTitle(tempDisplayTitle);
  tempDisplay.setColor(LTGRAY);  //  data background color
  tempDisplay.setData((char*)""); // We'll update it with new analog data later.
  tempDisplay.setUnit((char*)"deg F");  
  tempDisplay.update();  
  for (ndx = 0; ndx < 4; ndx++) {
    tempsensor[ndx].begin(sensor_addr + ndx); // required to get clock working
        // function of I2C communication?

  }
}

//                     loop

void loop() {
  #ifdef outputSerial
    delay(3000);
    sprintf(outputString, " loop  log data = %s", btoa(logData));
    Serial.println(outputString);
  #endif
  if (Andee101.isConnected()) {
    if (! AndeeFlag) {
      initializeAndeeDisplays();
      AndeeFlag = 1; //this flag is to signal that Andee has already connected and the function setup() has run once.
      #ifdef outputSerial
      Serial.println(F ("Andee connected"));
      #endif
    }
  } else {
    if (! Andee101.isConnected()) {
      if (AndeeFlag) {
         AndeeFlag = 0;
         #ifdef outputSerial
         Serial.println(F ("connect flag set to 0"));
         #endif    
      }
    }
  }
  // check for log request
  if (btnToggleLog.isPressed()) {
    btnToggleLog.ack();
    #ifdef outputSerial
    sprintf(outputString, "In check for log request: %s", btoa(logData));
    Serial.println(outputString);
    #endif
    if (logData) {
      logData = false;
      btnToggleLog.setTitle(btnToggleLogTitleOn);
      btnToggleLog.setColor(GREEN);
      #ifdef outputSerial
      Serial.println(F("Logging is *** just turnred OFF ***"));
      #endif
    }
    else {
      constructLogFileName( logDirName, logFileName );
      logData = writeHeadersToFile( logDirName, logFileName );
      btnToggleLog.setTitle(btnToggleLogTitleOff);
      btnToggleLog.setColor(RED);
      #ifdef outputSerial
      Serial.println(F("Logging is --- just turned ON ---"));
      #endif
    }
	  saveLogStatus( logData, logDirName, logFileName );
    btnToggleLog.update();
    delay(250);
  }
  
  //  check for request to set the timestamp
  if (btnSetClock.isPressed()) { adjustClock(); }

  if (btnSetDate.isPressed()) { adjustDate(); }
  
  #ifdef PERCENT_ADJ
  priorDsrdPercent = desiredPercent;
  sliderAdjustPercent.getSliderValue(&desiredPercent);
  if (priorDsrdPercent != desiredPercent) {
    sliderAdjustPercent.setData(desiredPercent);
    sliderAdjustPercent.update();
    desiredRatio = desiredPercent / 100.0;    
    savePercent (adjustmentEnabled, desiredPercent);
  }
  
  if (btnEnableAdjustment.isPressed()) {
    btnEnableAdjustment.ack();
    if (adjustmentEnabled) {
      adjustmentEnabled = false;
      btnEnableAdjustment.setTitle(btnEnableAdjustmentTitle);
      btnEnableAdjustment.setColor(GREEN);
      desiredPercent = percentInit;
      sliderAdjustPercent.moveSliderToValue(percentInit);
      sliderAdjustPercent.update();
    } else {
      adjustmentEnabled = true;
      btnEnableAdjustment.setTitle(btnDisableAdjustmentTitle);
      btnEnableAdjustment.setColor(RED);
  }
    btnEnableAdjustment.update();
    savePercent (adjustmentEnabled, desiredPercent);
    delay(250);
 }
    #endif
  #ifdef outputSerial
    sprintf(outputString, " before format time stamp  log data = %s", btoa(logData));
    Serial.println(outputString);
  #endif

  // format time stamp
  DateTime now = rtc.now();  
  currentTime = now.unixtime();
  currentDay = currentTime/86400;
  if (onPeriodStart == 0 && runState == Running) {
    // should only happen at startup
    onPeriodStart = currentTime;
  }

  sprintf(timeStamp, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  clockDisplay.setData(timeStamp);
  sprintf(todayDate, "%02d/%02d/%02d", now.month(), now.day(), now.year()-2000);
  clockDisplay.setUnit(todayDate);
  clockDisplay.update();
  
  for (ndx = 0; ndx < 4; ndx++) {
    if (!tempsensor[ndx].begin(sensor_addr + ndx)) {
      degF[ndx] = -40;
    }
    // Read and print out the temperature, then convert to *F
    tempsensor[ndx].shutdown_wake(0); // try wake up here
    degF[ndx] = tempsensor[ndx].readTempC()*9/5 + 32;
    tempsensor[ndx].shutdown_wake(1); // shutdown MSP9808 - power consumption ~0.1 mikro Ampere
  }
  sprintf(outputString, "%6.1f     %6.1f     %6.1f     %6.1f", degF[0], degF[1], degF[2], degF[3]); 
  tempDisplay.setData(outputString); 
  tempDisplay.update(); 

    // check values of Sensors - what is open, what isn't
  bedroomSensorInput = digitalRead(bedroomSensor);  // read bedroomSensor Pin
  if (bedroomSensorInput == LOW) {   // If Door_Sensor N.0. (nc with magnet) -> HIGH : Door is closed 
                          // LOW : Door is open [This will be our case]
    sprintf(bedroomDisplayStatus, "%s", "Closed");
    SSbedroom = false;   //  bedroom window not open
    digitalWrite(bedroomIndicator,LOW);
  } else {
    sprintf(bedroomDisplayStatus, "%s", " Open ");
    SSbedroom = true;    //  bedroom window is open
    digitalWrite(bedroomIndicator,HIGH);
            }
  officeSensorInput = digitalRead(officeSensor);  // read officeSensor Pin
  if (officeSensorInput == LOW) {
    sprintf(officeDisplayStatus, "%s", "Closed");
    SSoffice = false;
    digitalWrite(officeIndicator,LOW);
  } else {
    sprintf(officeDisplayStatus, "%s", " Open ");
    SSoffice = true;
    digitalWrite(officeIndicator,HIGH);
  }
  kitchenSensorInput = digitalRead(kitchenSensor);  // read kitchenSensor Pin
  if (kitchenSensorInput == LOW) {
    digitalWrite(kitchenIndicator,LOW);
    if (SSkitchen) {
     doorComplete = currentTime + doorDelay;
      sprintf(kitchenDisplayStatus, "%s", "Closing");
   } else {
    if ( currentTime > doorComplete ) {
      sprintf(kitchenDisplayStatus, "%s", "Closed ");
    }
   }
    SSkitchen = false;
  } else {
    digitalWrite(kitchenIndicator,HIGH);
    if (! SSkitchen) {
     doorComplete = currentTime + doorDelay;
     sprintf(kitchenDisplayStatus, "%s", "Opening");
   } else {
    if ( currentTime > doorComplete ) {
      sprintf(kitchenDisplayStatus, "%s", " Open  ");
    }
   }
    SSkitchen = true;
  }
  furnaceSensorInput = digitalRead(furnaceSensor);  // read furnaceSensor Pin
  if (furnaceSensorInput == LOW) {
    sprintf(furnaceDisplayStatus, "%s", "Closed");
    digitalWrite(furnaceIndicator,LOW);
    SSfurnace = false;
  } else {
    sprintf(furnaceDisplayStatus, "%s", " Open ");
    digitalWrite(furnaceIndicator,HIGH);
    SSfurnace = true;
  }
  #ifdef outputSerial
    sprintf(outputString, " before switch run state  log data = %s", btoa(logData));
    Serial.println(outputString);
  #endif
  if ( totPeriodStart == 0 ) totPeriodStart = currentTime;
  #ifdef outputSerial
    sprintf(outputString, " after switch run state  log data = %s", btoa(logData));
    Serial.println(outputString);
  #endif

  dtostrf(100.0 * movingAverage, 5, 2, strTemp0);
  blowerPercent.setData(strTemp0);
  if (adjustmentEnabled) {
    sprintf(outputString, "%s (adjusted req. %d%s)", blowerPercentTitle, desiredPercent, "%"); 
    blowerPercent.setTitle(outputString);
  } else {
    sprintf(outputString, "%s (default req. %d%s)", blowerPercentTitle, desiredPercent, "%"); 
    blowerPercent.setTitle(outputString);
  }
  blowerPercent.update();
  delay(250);
  
  #ifdef outputSerial
  Serial.print (F ("mov avg: "));
  Serial.print (strTemp0);
  sprintf(outputString, "  dsrd: %d  act:  ", desiredPercent);
  Serial.print (outputString);
  dtostrf(100.0 * actualRatio, 5, 3, strTemp0);
  Serial.print (strTemp0);
  Serial.print (F ("  "));
  sprintf (outputString, "furn = %d ", SSfurnace);
  Serial.println(outputString);
  #endif
    
  sprintf (outputString, "%s %s %s %s %s", bedroomDisplayStatus, officeDisplayStatus,
          kitchenDisplayStatus, furnaceDisplayStatus, operatingDisplayStatus);
  switchDisplay.setData(outputString);
  switchDisplay.update();
  delay(250);
    
  if (((currentTime >= loggedTime + interval) || firstTime ) && logData)  {
  
    loggedTime = currentTime;
    logFile = SD.open(logFileName, FILE_WRITE);
    if (! firstTime) {
      #ifdef outputSerial
      Serial.println(F("Logging now"));
      sprintf(outputString, "%s, %s, ", todayDate, timeStamp);
      Serial.print(outputString);
      sprintf(outputString, " %u, %u, %u, %u, %u\n", SSbedroom,
          SSoffice, SSkitchen, SSfurnace, SSrunning);
      Serial.print(outputString);
      #endif
      sprintf(outputString, "%lu,%s,", currentDay + 24107,  timeStamp);
      if (logFile) {
        offset = logFile.write(outputString, strlen(outputString));
            // note that day is exact for Excel, 24107 is delta to make date relative to 1/2/1904
            // time is presented as text to avoid dividing by 86400 in Excel to display correctly
        // append remainder of log - it appears that writing all in one action injects extra characters
        // at the head of the log entry.
        for (ndx = 0; ndx < 4; ndx++) {
          dtostrf(degF[ndx], 5, 3, strTemp0);
          if (offset > 0) { // only do second append if no error from first
            sprintf(outputString, "%s,", strTemp0);
            offset = logFile.write(outputString, strlen(outputString));
          }
        }        
        if (offset > 0) {
          dtostrf(100 * movingAverage, 5, 2, strTemp0);
          sprintf(outputString, "%s, %d,", strTemp0, averageCount);  
          offset = logFile.write(outputString, strlen(outputString));
        }
        if (offset > 0) {
          dtostrf(100 * desiredRatio, 5, 3, strTemp0);
          sprintf(outputString, "%s,", strTemp0);
          offset = logFile.write(outputString, strlen(outputString));
        }
        if (offset > 0) {
          dtostrf(100 * actualRatio, 5, 3, strTemp0);
          sprintf(outputString, "%s,%d,%d,", strTemp0, adjustmentEnabled, runState );
          offset = logFile.write(outputString, strlen(outputString));
        }
        if (offset > 0) {
          sprintf(outputString, "%u,%u,%u,%u,%u\n", SSbedroom, SSoffice, SSkitchen, SSfurnace, SSrunning);
          offset = logFile.write(outputString, strlen(outputString));        
        }
        if (offset > 0 ) {
          logCount++;
          sprintf(logStatus, "%ld", logCount);
        }
        logFile.close();
      } else {  // end log file exists
        offset = 0;
      }
    }  // end first time
    else {
      firstTime = false;
      offset = 1;  // forces logStatus to be retained if firstTime = true.
    }
  }  // end interval or first time

  doRunState();
  
  if(offset < 1) {
     sprintf(logStatus, "%s", "log write");
     logData = false;
     offset = 0;
  }
 
  logStats.setData(logCount);
  if (logData) {
    logStats.setUnit("Logging");
    logStats.setColor(LIGHT_GREEN);
  } else {
    logStats.setUnit("Logging OFF");
    logStats.setColor(MISTY_ROSE);
  }
  logStats.update();
}

// 
//               doRunState
//
//               turn on or off the fan
//

void doRunState( void ) {
  switch (runState) {
    case Running:
      actualRatio = calcPeriodRatio ( currentTime, onPeriodStart, totPeriodStart );
      #ifdef outputSerial
      Serial.print(F("actual ratio in case running: "));
      Serial.print(F("rslt, curr, on, base: "));
      dtostrf(actualRatio, 5, 3, strTemp0);
      Serial.print(strTemp0);
      Serial.print(F("  "));
      Serial.print(currentTime);
      Serial.print(F("  "));
      Serial.print(onPeriodStart);
      Serial.print(F("  "));
      Serial.println(totPeriodStart);
      #endif
  
      if ( SSbedroom || SSoffice || SSfurnace || (SSkitchen &&
             ( currentTime > doorComplete )))  {
        if ( adjustmentEnabled && ( actualRatio < desiredRatio )) {
               runState = RunningAdjusting;
        } else {
           runState = notRunning;
           doNotRunningEntryActions();
        }
      } else {
      if ( adjustmentEnabled && ( actualRatio >= desiredRatio ) ) {
        runState = notRunningAdjusting;
        doNotRunningAdjustingEntryActions();
      }
    }
    break;
    case notRunning:
      if (! SSbedroom && ! SSoffice && ! SSfurnace &&
             ( ! SSkitchen && (currentTime > doorComplete))) {
          runState = Running;
          doRunningEntryActions();
        }
    break;
    case RunningAdjusting:  // off transition but ratio not reached
      actualRatio = calcPeriodRatio ( currentTime, onPeriodStart, totPeriodStart );
      if ((actualRatio >= desiredRatio ) || (! adjustmentEnabled )){
          movingAverage = calcMovingAverage ( movingAverage, actualRatio, averageCount);
          runState = notRunning;
          doNotRunningEntryActions();
      }
    break;
    case notRunningAdjusting:
      if ( SSbedroom || SSoffice || SSfurnace || (SSkitchen &&
             ( currentTime > doorComplete )))  {
          runState = notRunning;
          doNotRunningEntryActions();
       }
    break;
    default:
    break;
  }
}

//
//                doNotRunningEntryActions
//

void doNotRunningEntryActions( void ) {
  SSrunning = false;
  sprintf(operatingDisplayStatus, "%s", "  Off  ");
  digitalWrite(relayControlPin, LOW);
  movingAverage = calcMovingAverage ( movingAverage, actualRatio, averageCount);
  actualRatio = 0;
  totPeriodStart = currentTime;
}

//
//                doRunningEntryActions
//

void doRunningEntryActions( void ) {
  SSrunning = true;
  sprintf(operatingDisplayStatus, "%s", "Running");
  digitalWrite(relayControlPin, HIGH);
  onPeriodStart = currentTime;
}

//
//
//

void doNotRunningAdjustingEntryActions( void ) {
  SSrunning = false;
  sprintf(operatingDisplayStatus, "%s", "  Off  ");
  digitalWrite(relayControlPin, HIGH);
}

//
//         adjustClock    
//

void adjustClock( void ) {
  int daySet;
  int monthSet;
  int yearSet;
  int hourSet; 
  int minuteSet;
  int secondSet;
  DateTime now = rtc.now();
  yearSet = now.year();
  monthSet = now.month();
  daySet = now.day();
  btnSetClock.ack();
  btnSetClock.getTimeInput (&hourSet, &minuteSet, &secondSet);
  btnSetClock.update();
  #ifdef outputSerial
  Serial.println  (F( "Time set test"));
  sprintf(todayDate, "%d/%d/%d", monthSet, daySet, yearSet);
  Serial.println (todayDate);
  sprintf(timeStamp, "%02d:%02d:%02d", hourSet, minuteSet, secondSet);
  Serial.println (timeStamp);
  #endif
  rtc.adjust(DateTime(yearSet, monthSet, daySet, hourSet, minuteSet, secondSet));
  
}

//
//         adjustDate    
//

void adjustDate (void) {
  int daySet;
  int monthSet;
  int yearSet;
  int hourSet; 
  int minuteSet;
  int secondSet;
  DateTime now = rtc.now();
  hourSet = now.hour();
  minuteSet = now.minute();
  secondSet = now.second();
  btnSetDate.ack();
  btnSetDate.getDateInput (&daySet,&monthSet,&yearSet);
  btnSetDate.update();
  #ifdef outputSerial
  Serial.println  (F( "Time set test"));
  sprintf(todayDate, "%d/%d/%d", monthSet, daySet, yearSet);
  Serial.println (todayDate);
  sprintf(timeStamp, "%02d:%02d:%02d", hourSet, minuteSet, secondSet);
  Serial.println (timeStamp);
  #endif
  rtc.adjust(DateTime(yearSet, monthSet, daySet, hourSet, minuteSet, secondSet));
}

//
//          constructFileName
//

void constructLogFileName( char* dirName, char* fileName ){
  DateTime now = rtc.now();
  sprintf(dirName, "D%02d%02d%02d",now.year()-2000, now.month(), now.day());
  sprintf(fileName, "%s/L%02d%02d%02d.csv",dirName, now.hour(), now.minute(), now.second());
}

//
//           writeHeadersToFile
//

bool writeHeadersToFile( char* dirName, char* fileName ) {
  bool b;
  #ifdef outputSerial
  Serial.println(F("writing headers to SD"));
  Serial.println(fileName);
  #endif
  // Write table headers to SD card
  File wfil;

  SD.mkdir(dirName);
  wfil = SD.open(fileName, FILE_WRITE);
  if (wfil) {
    wfil.write (CSV_headers, strlen(CSV_headers));
    b = true;
  } else {
    b = false;  // turn off logging - file problem (most likely SD card not present)
  }
  wfil.close();
  #ifdef outputSerial
  if (SD.exists(fileName)) {
    Serial.println(F("log file exists."));
  } else {
    Serial.println(F("log file doesn't exist."));
  }
  #endif
  return b;
}

//
//             savePercent
//

#ifdef PERCENT_ADJ
void savePercent( bool adj, int pct ) {
  char tChars[9];
  File pctFile;

  SD.remove(pctFileName);
  if (adj) {
    pctFile = SD.open(pctFileName,FILE_WRITE);
    sprintf(tChars,"%d\n", pct);
    pctFile.write(tChars, strlen(tChars));
    pctFile.close();
  }
}

//
//           retrievePercent
//

bool retrievePercent( int* pct ) {
  char tChars[9];
  bool adj;
  File pctFile;

  if (SD.exists(pctFileName)) {
    pctFile = SD.open(pctFileName);
    pctFile.read(tChars, 9);
    *pct = atoi(tChars);
    adj = true;
    pctFile.close();
  } else {
    adj = false;
  }

  sliderAdjustPercent.moveSliderToValue(*pct);
  sliderAdjustPercent.update();
  #ifdef outputSerial
    sprintf (outputString, "in retrieve percent %d", *pct);
    Serial.println(outputString);
  #endif
  delay(250);
  return adj;
}
#endif

//
//          saveLogStatus
//

void saveLogStatus( bool stat, char* dirName, char* fileName ) {
  char tChars[33];
  uint8_t len;
  File statFile;
  
  #ifdef outputSerial
    sprintf (outputString, "in saveLogStatus %s, %s, %s", btoa(stat), dirName, fileName);
    Serial.println(outputString);
  #endif
  
  SD.remove(statFileName);
  if (stat) {
    sprintf (tChars, "%s%s", dirName, fileName);
    len = 28;
    statFile = SD.open(statFileName, FILE_WRITE);
    #ifdef outputSerial
    if (! statFile) {
      Serial.println(F("could not open statFile"));
    }
    #endif
    statFile.write(tChars, len);
    statFile.close();
  }
}

//
//              retrieveLogStatus
//

bool retrieveLogStatus( char* dirName, char* fileName ) {
  int count;
  char tChars[2];
  bool stat;
  File statFile;

  if (SD.exists(statFileName)) {
    statFile = SD.open(statFileName);
    if (statFile) {
        statFile.read(dirName,7);
        dirName[8] = 0;
        statFile.read(fileName,19);
        fileName[20] = 0;
        stat = true;
    } else {
      stat = false;
    }
    statFile.close();
  } else {
    stat = false;
  }
  #ifdef outputSerial
  sprintf (outputString, "in retrieveLogStatus %s, %s, %s", btoa(stat), dirName, fileName);
  Serial.println(outputString);
  #endif
return stat;
}

//
//           calcPeriodRatio
//

float calcPeriodRatio ( unsigned long currentT, unsigned long onT, unsigned long periodT ) {
  float result;
  if (periodT > currentT)  currentT = currentT + 86400; 
  if (periodT > onT)  onT = onT + 86400;
  result = ((float)(currentT - onT) / (float)(currentT - periodT));
  return result;
}

//
//            calcMovingAverage
//

float calcMovingAverage (float mA, float Xi, int & count){
  if (count < maxN) count++;
  float mAout;
  mAout = (mA * (count - 1) + Xi) / count;
  return mAout;
}


