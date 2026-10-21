#include <Arduino_FreeRTOS.h>
#include <queue.h>
#include <semphr.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>
#include <Keypad.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo lockServo;

#define PIR_PIN 2
#define BUZZER_PIN 3
#define SERVO_PIN 4

const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {12, 11, 10, 9};
byte colPins[COLS] = {8, 7, 6, 5};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

char entered[5];
byte keyIndex = 0;
bool isLocked = true;

QueueHandle_t logQueue;
SemaphoreHandle_t lockMutex;

void TaskAccessControl(void *pvParameters);
void TaskIntrusionDetection(void *pvParameters);
void TaskLogger(void *pvParameters);

void setup() {
  Serial.begin(9600); 
  lcd.init();
  lcd.backlight();
  
  pinMode(PIR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  
  lockServo.attach(SERVO_PIN);
  lockServo.write(0); 
  
  lcd.setCursor(0,0);
  // F() makrosu yazıları SRAM'den kurtarıp kalıcı hafızaya yazar
  lcd.print(F("Sistem Kilitli")); 

  logQueue = xQueueCreate(3, sizeof(byte)); 
  lockMutex = xSemaphoreCreateMutex();     

  // Hafıza kontrolü: Eğer RAM yeterliyse görevleri başlat
  if(logQueue != NULL && lockMutex != NULL) {
    xTaskCreate(TaskIntrusionDetection, "PIR", 85, NULL, 3, NULL);
    xTaskCreate(TaskAccessControl, "KPD", 110, NULL, 2, NULL);
    xTaskCreate(TaskLogger, "LOG", 85, NULL, 1, NULL);
    vTaskStartScheduler();
  } else {
    lcd.clear();
    lcd.print(F("RTOS RAM HATASI"));
  }
}

void loop() {}

void TaskAccessControl(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    char key = keypad.getKey();
    if (key) {
      entered[keyIndex++] = key;
      lcd.setCursor(keyIndex - 1, 1);
      lcd.print("*");

      if (keyIndex == 4) {
        // Hafıza yiyen strcmp yerine doğrudan karakter kontrolü
        if (entered[0]=='1' && entered[1]=='2' && entered[2]=='3' && entered[3]=='4') {
          xSemaphoreTake(lockMutex, portMAX_DELAY);
          isLocked = false;
          xSemaphoreGive(lockMutex);

          lcd.clear();
          lcd.print(F("Erisim Onaylandi"));
          lockServo.write(90); 
          
          byte logMsg = 1; 
          xQueueSend(logQueue, &logMsg, portMAX_DELAY);
          
          vTaskDelay(5000 / portTICK_PERIOD_MS); 
          
          xSemaphoreTake(lockMutex, portMAX_DELAY);
          isLocked = true;
          xSemaphoreGive(lockMutex);
          
          lockServo.write(0); 
          lcd.clear();
          lcd.print(F("Sistem Kilitli"));
        } else {
          lcd.clear();
          lcd.print(F("Hatali Sifre!"));
          byte logMsg = 2; 
          xQueueSend(logQueue, &logMsg, portMAX_DELAY);
          vTaskDelay(2000 / portTICK_PERIOD_MS);
          lcd.clear();
          lcd.print(F("Sistem Kilitli"));
        }
        keyIndex = 0; 
      }
    }
    vTaskDelay(30 / portTICK_PERIOD_MS); // Tuş algılamayı hızlandırmak için 30ms'ye çekildi
  }
}

void TaskIntrusionDetection(void *pvParameters) {
  (void) pvParameters;
  for (;;) {
    int pirState = digitalRead(PIR_PIN);
    
    xSemaphoreTake(lockMutex, portMAX_DELAY);
    bool lockedStatus = isLocked;
    xSemaphoreGive(lockMutex);

    if (pirState == HIGH && lockedStatus) {
      tone(BUZZER_PIN, 1000); 
      byte logMsg = 3; 
      xQueueSend(logQueue, &logMsg, portMAX_DELAY);
      vTaskDelay(3000 / portTICK_PERIOD_MS); 
      noTone(BUZZER_PIN);
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void TaskLogger(void *pvParameters) {
  (void) pvParameters;
  byte msgCode;
  for (;;) {
    if (xQueueReceive(logQueue, &msgCode, portMAX_DELAY) == pdPASS) {
      Serial.print(F("[LOG] - "));
      if (msgCode == 1) Serial.println(F("Yetkili Girisi Yapildi. Kilit Acildi."));
      else if (msgCode == 2) Serial.println(F("DIKKAT: Hatali Sifre Denemesi!"));
      else if (msgCode == 3) Serial.println(F("ALARM: Kilitliyken Hareket Algilandi!"));
    }
  }
}
