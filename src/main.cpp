#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <cstdio>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define LED_PIN 2
#define BUZZER_PIN 14
#define TOUCH_PIN 23

bool buzzerIsPLay = false;
bool isSensorTouched = false;
bool alertIsTrue = false;

int locationID = 31;

// LED variables
unsigned long lastBlinkTime = 0;
const long blinkInterval = 300;

// Buzzer variables (неблокуючий таймер)
unsigned long lastBuzzerTime = 0;
const long buzzerInterval = 1000; // Інтервал між звуками

String API_URL = "https://api.alerts.in.ua/v1/iot/active_air_raid_alerts/" + String(locationID) + ".json?token=" + API_TOKEN;

// Інтервал оновлення даних (30 секунд)
const unsigned long UPDATE_INTERVAL = 30000;
unsigned long lastUpdateTimestamp = 0;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Оголошуємо клієнт глобально
WiFiClientSecure client;

void connectToWiFi() {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Connecting to WiFi...");
    display.display();

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi Connected");
        display.println("WiFi OK!");
        display.display();
        delay(1000);
    } else {
        Serial.println("\nWiFi Connection Failed");
        display.println("WiFi FAILED!");
        display.display();
    }
}

void updateDisplay(bool isAlertActive) {
    display.clearDisplay();
    
    if (isAlertActive) {
        display.fillRect(61, 10, 6, 32, SSD1306_WHITE);
        display.fillCircle(64, 50, 3, SSD1306_WHITE);
    } else {
        int startX = 40; 
        int midScreen = SCREEN_HEIGHT / 2;

        for (int i = 0; i < 5; i++) {
            display.fillCircle(startX + (i * 12), midScreen, 2, SSD1306_WHITE);
        }
    }
    
    display.display();
}

void displayError(const String& errorText) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("System Error:");
    display.setTextSize(2);
    display.setCursor(0, 25);
    display.println(errorText);
    display.display();
}

void checkAirAlert() {
    if (WiFi.status() != WL_CONNECTED) {
        connectToWiFi();
        return;
    }

    HTTPClient http;
    
    // Налаштовуємо таймаут підключення до 10 секунд
    http.setTimeout(10000);
    
    Serial.println("Checking alert status via Active Alerts API...");
    
    if (!http.begin(client, API_URL)) {
        Serial.println("Unable to connect to API server");
        displayError("ERR:CONN");
        return;
    }

    http.addHeader("Authorization", "Bearer demo");
    http.addHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");
    http.addHeader("Accept", "application/json");

    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK) {
        Serial.print("HTTP Error code: ");
        Serial.println(httpCode);
        displayError("ERR:" + String(httpCode));
        http.end();
        return;
    }

    String payload = http.getString();
        
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
        const char* status = doc | "N";

        bool newAlertStatus = (status[0] == 'A');

        // Якщо стан змінився з False на True — скидаємо прапорець натискання кнопкою
        if (newAlertStatus && !alertIsTrue) {
            isSensorTouched = false;
            buzzerIsPLay = true;
        }

        alertIsTrue = newAlertStatus;

        if (alertIsTrue) {
            updateDisplay(true);
            Serial.println("Status: TRUE (Alert Active in Kyiv)");
        } else {
            buzzerIsPLay = false;
            updateDisplay(false);
            digitalWrite(LED_PIN, LOW);
            Serial.println("Status: FALSE (No Alert)");
        }
    } else {
        Serial.print("JSON parsing failed: ");
        Serial.println(error.c_str());
        displayError("ERR:JSON");
    }

    http.end();
}

// Неблокуюче відтворення звуку
void handleBuzzer(unsigned long currentTimestamp) {
    if (buzzerIsPLay && !isSensorTouched) {
        if (currentTimestamp - lastBuzzerTime >= buzzerInterval) {
            lastBuzzerTime = currentTimestamp;
            tone(BUZZER_PIN, 1000, 500); 
        }
    }
}

void setup() {
    Serial.begin(115200);

    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
        Serial.println(F("SSD1306 allocation failed"));
        for(;;); 
    }
    
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    pinMode(BUZZER_PIN, OUTPUT);
    pinMode(TOUCH_PIN, INPUT);

    // Налаштування SSL-клієнта під HTTPS без перевірки сертифікатів
    client.setInsecure();

    display.setTextColor(SSD1306_WHITE);
    connectToWiFi();
    
    checkAirAlert();
}

void loop() {
    unsigned long currentTimestamp = millis();
    
    if (currentTimestamp - lastUpdateTimestamp >= UPDATE_INTERVAL) {
        lastUpdateTimestamp = currentTimestamp;
        checkAirAlert();
    }

    if (alertIsTrue) {
        // Sensor touch
        int touchState = digitalRead(TOUCH_PIN);
        if (touchState == HIGH && !isSensorTouched && buzzerIsPLay) {
            isSensorTouched = true;
            buzzerIsPLay = false;
            noTone(BUZZER_PIN); // Негайно вимикаємо пищання при дотику
            Serial.println("Muted by touch sensor!");
        }

        // Buzzer
        handleBuzzer(currentTimestamp);

        // LED
        if (currentTimestamp - lastBlinkTime >= blinkInterval) {
            lastBlinkTime = currentTimestamp;
            digitalWrite(LED_PIN, !digitalRead(LED_PIN));
        }
    } else {
        digitalWrite(LED_PIN, LOW);
        noTone(BUZZER_PIN);
    }
}