# Exchange-fan-data-log-V2
Version 2 of the controller / logger for a home air exchanger.

Version 2 is based on the prior version, with major changes to use
features of the Arduino 101 and Andee101.  Additionally the program
has been restructured, the state machine revised and corrected, and
files for retaining settings in case of power interruptions have
been revised.

This device controls operation of a home air exchange fan, and logs data 
from four temperature sensors as well as the switches on the sliding
deck door and windows in the bedroom and office.  An additional input
to controlling the fan which is also logged is the furnace control.

Information on the air exchanger is at
http://www.renewaire.com/products/residential-products/ev300

The air exchanger basically brings in air from the outside, and to save energy,
exchanges heat from inside air that is then vented outside.

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
*  Date number (number of days since 1/2/1904, the base used by Mac Excel)
*  Time of day (number of seconds since midnight, which must be converted 
    in Excel to a fraction of the day by dividing by 86400)
*  Temperature from sensor 0 (Outside air)
*  Temperature from sensor 1 (Outside air after exchange)
*  Temperature from sensor 2 (Inside air)
*  Temperature from sensor 3 (Inside air after exchange)
*  % of time movingAvg
*      desired %
*      actual %
*      adjustment enabled
*      run mode state
*  Status of Bedroom windows (0 = open, 1 = closed)
*  Status of Office windows
*  Status of Sliding Deck Door
*  Status of Furnace signal
*  Status of Air Exchange Fan (1 = running, 0 = not running)

The exchange fan is set to running if requested by the furnace and
all windows and the door are closed, except that the door signal is 
delayed to avoid turning the fan on and off excessively when the door
is used for a quick entry or exit from the house.  The delay is set by
constant doorDelay (in seconds).

The time running percent may be adjusted by the computer, and there are
buttons for setting the requested percent, reporting the current
percent, toggling enablement of adjustment.  The percentage calculated
time running per on/off cycle.

Update log:

2.0  1/16/2017
Converted to use Arduino 101, adafruit Datalogger shield

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
