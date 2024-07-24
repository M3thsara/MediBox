//Include Libraries
#include <Wire.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHTesp.h>
#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP32Servo.h>

//Define OLED parameters
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1                                    //Define the reset pin of the OLED as same as the reset pin of the ESP32.
#define SCREEN_ADDRESS 0x3C

#define BUZZER 5

#define LED_1 4
#define LED_2 0
#define LED_3 2

#define PB_CANCEL 34
#define PB_OK 32
#define PB_UP 35
#define PB_DOWN 33

#define DHTPIN 12

#define LDR_LEFT A0
#define LDR_RIGHT A3

#define SERVOMOTOR 16

//Wifi and mqtt clients defining
WiFiClient espClient;
PubSubClient mqttClient(espClient);

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

#define NTP_SERVER     "pool.ntp.org"
#define UTC_OFFSET_DST 0

float UTC_OFFSET = 5.5;

// Declare Objects
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DHTesp dhtSensor;

float GAMMA = 0.75;
float lux = 0;
float RLTen = 50;
float Minimum_Angle = 30;

// Creating arrays to store Light and temperature data to send through mqtt to node-red
char TempAr[10];
char LdrIntensityArr[10];
char LdrPositionArr[10];

bool is_Scheduled_ON = false;
unsigned long Scheduled_ON_Time;

unsigned long time_Now = 0;
unsigned long time_Last = 0;
int LightVal = 0; 
int Servo_Angle = 0;

//servo moter initializing position
int position = 0;
Servo servo;

// Global variables
int days = 0;
int hours = 0;
int minutes = 0;
int seconds = 0;

bool alarm_enabled = true;                           // Turn on and off the alarms
int n_alarms = 3;                                    // To keep track of the no. of alarms
int alarm_hours[] = {12, 1, 2};
int alarm_minutes[] = {45, 10, 20};
bool alarm_triggered[] = {false, false, false};      // To keep track whether the alarm has been triggered and if the user has responded.

// set_time_zone variables
int time_zone_hour = 5;
int time_zone_minute = 30;

// Buzzer notes 
int n_notes = 8;
int C = 256;
int D = 288;
int E = 320;
int F = 341;
int G = 384;
int A = 426;
int B = 480;
int C_H = 512;

int notes[] = {C,D,E,F,G,A,B,C_H};

// Modes in Menu
int current_mode = 0;
int max_modes = 4;
String modes[] = {"1 - Set Time Zone" , "2 - Set Alarms", "3 - View Alarm List" , "4 - Disable Alarms" };

// Setup function
void setup() {

  Serial.begin(115200);

  // Initialize serial monitor and OLED display.
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  pinMode(LED_3, OUTPUT);
  pinMode(PB_CANCEL, INPUT);
  pinMode(PB_OK, INPUT);
  pinMode(PB_UP, INPUT);
  pinMode(PB_DOWN, INPUT);
  pinMode(LDR_LEFT, INPUT);
  pinMode(LDR_RIGHT, INPUT);

  // Initialize the Temperature and humidity sensor.
  dhtSensor.setup(DHTPIN, DHTesp::DHT22);
  servo.attach(SERVOMOTOR, 500, 2400);
  
  if (! display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS))             // Here we check, was the display initialize successfully?      "!" symbol works as logical not operation.
  {                                                                      
    Serial.println(F("SSD1306 allocation failed"));                      // If the display was not initialize successfully, above ! statement will become true and this line will be print.
    for (;;);                                                            // This is an infinitely executing for loop without any inputs.   the code will not be run beyond this for loop if the display is not working.
  }

  display.display();
  delay(500);

  // Set up the wifi connection
  WiFi.begin("Wokwi-GUEST", "", 6);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    display.clearDisplay();
    print_line("Connecting to WIFI", 0, 0, 2);
  }

  display.clearDisplay();
  print_line("Connected to WIFI", 0, 0, 2);
  delay(500);

  display.clearDisplay();

  Setup_MQTT();
  timeClient.begin();
  timeClient.setTimeOffset(5.5*3600);

  print_line("Welcome to Medibox !", 0, 20, 2);
  delay(1000);
  display.clearDisplay();

  configTime(UTC_OFFSET*3600, UTC_OFFSET_DST, NTP_SERVER);

  // Clear OLED display.
  display.clearDisplay();

  // Initialize the status of LEDs.
  digitalWrite(LED_1,LOW);
  digitalWrite(LED_2,LOW);
  digitalWrite(LED_3,LOW);
}

// void loop function
void loop() {

  // Check if the MQTT client is not connected and reconnect if necessary
  if (!mqttClient.connected()) {
  Serial.println("Reconnecting to MQTT Broker");
  Connect_Broker();
  }

  // Keep the MQTT client connection active
  mqttClient.loop();

  // Publish sensor data to MQTT topics
  mqttClient.publish("Medibox_ Light_ Intensity", LdrIntensityArr);
  mqttClient.publish("Medibox_ Temperature", TempAr);
  mqttClient.publish("Medibox_ Light_ Position", LdrPositionArr);

  // Read LDR sensor values
  read_Val_LDR();

  // Control servo motor based on LDR and Light intensity
  servo_Motor();

  // Check scheduled alarms
  check_Schedule();

  update_time_with_check_alarm();
  delay(1000);

  if (digitalRead(PB_OK) == LOW){
    delay(50);                             // To debounce the pushbutton

    display.clearDisplay();
    print_line("MENU", 40, 20 , 2);
    delay (1500);
    display.clearDisplay();
    go_to_menu();
  }

  check_temp_humidity();
  //mqttClient.setCallback(Receive_Call);
}

void update_time_with_check_alarm(void){
  update_time();
  print_time_now();

  if (alarm_enabled == true){                                                                                      // Check if the alarms are enabled. 
    for (int i = 0 ; i < n_alarms ; i++){                                                                          // If so, we check the state of each alarm.
      if (alarm_triggered[i] == false && alarm_hours[i] == hours && alarm_minutes[i] == minutes){                  // If the alarm has not been already trigggered and if the current hour is equal to alarm hour and if the current minute is equal to alarm minute, we can ring the alarm.
        ring_alarm();
        alarm_triggered[i] = true;
      }
    }
  }
}

// Display a custom message.
void print_line (String text, int column, int row, int text_size){

  display.setTextSize(text_size);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(column, row);
  display.println(text);
  display.display();
}

void set_time_zone(){

  display.clearDisplay();
  print_line("Current Time Zone : UCT + " + String(time_zone_hour) + ":" + String(time_zone_minute), 0, 0, 2);
  
  int pressed = wait_for_button_press();                              // Incerement or decrement the hour value with up or down keys.
  display.clearDisplay();
  
  if (pressed == PB_OK){
    delay (200);
    print_line("Set the   time zone   hour", 0, 0, 2);

    while (true){ 
      int pressed = wait_for_button_press();                          // Incerement or decrement the hour value with up or down keys.
      display.clearDisplay();
      if (pressed == PB_UP){
        delay (200);
        time_zone_hour += 1;
        time_zone_hour = time_zone_hour % 24;                         // If the time_zone_hour reaches 24, we should push it back to 0 .
      }

      else if (pressed == PB_DOWN){
        delay (200);
        time_zone_hour -= 1;
        if (time_zone_hour < 0){                                          
          time_zone_hour = 23 ;                                       // If the time_zone_hour reaches below zero, we should push it back to 23 .
        }
      }

      else if (pressed == PB_OK){
        delay (200);
        hours = time_zone_hour;
        break;
      }                                                              // If the PB_OK button or PB_CANCEL button is pressed, exit from the hours setting part.

      else if (pressed == PB_CANCEL){
        delay(200);
        break;                                                      
      }
      print_line(String(time_zone_hour),0,50,2);

    }
    display.clearDisplay();

    print_line("Set the time zone   minute", 0, 0, 2);

    while (true){ 
      int pressed = wait_for_button_press();                          // Incerement or decrement the minute value with up or down keys.
      display.clearDisplay();
      if (pressed == PB_UP){
        delay (200);
        time_zone_minute += 1;
        time_zone_minute = time_zone_minute % 60;                     // If the time_zone_minute reaches 60, we should push it back to 0 .
      }

      else if (pressed == PB_DOWN){
        delay (200);
        time_zone_minute -= 1;
        if (time_zone_minute < 0){                                         
          time_zone_minute = 59 ;                                     // If the time_zone_minute reaches below zero, we should push it back to 59 . 
        }
      }

      else if (pressed == PB_OK){
        delay (200);
        minutes = time_zone_minute;
        break;                                                        // If the PB_OK button or PB_CANCEL button is pressed, exit from the minutes setting part.
      }

      else if(pressed == PB_CANCEL){
        delay(200);
        break;
      }
      print_line(String(time_zone_minute), 0, 50, 2);
    }
    display.clearDisplay();
    print_line("Time Zone is set to:", 0, 0, 2);
    print_line(String(time_zone_hour), 0, 50, 2);
    print_line(String(":"), 20, 50, 2);
    print_line(String(time_zone_minute) , 40, 50, 2);
    delay(4000);                                                      // If the time_zone_hour reaches 24, we should push it back to 0 .
  }

  UTC_OFFSET = float(time_zone_hour) + float(time_zone_minute)/60;
  configTime(UTC_OFFSET*3600, UTC_OFFSET_DST, NTP_SERVER);
}

void update_time(){                                                // Update time using WiFi
  struct tm timeinfo;
  getLocalTime(&timeinfo);

  char timeHour[3];
  strftime(timeHour,3, "%H", &timeinfo);
  hours = atoi(timeHour);

  char timeMinute[3];
  strftime(timeMinute,3, "%M", &timeinfo);
  minutes = atoi(timeMinute);

  char timeSecond[3];
  strftime(timeSecond,3, "%S", &timeinfo);
  seconds = atoi(timeSecond);

  char timeDay[3];
  strftime(timeDay,3, "%d", &timeinfo);
  days = atoi(timeDay);
}

void print_time_now(void){ 

  display.clearDisplay();
  String time_now = String(hours) + ":" + String(minutes) + ":" + String(seconds) ;
  print_line(time_now, 20, 0, 2);
}

int wait_for_button_press(){                                        // Retuns the button which has been pressed. So, the type of function should be int.
  while (true) {                                                    // We have to wait until any button is pressed. So, we create infinite loop. (while loop)
    if (digitalRead(PB_UP)== LOW){
      delay (200);
      return PB_UP;                                                 // Break the loop and return the int value.
    }

    else if (digitalRead(PB_DOWN)== LOW){
      delay (200);
      return PB_DOWN;
    }

    else if (digitalRead(PB_OK)== LOW){
      delay (200);
      return PB_OK;
    }

    else if (digitalRead(PB_CANCEL)== LOW){
      delay (200);
      return PB_CANCEL;
    }

    update_time();                                                   // Keep updating the time until button is pressed.
  }
}

void go_to_menu(){

  while (digitalRead(PB_CANCEL) == HIGH){
    display.clearDisplay();
    print_line(modes[current_mode], 0, 0, 2);

    int pressed = wait_for_button_press();                           // 'pressed' accept the return value from 'wait_for_button_press' function. 
    if (pressed == PB_UP){
      delay (200);
      current_mode += 1;
      current_mode = current_mode % max_modes;                       // If the current mode reaches to max_modes value, we should put it back to 0.  This way we can wrap around the options with a single button.
    }

    else if (pressed == PB_DOWN){
      delay (200);
      current_mode -= 1;
      if (current_mode < 0){                                         // If current_mode reaches below zero, we should put it back to 'max_modes - 1'
        current_mode = max_modes - 1 ;
      }
    }

    else if (pressed == PB_OK){
      delay (200);
      run_mode(current_mode);
    }

    else if(pressed == PB_CANCEL){
      delay(200);
      break;                                                         // Exit from the go_to_menu function
    }
  }
}

void run_mode(int mode){                                             // Here, mode = current_mode
  if (mode == 0){
    set_time_zone();
  }

  else if (mode == 1){
    selectAlarm();                                                   // Alarm number = current mode -1
  }

  else if (mode == 2){
    view_alarm_list();
  }

  else if (mode == 3){
    alarm_enabled = false;
    display.clearDisplay();
    print_line("Alarms    Disabled", 0, 0, 2);
    delay(3000);
  }
}

void selectAlarm(){                                                // User can select which alarm he wants to set among the three alarms.

  int alarm_number = 0;

  while(true){

    display.clearDisplay();
    print_line("Select the Alarm", 0, 0, 2);
    print_line(String(alarm_number + 1), 30, 40, 3);

    int pressed = wait_for_button_press();
    if (pressed == PB_UP){
      delay(200);
      alarm_number += 1;
      alarm_number = alarm_number % 3;
    }

    else if (pressed == PB_DOWN){
      delay(200);
      alarm_number -= 1;
      if (alarm_number<0){
        alarm_number = 2;
      }
    }

    else if (pressed == PB_OK){
      delay(200);
      display.clearDisplay();
      set_alarm(alarm_number);
      break;
    }
    
    else if (pressed == PB_CANCEL){
      delay(200);
      break;
    }
  }
}

void set_alarm (int alarm){                                              // User can set the alarm by setting the values for hours and minutes variables.

  bool alarm_enabled = true;
  int temp_hour = alarm_hours[alarm];                                     
  while (true){
    display.clearDisplay();
    print_line("Enter hour: " + String(temp_hour), 0, 0, 2);

    int pressed = wait_for_button_press();
    if (pressed == PB_UP){
      delay (200);
      temp_hour += 1;
      temp_hour = temp_hour % 24;
    }

    else if (pressed == PB_DOWN){
      delay (200);
      temp_hour -= 1;
      if (temp_hour < 0){
        temp_hour = 23 ;
      }
    }

    else if (pressed == PB_OK){
      delay (200);
      alarm_hours[alarm] = temp_hour;
      break;
    }

    else if(pressed == PB_CANCEL){
      delay(200);
      break;
    }
  }

  int temp_minute = alarm_minutes[alarm];
  while (true){
    display.clearDisplay();
    print_line("Enter minute: " + String(temp_minute), 0, 0, 2);

    int pressed = wait_for_button_press();
    if (pressed == PB_UP){
      delay (200);
      temp_minute += 1;
      temp_minute = temp_minute % 60;
    }

    else if (pressed == PB_DOWN){
      delay (200);
      temp_minute -= 1;
      if (temp_minute < 0){
        temp_minute = 59 ;
      }
    }

    else if (pressed == PB_OK){
      delay (200);
      alarm_minutes[alarm] = temp_minute;
      break;
    }

    else if(pressed == PB_CANCEL){
      delay(200);
      break;
    }
  }

  display.clearDisplay();
  print_line("Alarm is set" , 0 , 0 , 2);
  delay(1000);

  configTime(UTC_OFFSET*3600, UTC_OFFSET_DST, NTP_SERVER);
}

void view_alarm_list(){                                                       // Show all the three alarms at once.

  display.clearDisplay();
  print_line("1. " + String(alarm_hours[0]) + ":" + String(alarm_minutes[0]), 0, 0, 2);
  print_line("2. " + String(alarm_hours[1]) + ":" + String(alarm_minutes[1]), 0, 22, 2);
  print_line("3. " + String(alarm_hours[2]) + ":" + String(alarm_minutes[2]), 0, 45, 2);
  delay(8000);

  configTime(UTC_OFFSET*3600, UTC_OFFSET_DST, NTP_SERVER);
}

void ring_alarm(){                                                           // When the alarm time has arrived, the buzzer will ring.

  display.clearDisplay();
  print_line ("MEDICINE  TIME !", 0, 0, 2);

  digitalWrite(LED_1, HIGH);

  bool break_happened = false;

  // Ring the buzzer
  while (break_happened == false && digitalRead(PB_CANCEL) == HIGH){
    
    for (int i = 0 ; i < n_notes ; i++){
      if (digitalRead (PB_CANCEL) == LOW){
        delay(200);
        break_happened = true;
        break;
      }
      tone (BUZZER, notes[i]);
      delay (500);
      noTone(BUZZER);
      delay(2);
    }
  }

  digitalWrite(LED_1, LOW);
  display.clearDisplay();
}

void check_temp_humidity(){
  TempAndHumidity data = dhtSensor.getTempAndHumidity();                   // 'TempAndHumidity' is a data type which is built in 'DHT sensor library for ESPx' .


  if (data.temperature > 32){
    print_line ("TEMPERATURE IS HIGH" , 0 , 40 , 1);
    digitalWrite(LED_2, HIGH);
    delay(2000);
  }
  
  else if (data.temperature < 26){
    display.clearDisplay();
    print_line ("TEMPERATURE IS LOW" , 0 , 40 , 1);
    digitalWrite(LED_2, HIGH);
    delay(2000);
  }
  else {
    digitalWrite(LED_2, LOW);
  }

  if (data.humidity > 80){
    display.clearDisplay();
    print_line ("HUMIDITY IS HIGH" , 0 , 50 , 1);
    digitalWrite(LED_3, HIGH);
    delay(2000);
  }

  else if (data.humidity < 60){
    display.clearDisplay();
    print_line ("HUMIDITY IS LOW" , 0 , 50 , 1);
    digitalWrite(LED_3, HIGH);
    delay(2000);
  }
  else {
    digitalWrite(LED_3, LOW);
  }

  String(data.temperature, 2).toCharArray(TempAr, 6);
  display.display();

}

// Setting up MQTT connection
void Setup_MQTT() {
  // Setting MQTT server and callback function
  mqttClient.setServer("test.mosquitto.org", 1883);
  mqttClient.setCallback(Receive_Call);
}

// Receive_Call function to receive MQTT messages
void Receive_Call(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.println("]");

  // Creating a character array to store payload
  char payLoadCharArray[length];

  // Loop to print and store payload characters
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    payLoadCharArray[i] = (char)payload[i];
  }

  Serial.print("\n");

  // Handling different topics and payloads
  // Getting minimum angle
  if (strcmp(topic, "Shade_ Angle") == 0) {
    Minimum_Angle = atoi(payLoadCharArray);
  }
  // Receiving control factor
  if (strcmp(topic, "Medibox_ Cont_ Factor") == 0) {
    GAMMA = atof(payLoadCharArray);
  }
  // Receiving Medibox alarm
  if (strcmp(topic, "Medibox_ Alarm") == 0) {
    Buzzer_ON(payLoadCharArray[0] == 't');
  } else if (strcmp(topic, "Scheduled_ Alarm") == 0) {
    // Receiving scheduled alarm
    if (payLoadCharArray[0] == 'N') {
      is_Scheduled_ON = false;
    } else {
      is_Scheduled_ON = true;
      Scheduled_ON_Time = atol(payLoadCharArray);
    }
  }
}

// Function to connect to MQTT
void Connect_Broker() {
  // Loop until connected to MQTT
  while (!mqttClient.connected()) {
    Serial.println("Attempting to connect to MQTT");

    if (mqttClient.connect("ESP32-42442222666")) {
      Serial.println("Connected to MQTT");
      // Subscribing to MQTT topics
      mqttClient.subscribe("Shade_ Angle");
      mqttClient.subscribe("Medibox_ Cont_ Factor");
      mqttClient.subscribe("Medibox_ Alarm");
      mqttClient.subscribe("Scheduled_ Alarm");
    } else {
      Serial.println("Connection to MQTT failed");
      Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}

// Function to turn buzzer on or off
void Buzzer_ON(bool on) {
  if (on) {
    tone(BUZZER, 256);
  } else {
    noTone(BUZZER);
  }
}

// Function to get current time
unsigned long get_Time() {
  timeClient.update();
  return timeClient.getEpochTime();
}

// Function to read LDR values
void read_Val_LDR() {
  int analog_Value_Left = analogRead(LDR_LEFT);
  int analog_Value_Right = analogRead(LDR_RIGHT);
  int analog_Value = 0;
  
  if (analog_Value_Left < analog_Value_Right) {
    analog_Value = analog_Value_Left;
    LightVal = 10;
    String(LightVal).toCharArray(LdrPositionArr, 6);
  } else {
    analog_Value = analog_Value_Right;
    LightVal = 100;
    String(LightVal).toCharArray(LdrPositionArr, 6);
  }

  // Calculating Light intensity in 0-1 range
  float Voltage = analog_Value / 1024. * 5;
  float Resistence = 2000 * Voltage / (1 - Voltage / 5);
  float maxlux = pow(RLTen * 1e3 * pow(10, GAMMA) / 322.58, (1 / GAMMA));
  lux = pow(RLTen * 1e3 * pow(10, GAMMA) / Resistence, (1 / GAMMA)) / maxlux;
  String(lux).toCharArray(LdrIntensityArr, 6);

  Serial.print("lux ");
  Serial.println(lux);
}

// Function to control servo motor
void servo_Motor() {
  // Calculating position of servo motor based on Light intensity
  if (LightVal < 50) {
    position = (Minimum_Angle * 1.5) + ((Minimum_Angle) * lux * GAMMA);  
  } else {
    position = (Minimum_Angle * 0.5) + ((Minimum_Angle) * lux * GAMMA);
  }

  if (position < 180) {
    Servo_Angle = position; 
  } else {
    Servo_Angle = 180;
  }

  Serial.print("Servo_Angle ");
  Serial.println(Servo_Angle);
  servo.write(Servo_Angle);
}

// Function to check scheduled alarms
void check_Schedule() {
  if (is_Scheduled_ON) {
    unsigned long currentTime = get_Time();

    if (currentTime > Scheduled_ON_Time) {
      // Turning buzzer on
      Buzzer_ON(true);
      is_Scheduled_ON = false;

      // Publishing MQTT message to indicate alarm activation
      mqttClient.publish("Medibox_ Alarm_ Switching", "1");
      mqttClient.publish("Medibox_ Alarm_ Switching", "0");

      Serial.println("Scheduled ON");
      Serial.println(currentTime);
      Serial.println(Scheduled_ON_Time);
    }
  }
}