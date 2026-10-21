#include <HID-Project.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

#define SERIAL_BAUD 9600

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDR 0x3C
#define OLED_RESET -1
#define MAX_VISIBLE_ITEMS 4

#define NUM_ROWS 4
#define NUM_COLS 3
#define MAX_LAYERS 3
#define MAX_MACRO_STEPS 10

#define EEPROM_SIGNATURE_ADDRESS 1020
#define EEPROM_SIGNATURE 44
#define LAYER_LABELS_ADDRESS 950

#define LED_PIN 5
#define NUMPIXELS 12

#define MOD_CTRL 1
#define MOD_SHIFT 2
#define MOD_ALT 3
#define MOD_GUI 4

#define STATE_NORMAL 0
#define STATE_MAIN_MENU 1
#define STATE_LAYERS_MENU 2
#define STATE_LEDS_MENU 3
#define STATE_PROG_MODE 4
#define STATE_BRIGHTNESS_MENU 5
#define STATE_STATIC_COLORS_MENU 6
#define STATE_EFFECTS_MENU 7

#define LED_MODE_ADDRESS 1021
#define BRIGHTNESS_ADDRESS 1022

#define ENC_A A5
#define ENC_B A4
#define ENC_SW A3

#define numMainMenuItems 3
#define numLedsMenuOptions 3
#define numStaticColors 4
#define numEffects 5

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

int rowPins[NUM_ROWS] = { 0, 1, 30, 12 };
int colPins[NUM_COLS] = { 8, 9, 10 };

int currentScreenState = STATE_NORMAL;
int menuIndex = 0;
int colorMenuIndex = 0;
int currentLayer = 0;
int lastEncoderStateA;
bool encoderSwitchPressed = false;
int currentBrightness = 70;
int currentLedMode = 1;


const char* mainMenuItems[numMainMenuItems] = { "1. LAYER SELECT", "2. LED COLOR", "3. UART PROG" };
const char* ledMenuOptions[numLedsMenuOptions] = { "1. BRIGHTNESS", "2. STATIC COLORS", "3. EFFECTS" };
const char* staticColorOptions[numStaticColors] = { "1. CYAN", "2. GREEN", "3. RED", "4. PURPLE" };
const char* effectOptions[numEffects] = { "1. RAINBOW", "2. BREATHE", "3. FIRE", "4. REACTION", "5. RIPPLE"};

int colorMenuOffset = 0;
int dummyOffset = 0;

struct RGB {
  byte r;
  byte g;
  byte b;
};
RGB staticColors[4] = { { 0, 255, 100 }, { 0, 255, 0 }, { 250, 0, 0 }, { 150, 0, 255 } };

char layerNames[MAX_LAYERS][15] = { "L1", "L2", "L3" };
char dynamicLabels[MAX_LAYERS][NUM_ROWS][NUM_COLS][6];

struct __attribute__((packed)) MacroStep {
  byte action;
  byte key;
};

struct __attribute__((packed)) KeyConfig {
  char label[6];
  MacroStep steps[MAX_MACRO_STEPS];
};

bool lastKeyState[NUM_ROWS][NUM_COLS];

//  Effect variables
unsigned long lastLedUpdate = 0;
uint16_t rainbowCycle = 0;
uint8_t breatheVal = 10;
int8_t breatheDir = 5;
uint8_t theater_j = 0;
uint8_t theater_q = 0;
uint8_t reactiveFade[NUMPIXELS] = {0};
// Ripple effect 
uint8_t rippleOriginR = 0;
uint8_t rippleOriginC = 0;
unsigned long rippleStartTime = 0;


void handleLEDs();
uint32_t Wheel(byte WheelPos);
void updateOLED();
void applyActiveColor();
void readUART();
void executeMacro(int r, int c, bool isPress);
void loadConfigFromEEPROM();
void writeDefaultConfigEEPROM();


void setup() {
  delay(1000);
  pixels.begin();

  currentLedMode = EEPROM.read(LED_MODE_ADDRESS);
  if (currentLedMode == 0 || currentLedMode > 9) currentLedMode = 1;

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

  //  Encoder logic (Adjusting volume in normal state, navigation on menus)
  bool encoderMoved = false;
  int currentEncoderStateA = digitalRead(ENC_A);

  if (currentEncoderStateA != lastEncoderStateA && currentEncoderStateA == LOW) {
    int dir = (digitalRead(ENC_B) != currentEncoderStateA) ? -1 : 1;
    if (currentScreenState == STATE_MAIN_MENU || 
        currentScreenState == STATE_LAYERS_MENU || 
        currentScreenState == STATE_LEDS_MENU || 
        currentScreenState == STATE_STATIC_COLORS_MENU || 
        currentScreenState == STATE_EFFECTS_MENU) {
      dir = -dir; 
    }
    switch (currentScreenState) {
      case STATE_NORMAL:
        if (dir == 1) Consumer.write(MEDIA_VOLUME_UP);
        else Consumer.write(MEDIA_VOLUME_DOWN);
        break;
      case STATE_MAIN_MENU:
        menuIndex += dir;
        if (menuIndex < 0) menuIndex = 2;
        if (menuIndex > 2) menuIndex = 0;
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

  //  Encoder switch logic (navigation on menus)
  if (digitalRead(ENC_SW) == LOW) {
    if (!encoderSwitchPressed) {
      encoderSwitchPressed = true;

      if (currentScreenState == STATE_NORMAL) {
        currentScreenState = STATE_MAIN_MENU;
        menuIndex = 0;
      } else if (currentScreenState == STATE_MAIN_MENU) {
        if (menuIndex == 0) currentScreenState = STATE_LAYERS_MENU;
        else if (menuIndex == 1) {
          currentScreenState = STATE_LEDS_MENU;
          colorMenuIndex = 0;
        } else if (menuIndex == 2) currentScreenState = STATE_PROG_MODE;
      } else if (currentScreenState == STATE_LEDS_MENU) {
        if (colorMenuIndex == 0) currentScreenState = STATE_BRIGHTNESS_MENU;
        else if (colorMenuIndex == 1) {
          currentScreenState = STATE_STATIC_COLORS_MENU;
          colorMenuIndex = 0;
          colorMenuOffset = 0;
        } else if (colorMenuIndex == 2) {
          currentScreenState = STATE_EFFECTS_MENU;
          colorMenuIndex = 0;
          colorMenuOffset = 0;
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
        currentScreenState = STATE_LEDS_MENU;
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
      for (int i = 0; i < NUM_ROWS; i++)
        digitalWrite(rowPins[i], HIGH);  //  sets all rows to HIGH

      digitalWrite(rowPins[r], LOW);  //  sets one row to LOW (if any key in that row is pressed it connects that GND to its column
      delayMicroseconds(50);

      for (int c = 0; c < NUM_COLS; c++) {
        bool isPressed = (digitalRead(colPins[c]) == LOW);  // checking if any switch is pressed

        if (isPressed && !lastKeyState[r][c]) {
          lastKeyState[r][c] = true;
          executeMacro(r, c, true);
          updateOLED();
        } else if (!isPressed && lastKeyState[r][c]) {
          lastKeyState[r][c] = false;
          executeMacro(r, c, false);
          updateOLED();
        }
      }
    }
  }

  handleLEDs();
  delay(2);
}

void executeMacro(int r, int c, bool isPress) {
  // Ripple effect trigger
  if (isPress && currentLedMode == 9) {
    rippleOriginR = r;
    rippleOriginC = c;
    rippleStartTime = millis();
  }
  // Reaction effect trigger
  if (isPress && currentLedMode == 8) {
    int ledIndex = r * NUM_COLS + c; 
    if (ledIndex < NUMPIXELS) {
      reactiveFade[ledIndex] = 255; 
    }
  }


  if (!isPress) {
    Keyboard.releaseAll();
    return;
  }

  KeyConfig kCfg;
  EEPROM.get((currentLayer * 12 + (r * 3 + c)) * sizeof(KeyConfig), kCfg);

  for (int i = 0; i < MAX_MACRO_STEPS; i++) {
    if (kCfg.steps[i].action == 0) break;

    // Press
    if (kCfg.steps[i].action == 1) {
      if (kCfg.steps[i].key == MOD_CTRL) Keyboard.press(KEY_LEFT_CTRL);
      else if (kCfg.steps[i].key == MOD_SHIFT) Keyboard.press(KEY_LEFT_SHIFT);
      else if (kCfg.steps[i].key == MOD_ALT) Keyboard.press(KEY_LEFT_ALT);
      else if (kCfg.steps[i].key == MOD_GUI) Keyboard.press(KEY_LEFT_GUI);
      else Keyboard.press((char)kCfg.steps[i].key);
    }
    // Release
    else if (kCfg.steps[i].action == 2) {
      if (kCfg.steps[i].key == MOD_CTRL) Keyboard.release(KEY_LEFT_CTRL);
      else if (kCfg.steps[i].key == MOD_SHIFT) Keyboard.release(KEY_LEFT_SHIFT);
      else if (kCfg.steps[i].key == MOD_ALT) Keyboard.release(KEY_LEFT_ALT);
      else if (kCfg.steps[i].key == MOD_GUI) Keyboard.release(KEY_LEFT_GUI);
      else Keyboard.release((char)kCfg.steps[i].key);
    }
    // Release all
    else
      Keyboard.releaseAll();

    delay(10);
  }
}

void readUART() {
  char buffer[100];
  memset(buffer, 0, sizeof(buffer));

  int len = Serial.readBytesUntil('\n', buffer, sizeof(buffer) - 1);
  if (len == 0) return;

  if (buffer[len - 1] == '\r') {
    buffer[len - 1] = '\0';
    len--;
  }

  //  Return EEPROM table
  if (len == 1 && buffer[0] == '?') {
    showEEPROM();
    return; 
  }


  if (len < 5) return;
  //  Macro command
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

          if (strcmp(keyPart, "M1") == 0) keyCode = MOD_CTRL;
          else if (strcmp(keyPart, "M2") == 0) keyCode = MOD_SHIFT;
          else if (strcmp(keyPart, "M3") == 0) keyCode = MOD_ALT;
          else if (strcmp(keyPart, "M4") == 0) keyCode = MOD_GUI;
          else keyCode = keyPart[0];
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

  // Layer command
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


void printKeyName(byte keyCode) {
  if (keyCode == 0) return; 

  if (keyCode == MOD_CTRL) Serial.print(F("CTRL"));
  else if (keyCode == MOD_SHIFT) Serial.print(F("SHIFT"));
  else if (keyCode == MOD_ALT) Serial.print(F("ALT"));
  else if (keyCode == MOD_GUI) Serial.print(F("GUI"));
  else if (keyCode >= 32 && keyCode <= 126) Serial.print((char)keyCode);
  else { Serial.print(F("CODE:")); Serial.print(keyCode); }
}


void showEEPROM() {
  Serial.println(F("--- EEPROM CONFIG ---"));
  for (int l = 0; l < MAX_LAYERS; l++) {
    Serial.print(F("\nLAYER ")); Serial.println(l);
    for (int i = 0; i < 12; i++) {
      KeyConfig k;
      EEPROM.get((l * 12 + i) * sizeof(KeyConfig), k);
      Serial.print(F(" Key ")); Serial.print(i);
      Serial.print(F(" [")); Serial.print(k.label); Serial.print(F("] -> "));
      
      for(int s = 0; s < MAX_MACRO_STEPS; s++) {
        if(k.steps[s].action == 0) break;
        
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

//  loads key labels from EEPROM into ram
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

//  Initializes EEPROM with default keys if EEPROM_SIGNATURE is missing 
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

void updateOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  switch (currentScreenState) {
    case STATE_NORMAL:
      for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
          int x = c * 42;
          int y = r * 11;
          if (lastKeyState[r][c]) {
            display.fillRect(x, y, 41, 10, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK);
          } else {
            display.drawRect(x, y, 41, 10, SSD1306_WHITE);
            display.setTextColor(SSD1306_WHITE);
          }
          display.setCursor(x + 5, y + 1);
          display.print(dynamicLabels[currentLayer][r][c]);
        }
      }
      display.drawFastHLine(0, 46, 128, SSD1306_WHITE);
      display.setCursor(0, 52);
      display.print(F("LYR:"));
      display.print(layerNames[currentLayer]);
      break;

    case STATE_MAIN_MENU: drawMenu(F("MAIN MENU"), menuIndex, dummyOffset, 3, (const void*)mainMenuItems, false); break;
    case STATE_LAYERS_MENU: drawMenu(F("SELECT LAYER"), currentLayer, dummyOffset, MAX_LAYERS, (const void*)layerNames, true); break;
    case STATE_LEDS_MENU: drawMenu(F("LED OPTIONS"), colorMenuIndex, colorMenuOffset, numLedsMenuOptions, (const void*)ledMenuOptions, false); break;
    case STATE_STATIC_COLORS_MENU: drawMenu(F("STATIC COLORS"), colorMenuIndex, colorMenuOffset, numStaticColors, (const void*)staticColorOptions, false); break;
    case STATE_EFFECTS_MENU: drawMenu(F("LED EFFECTS"), colorMenuIndex, colorMenuOffset, numEffects, (const void*)effectOptions, false); break;

    case STATE_BRIGHTNESS_MENU:
      display.setCursor(0, 0);
      display.println(F("BRIGHTNESS"));
      display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
      display.setCursor(0, 25);
      display.print(F("Value: "));
      display.print(currentBrightness);
      display.drawRect(10, 45, 108, 10, SSD1306_WHITE);
      display.fillRect(10, 45, map(currentBrightness, 0, 150, 0, 108), 10, SSD1306_WHITE);
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

void applyActiveColor() {
  if (currentLedMode > 4) return;
  int colorIdx = currentLedMode - 1;
  for (int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(staticColors[colorIdx].r, staticColors[colorIdx].g, staticColors[colorIdx].b));
  }
  pixels.show();
}

void handleLEDs() {
  if (currentLedMode < 5) return;
  if (millis() - lastLedUpdate < 15) return;
  lastLedUpdate = millis();

  if (currentLedMode == 5) rainbowCycleEffect();
  else if (currentLedMode == 6) breatheEffect();
  else if (currentLedMode == 7) fireEffect();
  else if (currentLedMode == 8) reactionEffect(); 
  else if (currentLedMode == 9) rippleEffect(); 


  pixels.show();
}


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

void rainbowCycleEffect() {
  for (int i = 0; i < NUMPIXELS; i++) pixels.setPixelColor(i, Wheel(((i * 256 / NUMPIXELS) + rainbowCycle) & 255));
  rainbowCycle = (rainbowCycle + 1) % 1280;
}

void breatheEffect() {
  for (int i = 0; i < NUMPIXELS; i++) pixels.setPixelColor(i, pixels.Color(0, breatheVal, breatheVal / 2));
  breatheVal += breatheDir;
  if (breatheVal >= 250 || breatheVal <= 10) breatheDir = -breatheDir;
}

void fireEffect() {
  static unsigned long lastFireUpdate = 0;
  if (millis() - lastFireUpdate < 100) return; 
  
  lastFireUpdate = millis();

  for (int i = 0; i < NUMPIXELS; i++) {
    int flicker = random(0, 100); 
    if (flicker < 20)      pixels.setPixelColor(i, pixels.Color(150, 40, 0));
    else if (flicker < 50) pixels.setPixelColor(i, pixels.Color(200, 80, 0));
    else                   pixels.setPixelColor(i, pixels.Color(255, 120, 0));
  }
  pixels.show();
}

void reactionEffect() {
  static unsigned long lastReactionUpdate = 0;
  if (millis() - lastReactionUpdate < 15) return;
  lastReactionUpdate = millis();

  // Prolazimo kroz logičku mrežu (tipke)
  for (int r = 0; r < NUM_ROWS; r++) {
    for (int c = 0; c < NUM_COLS; c++) {
      
      int logicalIndex = r * NUM_COLS + c; // Indeks tipke koju si stisnuo
      int physicalIndex;
      
      // Zmijolika logika: 2. i 4. red (indeksi 1 i 3) idu naopako (2 - c)
      if (r % 2 == 0) {
        physicalIndex = logicalIndex;
      } else {
        physicalIndex = (r * NUM_COLS) + (2 - c);
      }

      // Ako je tipka aktivna, upali fizičku LEDicu i polako je gasi
      if (reactiveFade[logicalIndex] > 0) {
        pixels.setPixelColor(physicalIndex, pixels.Color(0, reactiveFade[logicalIndex], reactiveFade[logicalIndex]));
        
        if (reactiveFade[logicalIndex] > 10) reactiveFade[logicalIndex] -= 10;
        else reactiveFade[logicalIndex] = 0;
      } else {
        // Ako tipka nije aktivna, drži fizičku LEDicu ugašenom
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
      // ZIG-ZAG Logic
      if (visualR % 2 == 0) ledIndex = (visualR * NUM_COLS) + visualC;
      else                  ledIndex = (visualR * NUM_COLS) + (2 - visualC);

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
