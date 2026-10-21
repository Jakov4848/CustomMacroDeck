#include <Keyboard.h>
#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h> 

#define SERIAL_BAUD 9600 

// OLED
#define SCREEN_WIDTH 128 
#define SCREEN_HEIGHT 64 
#define SCREEN_ADDR 0x3C 
#define OLED_RESET    -1 
#define MAX_VISIBLE_ITEMS 4

//  Switch matrix
#define NUM_ROWS 4
#define NUM_COLS 3

#define MAX_LAYERS 3
#define MAX_MACRO_STEPS 10 

#define EEPROM_SIGNATURE 44

//  Led lightning
#define LED_PIN  5
#define NUMPIXELS 12

//  Modifiers
#define MOD_CTRL  1
#define MOD_SHIFT 2
#define MOD_ALT   3
#define MOD_GUI   4

//  State machine
#define STATE_NORMAL 0
#define STATE_MAIN_MENU 1
#define STATE_LAYERS_MENU 2
#define STATE_LEDS_MENU 3
#define STATE_PROG_MODE 4
#define STATE_BRIGHTNESS_MENU 5
#define STATE_STATIC_COLORS_MENU 6
#define STATE_EFFECTS_MENU 7

//  Rotary encoder
#define ENC_A A5
#define ENC_B A4
#define ENC_SW A3


Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Adafruit_NeoPixel pixels(NUMPIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

int rowPins[NUM_ROWS] = {0, 1, 30, 12};  // PD2=0, PD3=1, PD5=30, PD6=12
int colPins[NUM_COLS] = {8, 9, 10};      // PB4=8, PB5=9, PB6=10

int currentScreenState = STATE_NORMAL; 
int menuIndex = 0; 
int colorMenuIndex = 0;
int currentLayer = 0;
int lastEncoderStateA;
bool encoderSwitchPressed = false;

int currentBrightness = 70;
int currentLedMode = 1;

//  LED menu
const int numLedsMenuOptions = 3;
const char* ledMenuOptions[numLedsMenuOptions] = { 
  "1. BRIGHTNESS", 
  "2. STATIC COLORS", 
  "3. EFFECTS", 
};

// Static colors sub menu
const int numStaticColors = 4;
const char* staticColorOptions[numStaticColors] = { "1. CYAN", "2. GREEN", "3. RED", "4. PURPLE" };

// Effects sub menu
const int numEffects = 2;
const char* effectOptions[numEffects] = { "1. RAINBOW", "2. BREATHE" };

int colorMenuOffset = 0;
struct RGB { byte r; byte g; byte b; };
RGB staticColors[4] = { {0, 255, 100}, {0, 255, 0}, {250, 0, 0}, {150, 0, 255} };

const char* mainMenuItems[3] = { "1. LAYER SELECT", "2. LED COLOR", "3. UART PROG" };
const char* layerNames[MAX_LAYERS] = { "VISUAL STUDIO", "PHOTOSHOP", "GAMING" };

char dynamicLabels[MAX_LAYERS][NUM_ROWS][NUM_COLS][6]; // extracts key labels from eeprom

struct MacroStep {
  byte action; //1-Press 2-Release 3-ReleaseAll
  byte key;    
};

struct KeyConfig {
  char label[6]; 
  MacroStep steps[MAX_MACRO_STEPS];
}; // total 26bytes

// Edge Detection (remembers last key state)
bool lastKeyState[NUM_ROWS][NUM_COLS];



// Effects
unsigned long lastLedUpdate = 0;
uint16_t rainbowCycle = 0;
uint8_t breatheVal = 10;
int8_t breatheDir = 5;

void handleLEDs();
uint32_t Wheel(byte WheelPos);


//TODO
// 2.) DODATI MOGUĆNOST PROGRAMIRANJA NAZIVA LAYERA UARTOM
// 3.) DODATI FUNKCIJU ENCODERU U NORMAL SCREENU
// 4.) IZRADITI NOVI SCREEN KOJI POKAZUJE STA JE NA TIPKOVNICI PRITISNUTO KADA SE TIPKA STISNE(NPR. COMM - CTRL+K CTRL+C)
// 5.) IZRADITI TXT DATOTEKU ZA TESTIRANJE UARTA I PYTHON SKRIPTU KOJA ĆE KORISTITI TU TXT SKRIPTU DA DODA FUNKCIJE SVIM TIPKAMA U SVAKOM LAYERU


void updateOLED();  
void applyActiveColor();
void readUART();
void executeMacro(int r, int c, bool isPress);
void loadConfigFromEEPROM();  //  copies Key labels from EEPROM to RAM (dynamicLabels)
void writeDefaultConfigEEPROM(); // formats EEPROM

void setup() {
  delay(1000); 

  pixels.begin();

  // Loading LED settings from EEPROM-a
  currentLedMode = EEPROM.read(1021);
  if (currentLedMode == 0 || currentLedMode > 6) currentLedMode = 1; 

  currentBrightness = EEPROM.read(1022);
  if (currentBrightness == 0 || currentBrightness == 255) currentBrightness = 70; 
  pixels.setBrightness(currentBrightness);

  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDR);


  Serial.begin(SERIAL_BAUD); 

  // encoder pin config
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  lastEncoderStateA = digitalRead(ENC_A);

  // switch matrix config
  for (int c = 0; c < NUM_COLS; c++) 
    pinMode(colPins[c], INPUT_PULLUP);
  for (int r = 0; r < NUM_ROWS; r++) {
    pinMode(rowPins[r], OUTPUT);
    digitalWrite(rowPins[r], HIGH);
  }

  // checking eeprom signature
  if (EEPROM.read(1020) != EEPROM_SIGNATURE) {
    writeDefaultConfigEEPROM();
  }
  
  loadConfigFromEEPROM(); 
  applyActiveColor();
  Keyboard.begin();
  updateOLED();
}

void loop() {
  //  COMMUNICATION 
  if (Serial.available() > 0) {
    if (currentScreenState == STATE_PROG_MODE) {
      readUART();
    } else {
      while (Serial.available() > 0) {
        Serial.read(); 
      }
    }
  }

  //  ENCODER LOGIC 
  bool encoderMoved = false;
  int currentEncoderStateA = digitalRead(ENC_A);

  if (currentEncoderStateA != lastEncoderStateA && currentEncoderStateA == LOW) {
    int dir = (digitalRead(ENC_B) != currentEncoderStateA) ? -1 : 1;
    
    switch(currentScreenState) {
      case STATE_NORMAL: break;
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
        
        pixels.setBrightness(currentBrightness); // 1. Postavi novu granicu svjetline
        applyActiveColor(); // 2. Ponovno pošalji boju u LED-ice (ovo automatski radi i pixels.show)
        break;
    }
    encoderMoved = true;
  }
  lastEncoderStateA = currentEncoderStateA;

// ENCODER SWITCH LOGIC
  if (digitalRead(ENC_SW) == LOW) {
    if (!encoderSwitchPressed) {
      encoderSwitchPressed = true;
      
      if (currentScreenState == STATE_NORMAL) {
        currentScreenState = STATE_MAIN_MENU; 
        menuIndex = 0;
      } 
      else if (currentScreenState == STATE_MAIN_MENU) {
        if (menuIndex == 0) currentScreenState = STATE_LAYERS_MENU;
        else if (menuIndex == 1) { currentScreenState = STATE_LEDS_MENU; colorMenuIndex = 0; }
        else if (menuIndex == 2) currentScreenState = STATE_PROG_MODE;
      } 
      else if (currentScreenState == STATE_LEDS_MENU) {
        if (colorMenuIndex == 0) {
          currentScreenState = STATE_BRIGHTNESS_MENU; 
        } else if (colorMenuIndex == 1) {
          currentScreenState = STATE_STATIC_COLORS_MENU;
          colorMenuIndex = 0; colorMenuOffset = 0; // Resetiraj kursor za novi meni
        } else if (colorMenuIndex == 2) {
          currentScreenState = STATE_EFFECTS_MENU;
          colorMenuIndex = 0; colorMenuOffset = 0; // Resetiraj kursor za novi meni
        }
      }
      else if (currentScreenState == STATE_STATIC_COLORS_MENU) {
        currentLedMode = colorMenuIndex + 1; // Modovi od 1 do 4
        EEPROM.update(1021, currentLedMode);
        applyActiveColor(); 
        currentScreenState = STATE_NORMAL; 
      }
      else if (currentScreenState == STATE_EFFECTS_MENU) {
        currentLedMode = colorMenuIndex + 5; // Modovi od 5 na dalje
        EEPROM.update(1021, currentLedMode);
        // Ovdje kasnije dodaješ logiku za paljenje efekata, zasada samo sprema mod
        currentScreenState = STATE_NORMAL; 
      }
      else if (currentScreenState == STATE_BRIGHTNESS_MENU) {
        EEPROM.update(1022, currentBrightness); // Spremi svjetlinu
        currentScreenState = STATE_LEDS_MENU;   // Vrati se korak nazad
      }
      else if (currentScreenState == STATE_LAYERS_MENU || currentScreenState == STATE_PROG_MODE) {
        currentScreenState = STATE_NORMAL; 
      }
      encoderMoved = true;
    }
  } else {
    encoderSwitchPressed = false;
  }

  if (encoderMoved) {
    updateOLED();
  }

  //  MATRIX LOGIC (scanning the keys)
  if (currentScreenState != STATE_PROG_MODE) {
    for (int r = 0; r < NUM_ROWS; r++) {
      for (int i = 0; i < NUM_ROWS; i++) digitalWrite(rowPins[i], HIGH); // turn off rows
      digitalWrite(rowPins[r], LOW);  // turn on current row
      delayMicroseconds(50);

      for (int c = 0; c < NUM_COLS; c++) {
        bool isPressed = (digitalRead(colPins[c]) == LOW);


        // KEY PRESSED
        if (isPressed && !lastKeyState[r][c]) {
          lastKeyState[r][c] = true;

          
          executeMacro(r, c, true); 
          updateOLED();
        } 
        // KEY RELEASED
        else if (!isPressed && lastKeyState[r][c]) {
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

  if (!isPress) {
    Keyboard.releaseAll();
    return;
  }

  //  Calculating EEPROM address
  int keyID = r * NUM_COLS + c;
  int eepromAddress = (currentLayer * NUM_ROWS * NUM_COLS + keyID) * sizeof(KeyConfig);

  //  retreiving keyConfig from eeprom
  KeyConfig currentKey;
  EEPROM.get(eepromAddress, currentKey);

  // executing every macro step
  for (int i = 0; i < MAX_MACRO_STEPS; i++) {
    byte act = currentKey.steps[i].action;
    byte k = currentKey.steps[i].key;

    if (act == 0 && k == 0) break; 

    uint8_t targetKey = k;
    if (k == MOD_CTRL)  targetKey = KEY_LEFT_CTRL;
    if (k == MOD_SHIFT) targetKey = KEY_LEFT_SHIFT;
    if (k == MOD_ALT)   targetKey = KEY_LEFT_ALT;
    if (k == MOD_GUI)   targetKey = KEY_LEFT_GUI;

    if (act == 1) { 
      Keyboard.press(targetKey);
      delay(15); 
    }
    else if (act == 2) { 
      Keyboard.release(targetKey);
      delay(15);
    }
    else if (act == 3) { 
      Keyboard.releaseAll();
      delay(15);
    }
  }
}


void readUART() {
  String input = Serial.readStringUntil('\n');
  input.trim();
  
  if (input.startsWith("<<") && input.endsWith(">>")) {
    input = input.substring(2, input.length() - 2); 
    
    int firstComma = input.indexOf(',');
    int targetLyr = input.substring(0, firstComma).toInt();
    input = input.substring(firstComma + 1);
    
    int secondComma = input.indexOf(',');
    int targetKeyID = input.substring(0, secondComma).toInt();
    input = input.substring(secondComma + 1);
    
    int thirdComma = input.indexOf(',');
    String labelText = input.substring(0, thirdComma);
    input = input.substring(thirdComma + 1);
    
    KeyConfig newKey;
    memset(&newKey, 0, sizeof(KeyConfig));
    
    labelText.substring(0, 5).toCharArray(newKey.label, 6);
    
    int stepCounter = 0;
    while (input.indexOf('[') >= 0 && stepCounter < MAX_MACRO_STEPS) {
      int openBracket = input.indexOf('[');
      int closeBracket = input.indexOf(']');
      String stepStr = input.substring(openBracket + 1, closeBracket);
      input = input.substring(closeBracket + 1);
      
      byte actionCode = 0;
      byte keyCode = 0;
      
      if (stepStr == "3") { 
        actionCode = 3;
        keyCode = 0;
      } else {
        actionCode = stepStr.substring(0, 1).toInt(); 
        String keyPart = stepStr.substring(1);
        
        if (keyPart == "M1") keyCode = MOD_CTRL;
        else if (keyPart == "M2") keyCode = MOD_SHIFT;
        else if (keyPart == "M3") keyCode = MOD_ALT;
        else if (keyPart == "M4") keyCode = MOD_GUI;
        else {
          keyCode = keyPart.charAt(0); 
        }
      }
      
      newKey.steps[stepCounter].action = actionCode;
      newKey.steps[stepCounter].key = keyCode;
      stepCounter++;
    }
    
    int eeAddress = (targetLyr * NUM_ROWS * NUM_COLS + targetKeyID) * sizeof(KeyConfig);
    EEPROM.put(eeAddress, newKey);
    
    Serial.println("PROG: Succesfully stored in EEPROM!");
    
    loadConfigFromEEPROM(); 
    currentScreenState = STATE_NORMAL;
    updateOLED();
  }
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


void writeDefaultConfigEEPROM() {
  KeyConfig defaultKey;
  
  memset(&defaultKey, 0, sizeof(KeyConfig));
  strcpy(defaultKey.label, "COMM");
  defaultKey.steps[0] = {1, MOD_CTRL}; 
  defaultKey.steps[1] = {1, 'k'};      
  defaultKey.steps[2] = {2, 'k'};      
  defaultKey.steps[3] = {1, 'c'};      
  defaultKey.steps[4] = {3, 0};        
  EEPROM.put(0, defaultKey);           

  memset(&defaultKey, 0, sizeof(KeyConfig));
  strcpy(defaultKey.label, "UNCM");
  defaultKey.steps[0] = {1, MOD_CTRL};
  defaultKey.steps[1] = {1, 'k'};
  defaultKey.steps[2] = {2, 'k'};
  defaultKey.steps[3] = {1, 'u'};
  defaultKey.steps[4] = {3, 0};
  EEPROM.put(sizeof(KeyConfig), defaultKey); 

  memset(&defaultKey, 0, sizeof(KeyConfig));
  strcpy(defaultKey.label, "EMPTY");
  for(int i = 2; i < (NUM_ROWS * NUM_COLS * MAX_LAYERS); i++) {
    EEPROM.put(i * sizeof(KeyConfig), defaultKey);
  }

  EEPROM.write(1020, 44); 
}

void updateOLED() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  switch(currentScreenState) {
    
    case STATE_NORMAL: 
      for (int r = 0; r < NUM_ROWS; r++) {
        for (int c = 0; c < NUM_COLS; c++) {
          int x = c * 42; int y = r * 11;
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
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 52);
      display.print("LYR: "); display.print(layerNames[currentLayer]);
      break;

    case STATE_MAIN_MENU: 
      display.setCursor(0, 0);
      display.println("MAIN MENU");
      display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
      
      for (int i = 0; i < 3; i++) {
        int yPos = 16 + (i * 12);
        if (i == menuIndex) {
          display.fillRect(0, yPos - 2, 128, 11, SSD1306_WHITE);
          display.setTextColor(SSD1306_BLACK);
        } else {
          display.setTextColor(SSD1306_WHITE);
        }
        display.setCursor(4, yPos);
        display.println(mainMenuItems[i]);
      }
      break;

    case STATE_LAYERS_MENU: 
      display.setCursor(0, 0);
      display.println("SELECT LAYER");
      display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
      
      for(int i=0; i<MAX_LAYERS; i++) {
        int yPos = 16 + (i * 12);
        if(i == currentLayer) {
          display.fillRect(0, yPos-2, 128, 11, SSD1306_WHITE);
          display.setTextColor(SSD1306_BLACK);
        } else {
          display.setTextColor(SSD1306_WHITE);
        }
        display.setCursor(4, yPos);
        display.print(layerNames[i]);
      }
      break;

    case STATE_LEDS_MENU: 
      display.setCursor(0, 0);
      display.println("LED OPTIONS");
      display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
      
      // Crtanje opcija
      for (int i = 0; i < numLedsMenuOptions ; i++) {
        int yPos = 16 + (i * 12);
        
        if (i == colorMenuIndex) {
          display.fillRect(0, yPos - 2, 118, 11, SSD1306_WHITE);
          display.setTextColor(SSD1306_BLACK);
        } else {
          display.setTextColor(SSD1306_WHITE);
        }
        display.setCursor(4, yPos);
        display.print(ledMenuOptions[i]);
      }

      // Strelice za navigaciju
      display.setTextColor(SSD1306_WHITE);
      if (colorMenuOffset > 0) { display.setCursor(120, 16); display.print("^"); }
      if (colorMenuOffset + MAX_VISIBLE_ITEMS < numLedsMenuOptions) { display.setCursor(120, 52); display.print("v"); }
      break;

    case STATE_STATIC_COLORS_MENU: 
      display.setCursor(0, 0);
      display.println("STATIC COLORS");
      display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
      
      if (colorMenuIndex < colorMenuOffset) colorMenuOffset = colorMenuIndex;
      else if (colorMenuIndex >= colorMenuOffset + MAX_VISIBLE_ITEMS) {
        colorMenuOffset = colorMenuIndex - MAX_VISIBLE_ITEMS + 1;
      }

      for (int i = colorMenuOffset; i < colorMenuOffset + MAX_VISIBLE_ITEMS && i < numStaticColors; i++) {
        int visualIndex = i - colorMenuOffset; 
        int yPos = 16 + (visualIndex * 12);
        
        if (i == colorMenuIndex) {
          display.fillRect(0, yPos - 2, 118, 11, SSD1306_WHITE);
          display.setTextColor(SSD1306_BLACK);
        } else {
          display.setTextColor(SSD1306_WHITE);
        }
        display.setCursor(4, yPos);
        display.print(staticColorOptions[i]);
      }

      display.setTextColor(SSD1306_WHITE);
      if (colorMenuOffset > 0) { display.setCursor(120, 16); display.print("^"); }
      if (colorMenuOffset + MAX_VISIBLE_ITEMS < numStaticColors) { display.setCursor(120, 52); display.print("v"); }
      break;

    case STATE_EFFECTS_MENU: 
      display.setCursor(0, 0);
      display.println("LED EFFECTS");
      display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
      
      if (colorMenuIndex < colorMenuOffset) colorMenuOffset = colorMenuIndex;
      else if (colorMenuIndex >= colorMenuOffset + MAX_VISIBLE_ITEMS) {
        colorMenuOffset = colorMenuIndex - MAX_VISIBLE_ITEMS + 1;
      }

      for (int i = colorMenuOffset; i < colorMenuOffset + MAX_VISIBLE_ITEMS && i < numEffects; i++) {
        int visualIndex = i - colorMenuOffset; 
        int yPos = 16 + (visualIndex * 12);
        
        if (i == colorMenuIndex) {
          display.fillRect(0, yPos - 2, 118, 11, SSD1306_WHITE);
          display.setTextColor(SSD1306_BLACK);
        } else {
          display.setTextColor(SSD1306_WHITE);
        }
        display.setCursor(4, yPos);
        display.print(effectOptions[i]);
      }

      display.setTextColor(SSD1306_WHITE);
      if (colorMenuOffset > 0) { display.setCursor(120, 16); display.print("^"); }
      if (colorMenuOffset + MAX_VISIBLE_ITEMS < numEffects) { display.setCursor(120, 52); display.print("v"); }
      break;

    case STATE_BRIGHTNESS_MENU:
      display.setCursor(0, 0);
      display.println("ADJUST BRIGHTNESS");
      display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
      
      display.setCursor(0, 25);
      display.print("Value: ");
      display.print(currentBrightness);
      
      // Iscrtavanje okvira
      display.drawRect(10, 45, 108, 10, SSD1306_WHITE);
      // Iscrtavanje unutrašnjosti popunjene sukladno trenutnoj vrijednosti
      display.fillRect(10, 45, map(currentBrightness, 0, 150, 0, 108), 10, SSD1306_WHITE);
      break;

    case STATE_PROG_MODE: 
      display.setCursor(0, 0);
      display.println("UART PROG MODE");
      display.drawFastHLine(0, 10, 128, SSD1306_WHITE);

      display.setCursor(0, 20);
      display.println("Send UART package");
      display.setCursor(0, 33);
      display.println("Syntax: <<...>>");

      break;
  }

  display.display();
}


void applyActiveColor() {
  if (currentLedMode > 4) return; // NOVO: Ako je odabran efekt (5 ili 6), preskoči statične boje
  int colorIdx = currentLedMode - 1; 
  for(int i = 0; i < NUMPIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(staticColors[colorIdx].r, staticColors[colorIdx].g, staticColors[colorIdx].b));
  }
  pixels.show();
}
  
// ==========================================
// POZADINSKE ANIMACIJE ZA LEDICE
// ==========================================
void handleLEDs() {
  if (currentLedMode < 5) return; // Vrti se samo ako smo u EFFECTS meniju (modovi 5 i 6)

  // Vrti animacije brzinom od 50 FPS-a (svakih 20ms) bez blokiranja tipkovnice
  if (millis() - lastLedUpdate < 20) return; 
  lastLedUpdate = millis();

  for(int i = 0; i < NUMPIXELS; i++) {
    if (currentLedMode == 5) {
      // 1. EFEKT: RAINBOW (Duga)
      pixels.setPixelColor(i, Wheel(((i * 256 / NUMPIXELS) + rainbowCycle) & 255));
    }
    else if (currentLedMode == 6) {
      // 2. EFEKT: BREATHE (Cyan Disanje)
      pixels.setPixelColor(i, pixels.Color(0, breatheVal, breatheVal / 2));
    }
  }

  // Matematika za pomicanje animacije u idućem ciklusu
  if (currentLedMode == 5) {
    rainbowCycle = (rainbowCycle + 3) % 256;
  } 
  else if (currentLedMode == 6) {
    breatheVal += breatheDir;
    // Ograničavanje disanja između minimuma i maksimuma
    if (breatheVal >= 250 || breatheVal <= 10) breatheDir = -breatheDir;
  }

  pixels.show(); // Pošalji novu sliku u LED-ice
}

// Pomoćna funkcija za generiranje 255 boja Duge
uint32_t Wheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return pixels.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return pixels.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return pixels.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}