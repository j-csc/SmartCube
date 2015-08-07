#include <Adafruit_BMP085.h>
#include <Wire.h>
#include "RTClib.h"
#include <LiquidCrystal.h>
#include <SPI.h>
#include <Adafruit_VS1053.h>
#include <SD.h>
#include <ZX_Sensor.h>
//ZX SENSOR
const int ZX_ADDR = 0x10;  // ZX Sensor I2C address
ZX_Sensor zx_sensor = ZX_Sensor(ZX_ADDR);
GestureType gesture;
uint8_t gesture_speed;
uint8_t x_pos;
uint8_t z_pos;
//MUSIC MAKER SHIELD
#define CLK 13
#define MISO 12
#define MOSI 11
#define CARDCS 4     // Card chip select pin
// DREQ should be an Int pin, see http://arduino.cc/en/Reference/attachInterrupt
#define DREQ 3       // VS1053 Data request, ideally an Interrupt pin
#define SHIELD_RESET  -1      // VS1053 reset pin (unused!)
#define SHIELD_CS     7      // VS1053 chip select pin (output)
#define SHIELD_DCS    6      // VS1053 Data/command select pin (output)
Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(SHIELD_RESET, SHIELD_CS, SHIELD_DCS, DREQ, CARDCS);
bool DKDEBUG = true;
RTC_DS1307 rtc;
bool alarmOn = false;
const int snoozeAmount = 5000; // 5 seconds
bool snoozing;
DateTime alarm1;
DateTime alarm1SnoozeTime;
bool alarm1Enabled;
bool alarm1Active;
DateTime alarm2;
DateTime alarm2SnoozeTime;
bool alarm2Enabled;
bool alarm2Active;
enum clockState {
  clockStateNormalOperation,
  clockStateSetTime,
  clockStateSetAlarm1,
  clockStateSetAlarm2,
  clockStateCount

};
int VOLUME = 40;
byte newHour, newMinute;
clockState setMode;
DateTime currentSetTime;
const int hourPin = 1;
const int minutePin = 2;
const int setPin = 3;
LiquidCrystal lcd(8, 9, 17, 16, 15, 14);
Adafruit_BMP085 bmp;
//const char* songTrack[] = {"track001.mp3", "track002.mp3", "track003.mp3",
//                            "track004.mp3", "track005.mp3", "track006.mp3", "track007.mp3", "track008.mp3",
//                            "track009.mp3"
//                           };
//int currentFile;

enum songTracks
{
  track001,
  track002,
  track003,
  track004,
  track005,
  track006,
  track007,
  track008,
  track009,
  numberOfTracks
};

songTracks currentSong;

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(9600);
#ifdef AVR
  Wire.begin();
#else
  Wire1.begin();
#endif
  rtc.begin();
  DateTime now = rtc.now();
  alarm2 = DateTime(now.year(), now.month(), now.day(), 6, 0, 0);
  //  alarm1 = DateTime(now.year(),now.month(),now.day(),6,0,0);
  alarm1Enabled = false;
  alarm1 = DateTime(now.unixtime() + 3);
  synchronizeSnoozeTimesToAlarmTimes();

  lcd.begin(16, 2);
  lcd.clear();

  //TODO: FIX WIRING AND UNCOMMENT THESE!!!
  setupMusicPlayer();
  setupZXSensor();
  setupBMP();

}

void setupBMP()
{
  if (!bmp.begin())
  {
    Serial.println(F("Could not find a valid BMP085 sensor, check wiring!"));
    while (1) {}
  }
}

void setupZXSensor()
{
  // Initialize ZX Sensor (configure I2C and read model ID)
  if ( zx_sensor.init() ) {
    Serial.println("ZX Sensor initialization complete");
  } else {
    Serial.println("Something went wrong during ZX Sensor init!");
  }

}

void setupMusicPlayer()
{

  if (! musicPlayer.begin()) {
    Serial.println(F("Couldn't find VS1053, do you have the right pins defined?"));
    while (1);
  }
  if (!SD.begin(CARDCS)) {
    Serial.println(F("SD failed, or not present"));
    while (1);
  }
  Serial.println(F("SD OK!"));

  printDirectory(SD.open("/"), 0);
  musicPlayer.setVolume(VOLUME, VOLUME);
  if (! musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT))
    lcd.print("Hi, World");
  musicPlayer.GPIO_pinMode(hourPin, INPUT);
  musicPlayer.GPIO_pinMode(minutePin, INPUT);
  musicPlayer.GPIO_pinMode(setPin, INPUT);
}

void loop()
{

  gestureAction();

  if (setMode == clockStateNormalOperation)
  {
    if (debounceRead(setPin))
    {
      setMode = clockStateSetTime;
      currentSetTime = rtc.now();
      lcd.clear();
      Serial.println(F("now in set mode"));
    }
    else
    {
      updateDisplayNormalOperation();
      monitorAlarmButtons();
      checkAlarms();
      if (alarm1Active || alarm2Active)
      {
        monitorSnoozeSensor();
      }
    }
  } else
  {
    blinkDisplay();
    updateDisplaySetMode();
    monitorHourMinuteButtons();
    if (debounceRead(setPin))
    {
      lcd.clear();
      setMode = clockState(int(setMode) + 1);
      Serial.println(setMode);
      if (setMode == clockStateCount)
      {
        setMode = clockStateNormalOperation;
        lcd.display();
        setRTCTime();
      }
    }
  }

  if (Serial.available())
  {
    char plswork = Serial.read();
    serialControl(plswork);
  }
}

void gestureAction()
{
  if ( zx_sensor.gestureAvailable() )
  {
    gesture = zx_sensor.readGesture();
    gesture_speed = zx_sensor.readGestureSpeed();
    switch ( gesture ) {
      case NO_GESTURE:
        Serial.println(F("No Gesture"));
        break;
      case RIGHT_SWIPE:
        Serial.print(F("Right Swipe. Speed: "));
        Serial.println(gesture_speed, DEC);
        skipSong();
        break;
      case LEFT_SWIPE:
        Serial.print(F("Left Swipe. Speed: "));
        Serial.println(gesture_speed, DEC);
        previousSong();
        break;
    }
  }
}

void serialControl(char plswork)
{
  if (plswork == 'S')
  {
    Serial.println("Play Song");
    playSong();
  }
  if (plswork == 'P') {
    if (! musicPlayer.paused()) {
      Serial.println("Paused");
      musicPlayer.pausePlaying(true);
    } else {
      Serial.println("Resumed");
      musicPlayer.pausePlaying(false);
    }
  }
  if (plswork == 'X') {
    musicPlayer.stopPlaying();
  }

  if (plswork == 'H')
  {
    //Serial.println("Please enter the hour you want us to set:");
    Serial.println(F("Enter Hour"));
    newHour = Serial.read() - 48;
    currentSetTime = (currentSetTime.unixtime() + 3600 * (newHour - currentSetTime.hour()));
    rtc.adjust(currentSetTime);
    lcd.clear();
  }
  if (plswork == 'M')
  {
    //Serial.println("Please enter the minute you want us to set:");
    Serial.println(F("Enter Minute"));
    newMinute = Serial.read() - 48;
    currentSetTime = (currentSetTime.unixtime() + 60 * (newMinute - currentSetTime.minute()));
    rtc.adjust(currentSetTime);
    lcd.clear();
  }

  if (plswork == 'U')
  {
    if (VOLUME == 0)
    {
      Serial.println("MAX VOLUME");
    }
    else
    {
      VOLUME -= 10;
      musicPlayer.setVolume(VOLUME - 10, VOLUME - 10);
      Serial.println(VOLUME);
    }
  }
  if (plswork == 'D')
  {
    if (VOLUME == 50)
    {
      Serial.println("MIN VOLUME");
    }
    else
    {
      VOLUME += 10;
      musicPlayer.setVolume(VOLUME + 10, VOLUME + 10);
      Serial.println(VOLUME);
    }
  }
  if (plswork == '>')
  {
    skipSong();
  }
  if (plswork == '<')
  {
    previousSong();
  }
}

void setRTCTime()
{
  rtc.adjust(currentSetTime);
}
int buttonStates[4] = {LOW, LOW, LOW, LOW};
int lastButtonStates[4];

int buttonState = LOW;      // the current reading from the input pin
int lastButtonState = LOW;  // the previous reading from the input pin
long lastDebounceTime = 0;  // the last time the output pin was toggled
long debounceDelay = 50;    // the debounce time; increase if the output flickers
long lastChange = 0;


bool debounceRead(int pin)
{
  if ((millis() - lastChange) < 200)
  {
    return false;
  }
  switch (pin)
  {
    case hourPin:
      {
        buttonState = buttonStates[0];
        lastButtonState = lastButtonStates[0];
      }
      break;
    case minutePin:
      {
        buttonState = buttonStates[1];
        lastButtonState = lastButtonStates[1];
      }
      break;
    case setPin:
      {
        buttonState = buttonStates[2];
        lastButtonState = lastButtonStates[2];
      }
      break;
  }
  int reading = musicPlayer.GPIO_digitalRead(pin);
  if (reading != lastButtonState)
  {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay)
  {
    if (reading != buttonState)
    {
      Serial.println(F("Button press successful"));
      buttonState = reading;
      lastChange = millis();
      return buttonState;
    }
  }
  lastButtonState = reading;
  switch (pin)
  {
    case hourPin:
      {
        buttonStates[0] = buttonState;
        lastButtonStates[0] = lastButtonState;
      }
      break;
    case minutePin:
      {
        buttonStates[1] = buttonState;
        lastButtonStates[1] = lastButtonState;
      }
      break;
    case setPin:
      {
        buttonStates[2] = buttonState;
        lastButtonStates[2] = lastButtonState;
      }
      break;
  }
}
void monitorAlarmButtons()
{
  if (debounceRead(hourPin))
  {
    if (alarm1Active)
    {
      alarm1Active = false;
      synchronizeSnoozeTimesToAlarmTimes();
      stopAlarm();
    }
    else
    {
      alarm1Enabled = !alarm1Enabled;
    }
  }
  if (debounceRead(minutePin))
  {
    if (alarm2Active)
    {
      alarm2Active = false;
      synchronizeSnoozeTimesToAlarmTimes();
      stopAlarm();
    }
    else
    {
      alarm2Enabled = !alarm2Enabled;
    }
  }
}
void monitorHourMinuteButtons()
{
  if (debounceRead(hourPin))
  {
    switch (setMode)
    {
      case clockStateSetTime:
        {
          currentSetTime = (currentSetTime.unixtime() + 3600);
        } break;
      case clockStateSetAlarm1:
        {
          alarm1 = (alarm1.unixtime() + 3600);
        } break;
      case clockStateSetAlarm2:
        {
          alarm2 = (alarm2.unixtime() + 3600);
        } break;
    }
    synchronizeSnoozeTimesToAlarmTimes();
  }

  if (debounceRead(minutePin))
  {
    switch (setMode)
    {
      case clockStateSetTime:
        {
          currentSetTime = (currentSetTime.unixtime() + 60);
        } break;
      case clockStateSetAlarm1:
        {
          alarm1 = (alarm1.unixtime() + 60);
        } break;
      case clockStateSetAlarm2:
        {
          alarm2 = (alarm2.unixtime() + 60);
        } break;
    }
    synchronizeSnoozeTimesToAlarmTimes();
  }
}

void synchronizeSnoozeTimesToAlarmTimes()
{
  alarm1SnoozeTime = alarm1.unixtime();
  alarm2SnoozeTime = alarm2.unixtime();
}
void monitorSnoozeSensor()
{
  if ( zx_sensor.positionAvailable() ) {
    z_pos = zx_sensor.readZ();
    if ( z_pos != ZX_ERROR ) {
      Serial.print(F(" Z: "));
      Serial.println(z_pos);
      if (z_pos < 10)
      {
        snoozeHit();
      }
    }
  }
}
void checkAlarms()
{
  DateTime now = rtc.now();
  static long timeUpdate = 0;
  if (millis() - timeUpdate > 1000)
  {
    timeUpdate = millis();
    if ((shouldTriggerAlarmForTime(alarm1) || shouldTriggerAlarmForTime(alarm1SnoozeTime)) &&
        alarm1Enabled)
    {
      startAlarm();
      alarm1Active = true;
    }
    if ((shouldTriggerAlarmForTime(alarm2) || shouldTriggerAlarmForTime(alarm2SnoozeTime)) &&
        alarm2Enabled )
    {

      startAlarm();
      alarm2Active = true;
    }
  }
}
bool shouldTriggerAlarmForTime(DateTime alarmTime)
{
  if (alarmOn)  return false;
  DateTime now = rtc.now();
  return (now.hour() == alarmTime.hour()     &&
          now.minute() == alarmTime.minute());
}
#pragma mark - Alarm Ringing functionality
void startAlarm()
{
  playSong();
  Serial.print(F("startAlarm"));
  alarmOn = true;
}
void stopAlarm()
{
  stopSong();
  alarmOn = false;
}
void playSong()
{
  musicPlayer.startPlayingFile("track004.mp3");
}
void stopSong()
{
  musicPlayer.stopPlaying();
}
void snoozeHit()
{
  DateTime now = rtc.now();
  Serial.println(F("SNOOZING"));
  if (alarm1Active)
  {
    alarm1Active = false;
    alarm1SnoozeTime = now.unixtime() + snoozeAmount;
  }
  if (alarm2Active)
  {
    alarm2Active = false;
    alarm2SnoozeTime = now.unixtime() + snoozeAmount;
  }
  stopAlarm();
}
void updateDisplayNormalOperation()
{
  static long timeUpdate = 0;
  if (millis() - timeUpdate > 500)
  {
    timeUpdate = millis();
    writeCurrentTime();
    writeAlarmsStatus();
    writeTemperature();
  }
}
void writeTemperature()
{
  int temp = bmp.readTemperature();
  lcd.setCursor(12, 0);
  lcd.print(temp);
  lcd.print("C");
}
void updateDisplaySetMode()
{
  switch (setMode) {
    case clockStateSetTime:
      {
        writeTimeOnDisplay(currentSetTime, false, 0, 0);
      } break;
    case clockStateSetAlarm1:
      {
        writeTimeOnDisplay(alarm1, false, 0, 1);
      } break;
    case clockStateSetAlarm2:
      {
        writeTimeOnDisplay(alarm2, false, 10, 1);
      } break;
  }
}
bool displayOn = true;
void blinkDisplay()
{
  static long lastUpdate;
  if (millis() - lastUpdate > 500)
  {
    lastUpdate = millis();
    displayOn = !displayOn;
    if (displayOn)
    {
      lcd.noDisplay();
    }
    else
    {
      lcd.display();
    }
  }
}
void writeAlarmsStatus()
{
  lcd.setCursor(0, 1);
  if (alarm1Enabled)
  {
    if (alarm1Active)
    {
      lcd.print("*");
    }
    writeTimeOnDisplay(alarm1, false, 0, 1);
  }
  else
  {
    lcd.print("--:-- ");
  }
  lcd.setCursor(10, 1);
  if (alarm2Enabled)
  {
    if (alarm2Active)
    {
      lcd.print("*");
    }
    writeTimeOnDisplay(alarm2, false, 10, 1);
  }
  else
  {
    lcd.print("--:-- ");
  }
}
void writeCurrentTime()
{
  writeTimeOnDisplay(rtc.now(), true, 0, 0);
  if (DKDEBUG)
  {
    DateTime now = rtc.now();
    int armyHour = now.hour();
    if (armyHour > 12)
    {
      armyHour -= 12;
    }
  }
}
void writeTimeOnDisplay(DateTime time, bool withSeconds, int atCharacterIndex, int onLine)
{
  bool isAM = true;
  int armyHour = time.hour();
  if (armyHour > 12) {
    isAM = false;
    armyHour -= 12;
  }
  lcd.setCursor(atCharacterIndex, onLine);
  if (armyHour < 10) {
    lcd.print('0');
  }
  lcd.print(armyHour, DEC);

  lcd.print(':');
  if (time.minute() < 10) {
    lcd.print('0');
  }
  lcd.print(time.minute(), DEC);
  if (withSeconds)
  {
    lcd.print(':');
    if (time.second() < 10) {
      lcd.print('0');
    }
    lcd.print(time.second(), DEC);
  }
  if (isAM)
  {
    lcd.print("A");
  }
  else
  {
    lcd.print("P");
  }
}
void printDirectory(File dir, int numTabs) {
  while (true) {
    File entry =  dir.openNextFile();
    if (! entry) {
      break;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    } else {
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}

void skipSong()
{
  switch (currentSong)
  {
    case track001:
      musicPlayer.stopPlaying();
      musicPlayer.startPlayingFile("track002.mp3");
      currentSong = track002;
      break;
    case track002:
      musicPlayer.stopPlaying();
      musicPlayer.startPlayingFile("track003.mp3");
      currentSong = track003;
      break;
    case track003:
      musicPlayer.stopPlaying();
      musicPlayer.startPlayingFile("track004.mp3");
      currentSong = track004;
      break;
    case track004:
      musicPlayer.stopPlaying();
      musicPlayer.startPlayingFile("track005.mp3");
      currentSong = track005;
      break;
    case track005:
      musicPlayer.stopPlaying();
      musicPlayer.startPlayingFile("track006.mp3");
      currentSong = track006;
      break;
    case track006:
      musicPlayer.stopPlaying();
      musicPlayer.startPlayingFile("track007.mp3");
      currentSong = track007;
      break;
    case track007:
      musicPlayer.stopPlaying();
      musicPlayer.startPlayingFile("track008.mp3");
      currentSong = track008;
      break;
    case track008:
      musicPlayer.stopPlaying();
      musicPlayer.startPlayingFile("track001.mp3");
      currentSong = track001;
      break;
  }

  // stopSong();
  //   if (currentFile == sizeof(songTrack))
  //   {
  //     currentFile = 0;
  //     musicPlayer.startPlayingFile(songTrack[currentFile]);
  // }
  //   else
  //   {
  //     currentFile += 1;
  //     musicPlayer.startPlayingFile(songTrack[currentFile]);
  //   }
}
void previousSong()
{
  switch (currentSong)
  {
    case track001:
      musicPlayer.stopPlaying();
      musicPlayer.startPlayingFile("track008.mp3");
      currentSong = track008;
      break;
    case track002:
      musicPlayer.stopPlaying();
      musicPlayer.startPlayingFile("track001.mp3");
      currentSong = track001;
      break;
    case track003:
      musicPlayer.stopPlaying();
      musicPlayer.startPlayingFile("track002.mp3");
      currentSong = track002;
      break;
    case track004:
      musicPlayer.stopPlaying();
      musicPlayer.startPlayingFile("track003.mp3");
      currentSong = track003;
      break;
    case track005:
      musicPlayer.stopPlaying();
      musicPlayer.startPlayingFile("track004.mp3");
      currentSong = track004;
      break;
    case track006:
      musicPlayer.stopPlaying();
      musicPlayer.startPlayingFile("track005.mp3");
      currentSong = track005;
      break;
    case track007:
      musicPlayer.stopPlaying();
      musicPlayer.startPlayingFile("track006.mp3");
      currentSong = track006;
      break;
    case track008:
      musicPlayer.stopPlaying();
      musicPlayer.startPlayingFile("track007.mp3");
      currentSong = track007;
      break;
  }

  //      stopSong();
  //      if (currentFile == 0)
  //      {
  //        currentFile = sizeof(songTrack);
  //        musicPlayer.startPlayingFile(songTrack[currentFile]);
  //    }
  //      else
  //      {
  //        currentFile -= 1;
  //        musicPlayer.startPlayingFile(songTrack[currentFile]);
  //      }
}
