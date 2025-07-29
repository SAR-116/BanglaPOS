#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Keypad.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <PubSubClient.h>

#include "HOME_PAGE.h"
#include "INVENTORY_PAGE.h"
#include "INVENTORY_REPEAT_PAGE.h"
#include "SELL_REPEAT_PAGE.h"
#include "CUSTOMER_NUMBER_PAGE.h"
#include "SELL_PRODUCT_CODE_PAGE.h"
#include "STOCK_OUT_PAGE.h"

#include "ShonarBangla16pt8b.h"

// ----- WiFi & MQTT Configuration -----
const char* ssid = "4002";
const char* password = "combat12";
const char* mqtt_server = "test.mosquitto.org"; // Change to your MQTT broker IP

WiFiClient espClient;
PubSubClient client(espClient);

// ----- EEPROM & Product Setup -----
#define EEPROM_SIZE 128
#define PRODUCT_MIN 100
#define PRODUCT_MAX 109
#define PRODUCT_COUNT (PRODUCT_MAX - PRODUCT_MIN + 1)

struct Product {
  int code;
  int quantity;
  float price;
};

// ----- Display & Keypad -----
Adafruit_ST7789 tft = Adafruit_ST7789(15, 2, 4); // CS, DC, RST

const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {32, 33, 25, 26};
byte colPins[COLS] = {27, 14, 12, 13};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

enum State {
  HOME,
  INVENTORY_CODE, INVENTORY_QTY, INVENTORY_REPEAT,
  CUSTOMER_NUMBER, SELL_PRODUCT_CODE, SELL_PRODUCT_QTY, SELL_PRODUCT_PRICE, SELL_REPEAT,
  DISPLAY_CODE, DISPLAY_RESULT, STOCK_OUT
};

State currentState = HOME;
String inputBuffer = "";
int currentCode = -1;
int currentQty = 0;
float currentPrice = 0.0;
String customerNumber = "";
char key = 'E';

// ----- EEPROM Functions -----
int getEEPROMAddress(int index) {
  return index * sizeof(Product);
}

int findProductIndex(int code) {
  for (int i = 0; i < PRODUCT_COUNT; i++) {
    Product p;
    EEPROM.get(getEEPROMAddress(i), p);
    if (p.code == code) return i;
  }
  return -1;
}

bool isValidCode(int code) {
  return code >= PRODUCT_MIN && code <= PRODUCT_MAX;
}

void sendInventoryToDashboard() {
  String json2 = "[";
  bool first = true;

  for (int i = 0; i < PRODUCT_COUNT; i++) {
    Product p;
    EEPROM.get(getEEPROMAddress(i), p);
    if (p.code >= PRODUCT_MIN && p.code <= PRODUCT_MAX) {
      if (!first) json2 += ",";
      json2 += "{";
      json2 += "\"code\":" + String(p.code) + ",";
      json2 += "\"qty\":" + String(p.quantity) + ",";
      json2 += "\"price\":" + String(p.price, 2);
      json2 += "}";
      first = false;
      //Serial.println("loop %D\n", p);

    }
  }

  json2 += "]";
  client.publish("pos/inventory", json2.c_str());
  //Serial.println("pos/inventory\n");
  Serial.print("Publishing Inventory JSON: ");
  Serial.println(json2);  // DEBUG PRINT
  bool result = client.publish("pos/inventory", json2.c_str());
  Serial.println(result ? "Publish Success" : "Publish Failed");
}


void writeProduct(Product p) {
  int index = findProductIndex(p.code);
  if (index == -1) {
    for (int i = 0; i < PRODUCT_COUNT; i++) {
      Product slot;
      EEPROM.get(getEEPROMAddress(i), slot);
      if (slot.code == 0) {
        EEPROM.put(getEEPROMAddress(i), p);
        EEPROM.commit();
        sendInventoryToDashboard();
        return;
      }
    }
  } else {
    EEPROM.put(getEEPROMAddress(index), p);
    EEPROM.commit();
    sendInventoryToDashboard();
  }
}

bool readProduct(int code, Product &p) {
  int index = findProductIndex(code);
  if (index == -1) return false;
  EEPROM.get(getEEPROMAddress(index), p);
  return true;
}

// ----- Wi-Fi & MQTT Functions -----
void setup_wifi() {
  delay(10);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("WiFi connected");
}

void reconnect_mqtt() {
  while (!client.connected()) {
    if (client.connect("ESP32_POS_Client")) {
      Serial.println("MQTT connected");
      sendInventoryToDashboard();
    } else {
      delay(500);
      Serial.println("Not MQTT connected");
    }
  }
}

// ----- Display Functions -----
void initDisplay() {
  tft.init(240, 320);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
}

int cursorPositionX = 5;
int cursorPositionY = 230;
int16_t lastx_1, lasty_1;
uint16_t ww, hh;

void displayInput() {
  tft.setCursor(cursorPositionX + 20, cursorPositionY - 10);
  tft.print(" ");
  tft.setTextColor(ST77XX_WHITE);
  tft.setFont(&ShonarBangla16pt8b);
  tft.setCursor(cursorPositionX, cursorPositionY);
  if (key >= '0' && key <= '9') {
    tft.write((char)(0x9E6 + (key - '0')));
    char unicodeChar = (char)(0x9E6 + (key - '0'));
    char textStr[2] = { unicodeChar, '\0' };
    tft.getTextBounds(textStr, 0, 0, &lastx_1, &lasty_1, &ww, &hh);
    cursorPositionX += ww + 5;
    if (cursorPositionX > 200) {
      cursorPositionY += hh + 5;
      cursorPositionX = 5;
    }
  }
}

void displayResult(int quantity) {
  int temp = quantity;
  int divisor = 1;
  while (temp >= 10) {
    temp /= 10;
    divisor *= 10;
  }
  while (divisor > 0) {
    int digit = quantity / divisor;
    tft.setFont(&ShonarBangla16pt8b);
    tft.setCursor(cursorPositionX, cursorPositionY);
    tft.write((char)(0x9E6 + digit));
    quantity %= divisor;
    divisor /= 10;
    char unicodeChar = (char)(0x9E6 + digit);
    char textStr[2] = { unicodeChar, '\0' };
    tft.getTextBounds(textStr, 0, 0, &lastx_1, &lasty_1, &ww, &hh);
    cursorPositionX += ww + 5;
    if (cursorPositionX > 200) {
      cursorPositionY += hh + 5;
      cursorPositionX = 5;
    }
  }
}

void showPage() {
  switch (currentState) {
    case HOME:
      tft.drawRGBBitmap(0, 0, HOME_PAGE, 240, 320);
      cursorPositionX = 5; cursorPositionY = 245;
      break;
    case INVENTORY_CODE:
      tft.drawRGBBitmap(0, 0, INVENTORY_PAGE, 240, 320);
      cursorPositionX = 5; cursorPositionY = 155;
      break;
    case INVENTORY_QTY:
      cursorPositionX = 5; cursorPositionY = 265;
      break;
    case INVENTORY_REPEAT:
      tft.drawRGBBitmap(0, 0, INVENTORY_REPEAT_PAGE, 240, 320);
      cursorPositionX = 5; cursorPositionY = 220;
      break;
    case CUSTOMER_NUMBER:
      tft.drawRGBBitmap(0, 0, CUSTOMER_NUMBER_PAGE, 240, 320);
      cursorPositionX = 5; cursorPositionY = 150;
      break;
    case SELL_PRODUCT_CODE:
      tft.drawRGBBitmap(0, 0, SELL_PRODUCT_CODE_PAGE, 240, 320);
      cursorPositionX = 5; cursorPositionY = 130;
      break;
    case SELL_PRODUCT_QTY:
      cursorPositionX = 5; cursorPositionY = 220;
      break;
    case SELL_PRODUCT_PRICE:
      cursorPositionX = 5; cursorPositionY = 300;
      break;
    case SELL_REPEAT:
      tft.drawRGBBitmap(0, 0, SELL_REPEAT_PAGE, 240, 320);
      cursorPositionX = 5; cursorPositionY = 220;
      break;
    case DISPLAY_CODE:
      tft.drawRGBBitmap(0, 0, SELL_PRODUCT_CODE_PAGE, 240, 320);
      cursorPositionX = 5; cursorPositionY = 120;
      break;
    case DISPLAY_RESULT:
      Product p;
      if (readProduct(currentCode, p)) {
        cursorPositionX = 5; cursorPositionY = 220;
        displayResult(p.quantity);
        int itemp = ceil(p.price);
        cursorPositionX = 5; cursorPositionY = 300;
        displayResult(itemp);
      } else {
        tft.fillScreen(ST77XX_BLACK);
        tft.println("Product not found");
      }
      break;
    case STOCK_OUT:
      tft.drawRGBBitmap(0, 0, STOCK_OUT_PAGE, 240, 320);
      break;
  }
  if (currentState != STOCK_OUT) displayInput();
}

void BackButtonPage() {
  Product p;
  readProduct(currentCode, p);
  switch (currentState) {
    case HOME:
      tft.drawRGBBitmap(0, 0, HOME_PAGE, 240, 320);
      cursorPositionX = 5; cursorPositionY = 245;
      break;
    case INVENTORY_CODE:
      tft.drawRGBBitmap(0, 0, INVENTORY_PAGE, 240, 320);
      cursorPositionX = 5; cursorPositionY = 155;
      break;
    case INVENTORY_QTY:
      tft.drawRGBBitmap(0, 0, INVENTORY_PAGE, 240, 320);
      cursorPositionX = 5; cursorPositionY = 155;
      displayResult(p.code);
      cursorPositionX = 5; cursorPositionY = 265;
      break;
    case INVENTORY_REPEAT:
      tft.drawRGBBitmap(0, 0, INVENTORY_REPEAT_PAGE, 240, 320);
      cursorPositionX = 5; cursorPositionY = 220;
      break;
    case CUSTOMER_NUMBER:
      tft.drawRGBBitmap(0, 0, CUSTOMER_NUMBER_PAGE, 240, 320);
      cursorPositionX = 5; cursorPositionY = 150;
      break;
    case SELL_PRODUCT_CODE:
      tft.drawRGBBitmap(0, 0, SELL_PRODUCT_CODE_PAGE, 240, 320);
      cursorPositionX = 5; cursorPositionY = 130;
      break;
    case SELL_PRODUCT_QTY:
      tft.drawRGBBitmap(0, 0, SELL_PRODUCT_CODE_PAGE, 240, 320);
      cursorPositionX = 5; cursorPositionY = 130;
      displayResult(currentCode);
      cursorPositionX = 5; cursorPositionY = 220;
      break;
    case SELL_PRODUCT_PRICE:
      tft.drawRGBBitmap(0, 0, SELL_PRODUCT_CODE_PAGE, 240, 320);
      cursorPositionX = 5; cursorPositionY = 130;
      displayResult(currentCode);
      cursorPositionX = 5; cursorPositionY = 220;
      displayResult(currentQty);
      cursorPositionX = 5; cursorPositionY = 300;
      break;
    case SELL_REPEAT:
      tft.drawRGBBitmap(0, 0, SELL_REPEAT_PAGE, 240, 320);
      cursorPositionX = 5; cursorPositionY = 220;
      break;
    case DISPLAY_CODE:
      tft.drawRGBBitmap(0, 0, SELL_PRODUCT_CODE_PAGE, 240, 320);
      cursorPositionX = 5; cursorPositionY = 120;
      break;
    case DISPLAY_RESULT:
      break;
    case STOCK_OUT:
      tft.drawRGBBitmap(0, 0, STOCK_OUT_PAGE, 240, 320);
      break;
  }
}

// ----- State Machine -----
void handleEnter() {
  Product p;
  switch (currentState) {
    case HOME:
      if (inputBuffer == "1") currentState = CUSTOMER_NUMBER;
      else if (inputBuffer == "2") currentState = INVENTORY_CODE;
      else if (inputBuffer == "3") currentState = DISPLAY_CODE;
      break;

    case INVENTORY_CODE:
      currentCode = inputBuffer.toInt();
      currentState = isValidCode(currentCode) ? INVENTORY_QTY : HOME;
      break;

    case INVENTORY_QTY:
      currentQty = inputBuffer.toInt();
      if (readProduct(currentCode, p)) {
        p.quantity += currentQty;
      } else {
        p.code = currentCode;
        p.quantity = currentQty;
        p.price = 0.0;
      }
      writeProduct(p);
      currentState = INVENTORY_REPEAT;
      break;

    case INVENTORY_REPEAT:
      currentState = (inputBuffer == "1") ? INVENTORY_CODE : HOME;
      break;

    case CUSTOMER_NUMBER:
      customerNumber = inputBuffer;
      currentState = SELL_PRODUCT_CODE;
      break;

    case SELL_PRODUCT_CODE:
      currentCode = inputBuffer.toInt();
      currentState = isValidCode(currentCode) ? SELL_PRODUCT_QTY : HOME;
      break;

    case SELL_PRODUCT_QTY:
      currentQty = inputBuffer.toInt();
      currentState = SELL_PRODUCT_PRICE;
      break;

    case SELL_PRODUCT_PRICE:
      currentPrice = inputBuffer.toFloat();
      if (readProduct(currentCode, p)) {
        if ((p.quantity - currentQty) < 0) {
          currentState = STOCK_OUT;
          break;
        } else {
          p.quantity -= currentQty;
          if (p.quantity < 0) p.quantity = 0;
          p.price = currentPrice;
          writeProduct(p);

          // MQTT Publish Transaction
          String json = "{";
          json += "\"code\":" + String(currentCode) + ",";
          json += "\"qty\":" + String(currentQty) + ",";
          json += "\"price\":" + String(currentPrice, 2) + ",";
          json += "\"customer\":\"" + customerNumber + "\"";
          json += "}";
          client.publish("pos/transaction", json.c_str());
        }
      }
      currentState = SELL_REPEAT;
      break;

    case SELL_REPEAT:
      currentState = (inputBuffer == "1") ? SELL_PRODUCT_CODE : HOME;
      break;

    case STOCK_OUT:
      currentState = HOME;
      break;

    case DISPLAY_CODE:
      currentCode = inputBuffer.toInt();
      currentState = isValidCode(currentCode) ? DISPLAY_RESULT : HOME;
      break;

    case DISPLAY_RESULT:
      currentState = HOME;
      break;
  }

  inputBuffer = "";
  showPage();
}

// ----- Setup & Loop -----
void setup() {
  pinMode(5, OUTPUT);
  digitalWrite(5, HIGH);
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  initDisplay();
  showPage();

}

unsigned long lastBlinkTime = 0;
bool cursorVisible = true;
const unsigned long blinkInterval = 500;

void loop() {
  if (!client.connected()) reconnect_mqtt();
  client.loop();

  key = keypad.getKey();
  if (key) {
    if (key == 'A') {
      tft.setCursor(cursorPositionX + 20, cursorPositionY - 10);
      tft.print(" ");
      handleEnter();
    } else if (key == 'B') {
      inputBuffer = "";
      BackButtonPage();
      //if (currentState != STOCK_OUT) displayInput();
    } else if (key == 'C') {
      currentState = HOME;
      inputBuffer = "";
      showPage();
    } else {
      if((currentState!=STOCK_OUT)&(currentState!=DISPLAY_RESULT)){
      inputBuffer += key;
      displayInput();
      }
    }
  }

  if((currentState!=STOCK_OUT)&(currentState!=DISPLAY_RESULT)){
  // Cursor blink
  tft.setFont();
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  if (millis() - lastBlinkTime > blinkInterval) {
    lastBlinkTime = millis();
    cursorVisible = !cursorVisible;
    tft.setCursor(cursorPositionX + 20, cursorPositionY - 10);
    tft.print(cursorVisible ? "_" : " ");
  }
  }
}
