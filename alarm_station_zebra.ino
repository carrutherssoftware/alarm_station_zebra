#include <LTask.h>
#include <LGSM.h>
#include <LBattery.h>
#include <LStorage.h>
#include <LFlash.h>
#include <LGPS.h>

/*******************************************************
* Alarm Station Zebra                                  *
* a LinkIt One Remote Alarm System                     *
* Copyright © 2015 Andrew Carruthers                   *
* All rights reserved. Provided 'AS IS' under the      *
* 'simplified BSD license'. Essentially, leave my      *
* copyright notice if you use it.                      *
*                                                      *
* INSTRUCTIONS                                         *
* Uses text messaging to keep cel costs low.           *
* Notifies users of which sensor was activated, gps    *
* location and of low battery. It must have a phone #  *
* to send alerts to.                                   *
* Send commands via text message to,                   *
*    * Arm and disarm alarm (lockout after 4 attempts) *
*    * Change security code                            *
*    * Add, change or delete notification phone #s     *
* Setup by defining input pins below, max 1 motion     *
* detector and 2 switches
*                                                      *
*******************************************************/

// SET UP INPUT AND OUTPUT PINS HERE
const int PIR = 5; // Input pin from motion detector, -1 is unconnected, tripped on HIGH
const int SW1 = -1; // Input pin from door/window switch, -1 is unconnected, tripped on LOW
const int SW2 = -1; // Input pin from door/window switch, -1 is unconnected, tripped on LOW
const int LED = 8; // Output pin of LED detector indicator

const String defaultCode = "1111"; // Default login code

const int maxLoginAttempts = 4; // Maximum wrong login attempts before lockout
const int lockoutTime = 5; // Lockout time in minutes
const int alarmResetTime = 4; // Alarm reset time in minutes
const int batteryWarningLevel = 33;

char alarmCodeFile[] = "alarmcode.txt"; // Alarm code file name
char primaryPhoneFile[] = "primaryphone.txt";
char secondaryPhoneFile[] = "secondaryphone.txt";

// Main loop variables
uint32_t lockoutTimestamp;
uint32_t lowBatteryWarningTimestamp;
uint32_t alarmResetTimestamp;
int loginAttempts;
boolean alarmArmed;
String alarmCode;
String primaryPhone;
String secondaryPhone;
int state;

// Global GPS variables
String location;
gpsSentenceInfoStruct info;

// Message read function variables
char senderPhoneNum[20];
int messageLength;
char msg[161];
int v;
String senderPhoneNumber;
String message;
String attemptsLeft;
String attempts;
String lastPhone;
String lastCode;

// Read file function variables
String returnValue;

// Write file function variables
LFile dataFile;

// parseLocation function variables
String GPSstring;

void setup() {

  // Setup inputs and outputs
  if (PIR != -1)
  {
    pinMode(PIR, INPUT);
  }
  if (SW1 != -1)
  {
    pinMode(SW1, INPUT);
  }
  if (SW2 != -1)
  {
    pinMode(SW2, INPUT);
  }
  pinMode(LED, OUTPUT);

  Serial.begin(9600);

  LFlash.begin();

  while (!LSMS.ready())
  {
    delay(1000);
  }

  LGPS.powerOn();

  Serial.println("GSM OK!");

  lockoutTimestamp = 0;
  lowBatteryWarningTimestamp = 0;
  alarmResetTimestamp = 0;
  loginAttempts = 0;

  alarmArmed = true;
  location = "No fix";

  // Read pincode if there or assign default
  alarmCode = loadFromFile(alarmCodeFile);
  if (alarmCode == "")
  {
    alarmCode = defaultCode;
  }

  // Load primary phone number or set to empty
  primaryPhone = loadFromFile(primaryPhoneFile);

  // Load secondary phone number or set to empty
  secondaryPhone = loadFromFile(secondaryPhoneFile);
}

void loop() {

  // Light indicator LED from any active sensor
  if (PIR != -1)
  {
    state = digitalRead(PIR);
  }
  else if (SW1 != -1)
  {
    state += (1 - digitalRead(SW1));
  }
  else if (SW2 != -1)
  {
    state += (1 - digitalRead(SW2));
  }

  digitalWrite(LED, state);

  // Check battery and send warning if low
  if (LBattery.level() <= batteryWarningLevel &&
      LBattery.level() > 0 &&
      (millis() - lowBatteryWarningTimestamp > 1000 * 60 * 60 * 24 || lowBatteryWarningTimestamp == 0))
  {
    lowBatteryWarningTimestamp = millis();
    if (primaryPhone != "")
    {
      writeMessage(primaryPhone, "Alarm critical low battery at " + String(LBattery.level()) + "%", 1);
    }
    if (secondaryPhone != "")
    {
      writeMessage(secondaryPhone, "Alarm critical low battery at " + String(LBattery.level()) + "%", 1);
    }
  }

  // Trigger alarm if tripped, armed and reset
  if (state > 0 && alarmArmed && (millis() - alarmResetTimestamp > 1000 * 60 * alarmResetTime || alarmResetTimestamp == 0))
  {
    alarmResetTimestamp = millis();

    // Get location and update location variable
    LGPS.getData(&info);
    parseLocation((const char*)info.GPGGA);

    if (primaryPhone != "")
    {
      writeMessage(primaryPhone, "ALERT! Alarm at GPS location: " + location, 1);
    }
    if (secondaryPhone != "")
    {
      writeMessage(secondaryPhone, "ALERT! Alarm at GPS location: " + location, 1);
    }
  }

  // Read message if not locked out
  if (LSMS.available())
  {
    // Check for lockout
    if (millis() - lockoutTimestamp > 1000 * 60 * lockoutTime)
    {
      readMessage();
    }
    LSMS.flush(); // delete message
  }
}

// Write out an SMS and try up to 3 times until successful
void writeMessage(String phoneNum, String message, int attempt) {

  LSMS.beginSMS(phoneNum.c_str());
  LSMS.print(message);

  // If SMS fails wait 2 seconds then try again for up to 3 attempts
  if (!(LSMS.endSMS()) && attempt < 4)
  {
    delay(2000);
    writeMessage(phoneNum, message, ++attempt);
  }

  return;
}

// Read the incoming message
void readMessage() {

  messageLength = 0;

  LSMS.remoteNumber(senderPhoneNum, 20); // Put incoming number in senderPhoneNumber array

  while (true)
  {
    v = LSMS.read();
    if (v < 0)
    {
      msg[messageLength] = '\0';
      break;
    }
    msg[messageLength] = (char)v;
    Serial.print((char)v);
    ++messageLength;
  }
  Serial.println();

  // Put to string objects
  message = String(msg);
  senderPhoneNumber = String(senderPhoneNum);

  // Check loginCode
  if (!message.startsWith(alarmCode) || message.substring(4, 5) != " ")
  {
    ++loginAttempts;
    attemptsLeft = String(maxLoginAttempts - loginAttempts);
    attempts = String("Incorrect pin input, you have " + attemptsLeft + " attempts left before lockout.");
    writeMessage(senderPhoneNumber, attempts, 1);
    return;
  }

  // Check for null phone numbers right away
  if (primaryPhone == "" && secondaryPhone == "" && message.substring(5, 11) != "change" && message.substring(5, 11) != "CHANGE")
  {
    writeMessage(senderPhoneNumber, "No notification numbers defined, please use change command ie. '#### change1 000-000-0000'", 1);
    return;
  }

  // Send help text
  if (message.substring(5, 6) == "?")
  {
    writeMessage(senderPhoneNumber, "Help: pattern is '#### command param', commands are ON, OFF, CHANGE1, CHANGE2, CHANGEC, then a space then the new code or phone number or 0 (zero) to delete", 1);
    return;
  }

  // Turn alarm notifications on
  if (message.substring(5, 7) == "on" || message.substring(5, 7) == "ON")
  {
    alarmArmed = true;
    writeMessage(senderPhoneNumber, "Alarm is now armed (on).", 1);
    return;
  }

  // Turn alarm notifications off
  if (message.substring(5, 8) == "off" || message.substring(5, 8) == "OFF")
  {
    alarmArmed = false;
    writeMessage(senderPhoneNumber, "Alarm is now disarmed (off).", 1);
    return;
  }

  // Change primary or secondary phone or security code
  if (message.substring(5, 11) == "change" || message.substring(5, 11) == "CHANGE")
  {
    if (message.substring(11, 12) == "1")
    {
      lastPhone = primaryPhone;
      primaryPhone = message.substring(13);

      // Delete number and file if 0
      if (primaryPhone == "0")
      {
        primaryPhone = "";
        LFlash.remove(primaryPhoneFile);
        writeMessage(senderPhoneNumber, "Primary phone number deleted", 1);
      }
      else
      {
        updateFile(primaryPhoneFile, primaryPhone);
        writeMessage(senderPhoneNumber, "Primary phone number changed from " + lastPhone + " to " + primaryPhone, 1);
      }
      return;
    }

    if (message.substring(11, 12) == "2")
    {
      lastPhone = secondaryPhone;
      secondaryPhone = message.substring(13);

      // Delete number and file if 0,
      if (secondaryPhone == "0")
      {
        secondaryPhone = "";
        LFlash.remove(secondaryPhoneFile);
        writeMessage(senderPhoneNumber, "Secondary phone number deleted", 1);
      }
      else
      {
        updateFile(secondaryPhoneFile, secondaryPhone);
        writeMessage(senderPhoneNumber, "Secondary phone number changed from " + lastPhone + " to " + secondaryPhone, 1);
      }
      return;
    }

    // Change security code here
    if (message.substring(11, 12) == "c" || message.substring(11, 12) == "C")
    {
      lastCode = alarmCode;
      alarmCode = message.substring(13, 17);
      updateFile(alarmCodeFile, alarmCode);
      writeMessage(senderPhoneNumber, "Security code changed from " + lastCode + " to " + alarmCode, 1);
      return;
    }
  }
}

// Read string from file
String loadFromFile(char *fileName)
{
  returnValue = "";
  dataFile = LFlash.open(fileName, FILE_READ);
  if (dataFile)
  {
    while (dataFile.available())
    {
      returnValue += String((char)dataFile.read());
    }
    dataFile.close();
    return returnValue;
  }
  else
  {
    Serial.println(String(fileName) + " not found.");
    return "";
  }
}

// Append to or create new file
void updateFile(char *fileName, String data)
{
  LFlash.remove(fileName);
  dataFile = LFlash.open(fileName, FILE_WRITE);
  if (dataFile)
  {
    dataFile.write(data.c_str());
    dataFile.close();
  }
  else
  {
    Serial.println("error opening " + String(fileName));
  }
  return;
}

// GPS location function returns https://maps.google.com/?q=49+23.47953,-123+11.23821
boolean parseLocation(const char* GPGGAstr)
{
  // Check for fix
  if (GPGGAstr[43] == '0') {
    location = "No GPS Fix";
    return 0;
  }

  // Add url prefix
  location = "http://maps.google.com/?q=";
  GPSstring = String(GPGGAstr);

  if (GPGGAstr[28] == 'S') location = location + "-";
  location += GPSstring.substring(18, 20) + "+" + GPSstring.substring(20, 26) + ",";

  if (GPGGAstr[41] == 'W') location = location + "-";
  location = location + GPSstring.substring(30, 33) + "+" + GPSstring.substring(33, 38);

  return 1;
}
