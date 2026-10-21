#include <HID-Project.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

#define SERIAL_BAUD 9600

//  OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDR 0x3C
#define OLED_RESET -1
#define MAX_VISIBLE_ITEMS 4

//  Switch matrix dimensions
#define NUM_ROWS 4
#define NUM_COLS 3
#define MAX_LAYERS 3
#define MAX_MACRO_STEPS 10

//  EEPROM
#define EEPROM_SIGNATURE_ADDRESS 1020
#define EEPROM_SIGNATURE 44
#define LAYER_LABELS_ADDRESS 950
#define LED_MODE_ADDRESS 1021
#define BRIGHTNESS_ADDRESS 1022

//  LED
#define LED_PIN 5
#define NUMPIXELS 12

//  M-key codes used by UART macro syntax
//  M1-M4 are keyboard modifiers. M5-M18 are special keys.
#define MOD_CTRL 1
#define MOD_SHIFT 2
#define MOD_ALT 3
#define MOD_GUI 4
#define MOD_ENTER 5
#define MOD_TAB 6
#define MOD_F1 7
#define MOD_F2 8
#define MOD_F3 9
#define MOD_F4 10
#define MOD_F5 11
#define MOD_F6 12
#define MOD_F7 13
#define MOD_F8 14
#define MOD_F9 15
#define MOD_F10 16
#define MOD_F11 17
#define MOD_F12 18
#define MOD_MAX 18

//  State machine
#define STATE_NORMAL 0
#define STATE_MAIN_MENU 1
#define STATE_LAYERS_MENU 2
#define STATE_LEDS_MENU 3
#define STATE_PROG_MODE 4
#define STATE_BRIGHTNESS_MENU 5
#define STATE_STATIC_COLORS_MENU 6
#define STATE_EFFECTS_MENU 7



//  Numbers of options in each menu
#define numMainMenuItems 3
#define numLedsMenuOptions 3
#define numStaticColors 4
#define numEffects 4

//  Encoder
#define ENC_A A5
#define ENC_B A4
#define ENC_SW A3

#define RELEASE_DELAY 20
#define END_MACRO_DELAY 300
#define LOOP_DELAY 2

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

//  Switch matrix pins
const byte rowPins[NUM_ROWS] = { 0, 1, 30, 12 };
const byte colPins[NUM_COLS] = { 8, 9, 10 };

bool lastKeyState[NUM_ROWS][NUM_COLS];  //  Remembers which keys were pressed

//  One key instruction
struct __attribute__((packed)) MacroStep {
  byte action;  // 1-Press  2-Release  3-Release all
  byte key;
};

//  Set of key instructions
struct __attribute__((packed)) KeyConfig {
  char label[6];
  MacroStep steps[MAX_MACRO_STEPS];
};


char layerNames[MAX_LAYERS][15] = { "L1", "L2", "L3" };
char dynamicLabels[MAX_LAYERS][NUM_ROWS][NUM_COLS][6];  //  Retrieved from EEPROM

//  Menu variables and states
int currentScreenState = STATE_NORMAL;
int mainMenuIndex = 0;
int colorMenuIndex = 0;
int currentLayer = 0;
int screenOffset = 0;

//  Menu options
const char* mainMenuItems[numMainMenuItems] = { "1. LAYER SELECT", "2. LED COLOR", "3. UART PROG" };
const char* ledMenuOptions[numLedsMenuOptions] = { "1. BRIGHTNESS", "2. STATIC COLORS", "3. EFFECTS" };
const char* staticColorOptions[numStaticColors] = { "1. CYAN", "2. GREEN", "3. RED", "4. PURPLE" };
const char* effectOptions[numEffects] = { "1. RAINBOW", "2. BREATHE", "3. REACTION", "4. RIPPLE" };

//  Encoder variables
int lastEncoderStateA;
bool encoderSwitchPressed = false;


//  LED options (static and effects)
int currentBrightness = 70;
int currentLedMode = 1;

struct RGB {
  byte r;
  byte g;
  byte b;
};

RGB staticColors[4] = { { 0, 255, 100 }, { 0, 255, 0 }, { 250, 0, 0 }, { 150, 0, 255 } };

//  Effects
unsigned long lastLedUpdate = 0;

//  Rainbow effect
uint16_t rainbowCycle = 0;

//  Breathe effect
uint8_t breatheVal = 10;
int8_t breatheDir = 5;

//  Reaction effect
uint8_t reactiveFade[NUMPIXELS] = { 0 };

// Ripple effect
uint8_t rippleOriginR = 0;
uint8_t rippleOriginC = 0;
unsigned long rippleStartTime = 0;

// Pop-up variables
unsigned long popupTimer = 0;
bool isPopupActive = false;
char popupText[20] = "";

//  Functions
void handleLEDs();
uint32_t Wheel(byte WheelPos);
void updateOLED();
void applyActiveColor();
void readUART();
void executeMacro(int r, int c, bool isPress);
void pressMacroKey(byte keyCode);
void releaseMacroKey(byte keyCode);
bool parseMacroKey(const char* keyPart, byte& keyCode);
void loadConfigFromEEPROM();
void writeDefaultConfigEEPROM();


void pressMacroKey(byte keyCode) {
  switch (keyCode) {
    case MOD_CTRL: Keyboard.press(KEY_LEFT_CTRL); break;
    case MOD_SHIFT: Keyboard.press(KEY_LEFT_SHIFT); break;
    case MOD_ALT: Keyboard.press(KEY_LEFT_ALT); break;
    case MOD_GUI: Keyboard.press(KEY_LEFT_GUI); break;
    case MOD_ENTER: Keyboard.press(KEY_ENTER); break;
    case MOD_TAB: Keyboard.press(KEY_TAB); break;
    case MOD_F1: Keyboard.press(KEY_F1); break;
    case MOD_F2: Keyboard.press(KEY_F2); break;
    case MOD_F3: Keyboard.press(KEY_F3); break;
    case MOD_F4: Keyboard.press(KEY_F4); break;
    case MOD_F5: Keyboard.press(KEY_F5); break;
    case MOD_F6: Keyboard.press(KEY_F6); break;
    case MOD_F7: Keyboard.press(KEY_F7); break;
    case MOD_F8: Keyboard.press(KEY_F8); break;
    case MOD_F9: Keyboard.press(KEY_F9); break;
    case MOD_F10: Keyboard.press(KEY_F10); break;
    case MOD_F11: Keyboard.press(KEY_F11); break;
    case MOD_F12: Keyboard.press(KEY_F12); break;
    default:
      if (keyCode >= 32 && keyCode <= 126) Keyboard.press((char)keyCode);
      break;
  }
}

void releaseMacroKey(byte keyCode) {
  switch (keyCode) {
    case MOD_CTRL: Keyboard.release(KEY_LEFT_CTRL); break;
    case MOD_SHIFT: Keyboard.release(KEY_LEFT_SHIFT); break;
    case MOD_ALT: Keyboard.release(KEY_LEFT_ALT); break;
    case MOD_GUI: Keyboard.release(KEY_LEFT_GUI); break;
    case MOD_ENTER: Keyboard.release(KEY_ENTER); break;
    case MOD_TAB: Keyboard.release(KEY_TAB); break;
    case MOD_F1: Keyboard.release(KEY_F1); break;
    case MOD_F2: Keyboard.release(KEY_F2); break;
    case MOD_F3: Keyboard.release(KEY_F3); break;
    case MOD_F4: Keyboard.release(KEY_F4); break;
    case MOD_F5: Keyboard.release(KEY_F5); break;
    case MOD_F6: Keyboard.release(KEY_F6); break;
    case MOD_F7: Keyboard.release(KEY_F7); break;
    case MOD_F8: Keyboard.release(KEY_F8); break;
    case MOD_F9: Keyboard.release(KEY_F9); break;
    case MOD_F10: Keyboard.release(KEY_F10); break;
    case MOD_F11: Keyboard.release(KEY_F11); break;
    case MOD_F12: Keyboard.release(KEY_F12); break;
    default:
      if (keyCode >= 32 && keyCode <= 126) Keyboard.release((char)keyCode);
      break;
  }
}

bool parseMacroKey(const char* keyPart, byte& keyCode) {
  if (!keyPart || keyPart[0] == '\0') return false;

  if (keyPart[0] == 'M') {
    int mCode = atoi(keyPart + 1);
    if (mCode >= 1 && mCode <= MOD_MAX) {
      keyCode = (byte)mCode;
      return true;
    }
    return false;
  }

  keyCode = keyPart[0];
  return true;
}

void setup() {
  delay(1000);
  pixels.begin();

  //  loads currect LED mode
  currentLedMode = EEPROM.read(LED_MODE_ADDRESS);
  if (currentLedMode == 0 || currentLedMode > 8) currentLedMode = 1;

  //  loads currect brightness
  currentBrightness = EEPROM.read(BRIGHTNESS_ADDRESS);
  if (currentBrightness == 0 || currentBrightness == 255) currentBrightness = 70;
  pixels.setBrightness(currentBrightness);

  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDR);
  Serial.begin(SERIAL_BAUD);

  //  Encoder config
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  lastEncoderStateA = digitalRead(ENC_A);

  //  Switch matrix config
  for (int c = 0; c < NUM_COLS; c++) pinMode(colPins[c], INPUT_PULLUP);
  for (int r = 0; r < NUM_ROWS; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH);
  }

  //  If there is no signature on specified address it writes default config
  if (EEPROM.read(EEPROM_SIGNATURE_ADDRESS) != EEPROM_SIGNATURE) writeDefaultConfigEEPROM();

  //  loads key definitions
  loadConfigFromEEPROM();

  //  loads layer labels
  for (int i = 0; i < MAX_LAYERS; i++) EEPROM.get(LAYER_LABELS_ADDRESS + (i * 15), layerNames[i]);

  //  if color is static
  applyActiveColor();

  Keyboard.begin();
  Consumer.begin();

  updateOLED();
}

void loop() {

  //  reads uart only if its in PROG MODE
  if (currentScreenState == STATE_PROG_MODE) {
    if (Serial.available() > 0) readUART();
  } else {
    while (Serial.available() > 0) Serial.read();  //  ignores input
  }

  //  Encoder logic - navigation through menus
  bool encoderMoved = false;
  int currentEncoderStateA = digitalRead(ENC_A);

  if (currentEncoderStateA != lastEncoderStateA && currentEncoderStateA == LOW) {
    int dir = (digitalRead(ENC_B) != currentEncoderStateA) ? -1 : 1;
    if (currentScreenState == STATE_MAIN_MENU || currentScreenState == STATE_LAYERS_MENU || currentScreenState == STATE_LEDS_MENU || currentScreenState == STATE_STATIC_COLORS_MENU || currentScreenState == STATE_EFFECTS_MENU) {
      dir = -dir;
    }
    switch (currentScreenState) {
      case STATE_NORMAL:
        if (dir == 1) Consumer.write(MEDIA_VOLUME_UP);
        else Consumer.write(MEDIA_VOLUME_DOWN);
        break;
      case STATE_MAIN_MENU:
        mainMenuIndex += dir;
        if (mainMenuIndex < 0) mainMenuIndex = 2;
        if (mainMenuIndex > 2) mainMenuIndex = 0;
        break;
      case STATE_LAYERS_MENU:
        currentLayer += dir;
        if (currentLayer < 0) currentLayer = MAX_LAYERS - 1;
        if (currentLayer >= MAX_LAYERS) currentLayer = 0;
        break;
      case STATE_LEDS_MENU:
        colorMenuIndex += dir;
        if (colorMenuIndex < 0) colorMenuIndex = numLedsMenuOptions - 1;
        if (colorMenuIndex >= numLedsMenuOptions) colorMenuIndex = 0;
        break;
      case STATE_STATIC_COLORS_MENU:
        colorMenuIndex += dir;
        if (colorMenuIndex < 0) colorMenuIndex = numStaticColors - 1;
        if (colorMenuIndex >= numStaticColors) colorMenuIndex = 0;
        break;
      case STATE_EFFECTS_MENU:
        colorMenuIndex += dir;
        if (colorMenuIndex < 0) colorMenuIndex = numEffects - 1;
        if (colorMenuIndex >= numEffects) colorMenuIndex = 0;
        break;
      case STATE_BRIGHTNESS_MENU:
        currentBrightness += (dir * 10);
        if (currentBrightness > 150) currentBrightness = 150;
        if (currentBrightness < 0) currentBrightness = 0;
        pixels.setBrightness(currentBrightness);
        applyActiveColor();
        break;
    }
    encoderMoved = true;
  }
  lastEncoderStateA = currentEncoderStateA;

  //  Encoder switch pressed 
  if (digitalRead(ENC_SW) == LOW) {
    if (!encoderSwitchPressed) {
      encoderSwitchPressed = true;

      if (currentScreenState == STATE_NORMAL) {
        currentScreenState = STATE_MAIN_MENU;
        mainMenuIndex = 0;
        screenOffset = 0;
      } else if (currentScreenState == STATE_MAIN_MENU) {
        if (mainMenuIndex == 0) {
          currentScreenState = STATE_LAYERS_MENU;
          screenOffset = 0;
        } else if (mainMenuIndex == 1) {
          currentScreenState = STATE_LEDS_MENU;
          colorMenuIndex = 0;
          screenOffset = 0;
        } else if (mainMenuIndex == 2) currentScreenState = STATE_PROG_MODE;
      } else if (currentScreenState == STATE_LEDS_MENU) {
        if (colorMenuIndex == 0) currentScreenState = STATE_BRIGHTNESS_MENU;
        else if (colorMenuIndex == 1) {
          currentScreenState = STATE_STATIC_COLORS_MENU;
          colorMenuIndex = 0;
          screenOffset = 0;
        } else if (colorMenuIndex == 2) {
          currentScreenState = STATE_EFFECTS_MENU;
          colorMenuIndex = 0;
          screenOffset = 0;
        }
      } else if (currentScreenState == STATE_STATIC_COLORS_MENU) {
        currentLedMode = colorMenuIndex + 1;
        EEPROM.update(LED_MODE_ADDRESS, currentLedMode);
        applyActiveColor();
        currentScreenState = STATE_NORMAL;
      } else if (currentScreenState == STATE_EFFECTS_MENU) {
        currentLedMode = colorMenuIndex + 5;
        EEPROM.update(LED_MODE_ADDRESS, currentLedMode);
        currentScreenState = STATE_NORMAL;
      } else if (currentScreenState == STATE_BRIGHTNESS_MENU) {
        EEPROM.update(BRIGHTNESS_ADDRESS, currentBrightness);
        currentScreenState = STATE_NORMAL;
      } else if (currentScreenState == STATE_LAYERS_MENU || currentScreenState == STATE_PROG_MODE) {
        currentScreenState = STATE_NORMAL;
      }
      encoderMoved = true;
    }
  } else {
    encoderSwitchPressed = false;
  }
  if (encoderMoved) updateOLED();

  //  Switch matrix logic
  if (currentScreenState != STATE_PROG_MODE) {
    for (int r = 0; r < NUM_ROWS; r++) {

      //  Sets all rows to HIGH
      for (int i = 0; i < NUM_ROWS; i++)
        digitalWrite(rowPins[i], HIGH);

      //Activates current row
      digitalWrite(rowPins[r], LOW);
      delayMicroseconds(50);

      for (int c = 0; c < NUM_COLS; c++) {
        //  Checking if any key is pressed in row
        bool isPressed = (digitalRead(colPins[c]) == LOW);
        //  KEY PRESSED
        if (isPressed && !lastKeyState[r][c]) {
          lastKeyState[r][c] = true;
          executeMacro(r, c, true);
          //  KEY RELEASED
        } else if (!isPressed && lastKeyState[r][c]) {
          lastKeyState[r][c] = false;
          executeMacro(r, c, false);
        }
      }
    }
  }

  // Pop-up screen timer
  if (isPopupActive && (millis() - popupTimer > 600)) {
    isPopupActive = false;
    if (currentScreenState == STATE_NORMAL) updateOLED();
  }

  handleLEDs();
  delay(LOOP_DELAY);
}

void executeMacro(int r, int c, bool isPress) {
  // Reaction effect trigger
  if (isPress && currentLedMode == 7) {
    int ledIndex = r * NUM_COLS + c;
    if (ledIndex < NUMPIXELS) {
      reactiveFade[ledIndex] = 255;
    }
  }

  // Ripple effect trigger
  if (isPress && currentLedMode == 8) {
    rippleOriginR = r;
    rippleOriginC = c;
    rippleStartTime = millis();
  }

  // Pop-up screen trigger - writes macro instructions on screen
  if (isPress && currentScreenState == STATE_NORMAL) {
    buildPopupText(r, c);

    isPopupActive = true;
    popupTimer = millis();
    updateOLED();
  } 

  //  If key is released it releases all keys and returns from function
  if (!isPress) {
    Keyboard.releaseAll();
    return;
  }

  KeyConfig kCfg;  // temp variable for storing EEPROM keyConfig data
  EEPROM.get((currentLayer * 12 + (r * 3 + c)) * sizeof(KeyConfig), kCfg);

  //  performs all instructions from keyConfig
  for (int i = 0; i < MAX_MACRO_STEPS; i++) {
    if (kCfg.steps[i].action == 0) break;

    if (kCfg.steps[i].action == 1) {
      pressMacroKey(kCfg.steps[i].key);

    } else if (kCfg.steps[i].action == 2) {
      releaseMacroKey(kCfg.steps[i].key);
      delay(RELEASE_DELAY);

    } else{
      Keyboard.releaseAll();
      delay(END_MACRO_DELAY);
    }

  }
}


void readUART() {
  //  Initializes buffer and read serial data
  char buffer[100];
  memset(buffer, 0, sizeof(buffer));

  int len = Serial.readBytesUntil('\n', buffer, sizeof(buffer) - 1);
  if (len == 0) return;

  if (buffer[len - 1] == '\r') {
    buffer[len - 1] = '\0';
    len--;
  }

  //  Prints EEPROM data if asked
  if (len == 1 && buffer[0] == '?') {
    showEEPROM();
    return;
  }

  if (len < 5) return;

  //  Reads macro command
  if (buffer[0] == '<' && buffer[1] == '<' && buffer[len - 2] == '>' && buffer[len - 1] == '>') {
    buffer[len - 2] = '\0';
    char* content = &buffer[2];

    char* layerStr = strtok(content, ",");
    char* idStr = strtok(NULL, ",");
    char* labelStr = strtok(NULL, ",");
    char* macrosStr = strtok(NULL, "");

    if (!layerStr || !idStr || !labelStr || !macrosStr) {
      Serial.println(F("ERR1"));
      return;
    }

    int targetLyr = atoi(layerStr);
    int targetKeyID = atoi(idStr);

    KeyConfig newKey;
    memset(&newKey, 0, sizeof(KeyConfig));

    strncpy(newKey.label, labelStr, 5);
    newKey.label[5] = '\0';

    int stepCounter = 0;
    char* ptr = macrosStr;

    while (*ptr != '\0' && stepCounter < MAX_MACRO_STEPS) {
      if (*ptr == '[') {
        ptr++;
        char* endBracket = strchr(ptr, ']');
        if (!endBracket) break;

        *endBracket = '\0';

        byte actionCode = ptr[0] - '0';
        byte keyCode = 0;

        if (actionCode == 3) {
          keyCode = 0;
        } else {
          char* keyPart = &ptr[1];

          if (!parseMacroKey(keyPart, keyCode)) {
            Serial.println(F("ERR2"));
            return;
          }
        }

        newKey.steps[stepCounter].action = actionCode;
        newKey.steps[stepCounter].key = keyCode;
        stepCounter++;

        ptr = endBracket + 1;
      } else {
        ptr++;
      }
    }

    int eeAddress = (targetLyr * 12 + targetKeyID) * sizeof(KeyConfig);
    EEPROM.put(eeAddress, newKey);

    Serial.println(F("PROG: OK!"));
    loadConfigFromEEPROM();
    updateOLED();
  }

  //  Reads layer command
  else if (buffer[0] == '{' && buffer[1] == '{' && buffer[len - 2] == '}' && buffer[len - 1] == '}') {
    buffer[len - 2] = '\0';
    char* content = &buffer[2];

    char* idStr = strtok(content, ",");
    char* nameStr = strtok(NULL, "");

    if (idStr && nameStr) {
      int layerID = atoi(idStr);
      if (layerID >= 0 && layerID < MAX_LAYERS) {
        strncpy(layerNames[layerID], nameStr, 14);
        layerNames[layerID][14] = '\0';
        EEPROM.put(LAYER_LABELS_ADDRESS + (layerID * 15), layerNames[layerID]);

        Serial.println(F("PROG:OK!"));
        updateOLED();
      }
    }
  }
}

//  Translates modifier codes to ASCII and prints them to serial monitor 
void printKeyName(byte keyCode) {
  if (keyCode == 0) return;

  if (keyCode == MOD_CTRL) Serial.print(F("CTRL"));
  else if (keyCode == MOD_SHIFT) Serial.print(F("SHIFT"));
  else if (keyCode == MOD_ALT) Serial.print(F("ALT"));
  else if (keyCode == MOD_GUI) Serial.print(F("GUI"));
  else if (keyCode == MOD_ENTER) Serial.print(F("ENTER"));
  else if (keyCode == MOD_TAB) Serial.print(F("TAB"));
  else if (keyCode >= MOD_F1 && keyCode <= MOD_F12) {
    Serial.print(F("F"));
    Serial.print(keyCode - MOD_F1 + 1);
  }
  else if (keyCode >= 32 && keyCode <= 126) Serial.print((char)keyCode);
  else {
    Serial.print(F("CODE:"));
    Serial.print(keyCode);
  }
}

//  Prints all key configs from EEPROM to serial monitor
void showEEPROM() {
  Serial.println(F("--- EEPROM CONFIG ---"));
  for (int l = 0; l < MAX_LAYERS; l++) {
    Serial.print(F("\nLAYER "));
    Serial.println(l);
    for (int i = 0; i < 12; i++) {
      KeyConfig k;
      EEPROM.get((l * 12 + i) * sizeof(KeyConfig), k);
      Serial.print(F(" Key "));
      Serial.print(i);
      Serial.print(F(" ["));
      Serial.print(k.label);
      Serial.print(F("] -> "));

      for (int s = 0; s < MAX_MACRO_STEPS; s++) {
        if (k.steps[s].action == 0) break;

        if (k.steps[s].action == 1) Serial.print(F("PRESS "));
        else if (k.steps[s].action == 2) Serial.print(F("REL "));

        printKeyName(k.steps[s].key);
        Serial.print(F("  "));
      }
      Serial.println();
    }
  }
  Serial.println(F("--- END ---"));
}


void loadConfigFromEEPROM() {
  for (int l = 0; l < MAX_LAYERS; l++) {
    for (int r = 0; r < NUM_ROWS; r++) {
      for (int c = 0; c < NUM_COLS; c++) {

        int keyID = r * NUM_COLS + c;
        int eeAddress = (l * NUM_ROWS * NUM_COLS + keyID) * sizeof(KeyConfig);

        KeyConfig tempKey;
        EEPROM.get(eeAddress, tempKey);

        if (tempKey.label[0] == 0 || tempKey.label[0] == 255) {
          strcpy(dynamicLabels[l][r][c], "EMPTY");
        } else {
          strncpy(dynamicLabels[l][r][c], tempKey.label, 5);
          dynamicLabels[l][r][c][5] = '\0';
        }
      }
    }
  }
}

//  formats the EEPROM with default keyConfig values, executes only if EEPROM_SIGNATURE isnt found on EEPROM_SIGNATURE_ADDRESS
void writeDefaultConfigEEPROM() {
  KeyConfig defaultKey;

  memset(&defaultKey, 0, sizeof(KeyConfig));
  strcpy(defaultKey.label, "COMM");
  defaultKey.steps[0] = { 1, MOD_CTRL };
  defaultKey.steps[1] = { 1, 'k' };
  defaultKey.steps[2] = { 2, 'k' };
  defaultKey.steps[3] = { 1, 'c' };
  defaultKey.steps[4] = { 3, 0 };
  EEPROM.put(0, defaultKey);

  memset(&defaultKey, 0, sizeof(KeyConfig));
  strcpy(defaultKey.label, "UNCM");
  defaultKey.steps[0] = { 1, MOD_CTRL };
  defaultKey.steps[1] = { 1, 'k' };
  defaultKey.steps[2] = { 2, 'k' };
  defaultKey.steps[3] = { 1, 'u' };
  defaultKey.steps[4] = { 3, 0 };
  EEPROM.put(sizeof(KeyConfig), defaultKey);

  memset(&defaultKey, 0, sizeof(KeyConfig));
  strcpy(defaultKey.label, "EMPTY");
  for (int i = 2; i < (NUM_ROWS * NUM_COLS * MAX_LAYERS); i++) {
    EEPROM.put(i * sizeof(KeyConfig), defaultKey);
  }

  EEPROM.write(1020, 44);
}


//  handles drawing every menu
void drawMenu(const __FlashStringHelper* title, int selectedIndex, int& offset, int itemCount, const void* items, bool isDynamic) {
  display.setCursor(0, 0);
  display.println(title);
  display.drawFastHLine(0, 10, 128, SSD1306_WHITE);

  if (selectedIndex < offset) offset = selectedIndex;
  else if (selectedIndex >= offset + MAX_VISIBLE_ITEMS) offset = selectedIndex - MAX_VISIBLE_ITEMS + 1;

  for (int i = offset; i < offset + MAX_VISIBLE_ITEMS && i < itemCount; i++) {
    int yPos = 16 + ((i - offset) * 12);
    display.fillRect(0, yPos - 2, (i == selectedIndex ? 118 : 0), 11, SSD1306_WHITE);
    display.setTextColor(i == selectedIndex ? SSD1306_BLACK : SSD1306_WHITE);
    display.setCursor(4, yPos);

    if (isDynamic) display.print(((char(*)[15])items)[i]);
    else display.print(((const char**)items)[i]);
  }

  display.setTextColor(SSD1306_WHITE);
  if (offset > 0) {
    display.setCursor(120, 16);
    display.print('^');
  }
  if (offset + MAX_VISIBLE_ITEMS < itemCount) {
    display.setCursor(120, 52);
    display.print('v');
  }
}


//  handles OLED screen
void updateOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  switch (currentScreenState) {
    case STATE_NORMAL:
      if (isPopupActive) {

        int textLen = strlen(popupText);

        if (textLen > 9) display.setTextSize(1);
        else display.setTextSize(2);

        display.setTextColor(SSD1306_WHITE);


        int charWidth = (textLen > 9) ? 6 : 12;
        int textX = 64 - ((textLen * charWidth) / 2);
        int textY = (textLen > 9) ? 28 : 24;

        display.setCursor(textX, textY);
        display.print(popupText);

        display.setTextSize(1);
        display.setTextColor(SSD1306_WHITE);
      } else {

        for (int r = 0; r < NUM_ROWS; r++) {
          for (int c = 0; c < NUM_COLS; c++) {
            int x = c * 42;
            int y = r * 11;

            display.drawRect(x, y, 41, 10, SSD1306_WHITE);
            display.setTextColor(SSD1306_WHITE);

            display.setCursor(x + 5, y + 1);
            display.print(dynamicLabels[currentLayer][r][c]);
          }
        }
        display.drawFastHLine(0, 46, 128, SSD1306_WHITE);
        display.setCursor(0, 52);
        display.print(F("LYR:"));
        display.print(layerNames[currentLayer]);
      }
      break;

    case STATE_MAIN_MENU: drawMenu(F("MAIN MENU"), mainMenuIndex, screenOffset, 3, (const void*)mainMenuItems, false); break;
    case STATE_LAYERS_MENU: drawMenu(F("SELECT LAYER"), currentLayer, screenOffset, MAX_LAYERS, (const void*)layerNames, true); break;
    case STATE_LEDS_MENU: drawMenu(F("LED OPTIONS"), colorMenuIndex, screenOffset, numLedsMenuOptions, (const void*)ledMenuOptions, false); break;
    case STATE_STATIC_COLORS_MENU: drawMenu(F("STATIC COLORS"), colorMenuIndex, screenOffset, numStaticColors, (const void*)staticColorOptions, false); break;
    case STATE_EFFECTS_MENU: drawMenu(F("LED EFFECTS"), colorMenuIndex, screenOffset, numEffects, (const void*)effectOptions, false); break;

    case STATE_BRIGHTNESS_MENU:
      display.setCursor(0, 0);
      display.println(F("BRIGHTNESS"));
      display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
      display.setCursor(0, 25);
      display.print(F("Value: "));
      display.print(currentBrightness);
      display.drawRect(10, 45, 108, 10, SSD1306_WHITE);
      display.fillRect(10, 45, (currentBrightness * 108) / 150, 10, SSD1306_WHITE);
      break;

    case STATE_PROG_MODE:
      display.setCursor(0, 0);
      display.println(F("PROG MODE"));
      display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
      display.setCursor(0, 20);
      display.println(F("Send UART package"));
      display.setCursor(0, 33);
      display.println(F("KEY: <<...>>"));
      display.setCursor(0, 46);
      display.println(F("LAYER NAME: {{...}}"));
      break;
  }
  display.display();
}


//  applies only static colors
void applyActiveColor() {
  if (currentLedMode > 4) return;
  int colorIdx = currentLedMode - 1;
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(staticColors[colorIdx].r, staticColors[colorIdx].g, staticColors[colorIdx].b));
  }
  pixels.show();
}

//  handles LED effects
void handleLEDs() {
  if (currentLedMode < 5) return;
  if (millis() - lastLedUpdate < 15) return;
  lastLedUpdate = millis();

  if (currentLedMode == 5) rainbowCycleEffect();
  else if (currentLedMode == 6) breatheEffect();
  else if (currentLedMode == 7) reactionEffect();
  else if (currentLedMode == 8) rippleEffect();

  pixels.show();
}


//  provides smooth, mathematically calculated transitions across the color spectrum 
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85) return pixels.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  if (WheelPos < 170) {
    WheelPos -= 85;
    return pixels.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return pixels.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

//  Effects
void rainbowCycleEffect() {
  for (int i = 0; i < NUMPIXELS; i++) pixels.setPixelColor(i, Wheel(((i * 256 / NUMPIXELS) + rainbowCycle) & 255));
  rainbowCycle = (rainbowCycle + 1) % 1280;
}

void breatheEffect() {
  for (int i = 0; i < NUMPIXELS; i++) pixels.setPixelColor(i, pixels.Color(0, breatheVal, breatheVal / 2));
  breatheVal += breatheDir;
  if (breatheVal >= 250 || breatheVal <= 10) breatheDir = -breatheDir;
}

void reactionEffect() {
  static unsigned long lastReactionUpdate = 0;
  if (millis() - lastReactionUpdate < 15) return;
  lastReactionUpdate = millis();

  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {

      int logicalIndex = r * NUM_COLS + c;
      int physicalIndex;

      if (r % 2 == 0) {
        physicalIndex = logicalIndex;
      } else {
        physicalIndex = (r * NUM_COLS) + (2 - c);
      }

      if (reactiveFade[logicalIndex] > 0) {
        pixels.setPixelColor(physicalIndex, pixels.Color(0, reactiveFade[logicalIndex], reactiveFade[logicalIndex]));

        if (reactiveFade[logicalIndex] > 10) reactiveFade[logicalIndex] -= 10;
        else reactiveFade[logicalIndex] = 0;
      } else {
        pixels.setPixelColor(physicalIndex, 0);
      }
    }
  }
}

void rippleEffect() {
  uint8_t baseWhite = 40;

  if (rippleStartTime == 0) {
    for (int i = 0; i < NUMPIXELS; i++) {
      pixels.setPixelColor(i, pixels.Color(baseWhite, baseWhite, baseWhite));
    }
    return;
  }

  unsigned long elapsed = millis() - rippleStartTime;

  int spreadDelay = 80;
  int fadeDuration = 700;

  if (elapsed > (5 * spreadDelay + fadeDuration)) {
    rippleStartTime = 0;
    return;
  }

  for (int visualR = 0; visualR < NUM_ROWS; visualR++) {
    for (int visualC = 0; visualC < NUM_COLS; visualC++) {

      int ledIndex;
      if (visualR % 2 == 0) ledIndex = (visualR * NUM_COLS) + visualC;
      else ledIndex = (visualR * NUM_COLS) + (2 - visualC);

      uint8_t r = baseWhite;
      uint8_t g = baseWhite;
      uint8_t b = baseWhite;

      if (visualR == rippleOriginR || visualC == rippleOriginC) {

        int dist = abs(visualR - rippleOriginR) + abs(visualC - rippleOriginC);
        int timeToReach = dist * spreadDelay;

        if (elapsed >= timeToReach) {
          int timeSinceReached = elapsed - timeToReach;

          if (timeSinceReached < fadeDuration) {

            int fadeProgress = (timeSinceReached * 255) / fadeDuration;

            r = (fadeProgress * baseWhite) / 255;
            g = (fadeProgress * baseWhite) / 255;
            b = 255 - ((fadeProgress * (255 - baseWhite)) / 255);
          }
        }
      }
      pixels.setPixelColor(ledIndex, pixels.Color(r, g, b));
    }
  }
}


//  forms ASCII pop-up text from stored EEPROM macro  
void buildPopupText(int r, int c) {
  KeyConfig kCfg;
  EEPROM.get((currentLayer * 12 + (r * 3 + c)) * sizeof(KeyConfig), kCfg);

  int pos = 0;
  bool first = true;

  for (int i = 0; i < MAX_MACRO_STEPS; i++) {
    if (kCfg.steps[i].action == 0) break;

    if (kCfg.steps[i].action == 1) {
      if (!first && pos < 18) popupText[pos++] = '+';
      first = false;

      byte key = kCfg.steps[i].key;
      const char* modStr = nullptr;

      if (key == MOD_CTRL) modStr = "CTRL";
      else if (key == MOD_SHIFT) modStr = "SHFT";
      else if (key == MOD_ALT) modStr = "ALT";
      else if (key == MOD_GUI) modStr = "WIN";
      else if (key == MOD_ENTER) modStr = "ENT";
      else if (key == MOD_TAB) modStr = "TAB";

      if (modStr) {
        while (*modStr && pos < 18) popupText[pos++] = *modStr++;
      } else if (key >= MOD_F1 && key <= MOD_F12) {
        byte fNum = key - MOD_F1 + 1;
        if (pos < 18) popupText[pos++] = 'F';
        if (fNum >= 10 && pos < 18) popupText[pos++] = '1';
        if (pos < 18) popupText[pos++] = (fNum >= 10) ? ('0' + (fNum - 10)) : ('0' + fNum);
      } else if (key >= 32 && key <= 126) {
        if (key >= 'a' && key <= 'z') key -= 32;
        if (pos < 18) popupText[pos++] = (char)key;
      }
    }
  }

  popupText[pos] = '\0';
  if (pos == 0) strcpy(popupText, "EMPTY");
}