#include <Adafruit_BME280.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include <SPI.h>
#include <TFT_eSPI.h>

#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>

#include <esp_task_wdt.h>

#define WDT_TIMEOUT 120


const char* ssid     = "Casa";
const char* password = "*******";

Adafruit_BME280 bmp;
TFT_eSPI tft = TFT_eSPI();       // Invoke custom library
OneWire oneWire(32);
DallasTemperature temp_sensor(&oneWire);
WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);

float temp_offset = 0.0;
float temp = 21.0;
float temp2 = 22.0;
float pres = 1013;
float hr = 50.0;
int counter = 0;


// Factory settings for screen calib
uint16_t calData_[5] = { 427, 3445, 323, 3434, 7 };
// Actual settings for screen calib
uint16_t calData[5];

float set_temp = 20.0;

long lastMsg = 0;
long lastMqttConn = 0;
long lastMeas = -10000;
long lastTouch = 0;
long lastOnOff = 0;

const bool ACTIVE_WIFI = false;

#define mqtt_server "192.168.31.61"

#define humidity_topic "sensor/humidity"
#define temperature_topic "sensor/temperature"
#define pressure_topic "sensor/pressure"

//#define EEPROM_SIZE 32
Preferences preferences;

// Salidas digitales
const int outQ1 = 33; // Demanda de frío
const int outQ2 = 27; // Demanda de calor
const int outQ3 = 12;
const int outQ4 = 13;


bool led_state = false;
const int screenBL = 25;

bool airzone_active = false;
bool touch_debug = false;

bool pressed_onoff = false;
int ClimaMode = 0; // 0 ninguno, 1 frío, 2 calor

#define LEDC_CHANNEL_0     0
#define LEDC_TIMER_13_BIT  8
#define LEDC_BASE_FREQ     5000


float coseno[12] = {1.0f, 0.8660f, 0.5f, 0.0f, -0.5f, -0.8660f, -1.0f, -0.8660f, -0.5f, 0.0f, 0.5f, 0.8660f };
float seno[12] = {0.0f, 0.5f, 0.8660f, 1.0f, 0.8660f, 0.5f, 0.0f, -0.5f, -0.8660f, -1.0f, -0.8660f, -0.5f };

// Cambiando el valor de darkMode se puede alternar en la versión light o dark
const bool darkMode = true;
const uint32_t FRONT_COL = darkMode ? TFT_WHITE : TFT_BLACK;
const uint32_t BACK_COL = darkMode ? TFT_BLACK : TFT_WHITE;
const uint32_t GRAY_COL = darkMode ? TFT_DARKGREY : TFT_LIGHTGREY;


bool MASTER_MODE = true;
bool MENU_MODE = false;

// Ajustar el valor mínimo de la pantalla en modo "sleep"
int MIN_BACKLIGHT = 15;

void setup() {
  
  Serial.begin(9600);
  Serial.println("Configuring WDT...");
  esp_task_wdt_init(WDT_TIMEOUT, true); //enable panic so ESP32 restarts
  esp_task_wdt_add(NULL); //add current thread to WDT watch

  // Configuramos salidas digitales del Climazone
  pinMode(outQ1, OUTPUT);
  digitalWrite(outQ1, LOW);
  pinMode(outQ2, OUTPUT);
  digitalWrite(outQ2, LOW);
  pinMode(outQ3, OUTPUT);
  digitalWrite(outQ3, LOW);
  pinMode(outQ4, OUTPUT);
  digitalWrite(outQ4, LOW);

  ledcSetup(LEDC_CHANNEL_0, LEDC_BASE_FREQ, LEDC_TIMER_13_BIT);
  ledcAttachPin(screenBL, LEDC_CHANNEL_0);
  
  bmp.begin(0x76); // Dirección I2C del dispositivo, encontrada con I2C-Scanner
  temp_sensor.begin();
  
  tft.init();
  tft.setRotation(1);

  // Load Data from EEPROM
  load_data_eeprom();
  // Configure touch screen
  tft.setTouch(calData);

  tft.fillScreen(BACK_COL);
  while (!Serial && (millis() <= 1000));

  if (ACTIVE_WIFI)
  {
    setup_wifi();
    mqtt_client.setServer(mqtt_server, 1883);
    mqtt_client.setCallback(callback);
  }

  draw_onoff_button();
}

void load_data_eeprom()
{
  if (!preferences.begin("settings", false))
    Serial.println("Failed to initialize settings memory");
  else
    Serial.println("Loading settings from memory");

  MASTER_MODE = preferences.getBool("master_mode", false);
  temp_offset = preferences.getFloat("temp_offset", 0.0f);
  MIN_BACKLIGHT = preferences.getInt("min_backlight", MIN_BACKLIGHT);

  // Load touch screen calibration
  calData[0] = preferences.getUShort("caldata_0", calData_[0]);
  calData[1] = preferences.getUShort("caldata_1", calData_[1]);
  calData[2] = preferences.getUShort("caldata_2", calData_[2]);
  calData[3] = preferences.getUShort("caldata_3", calData_[3]);
  calData[4] = preferences.getUShort("caldata_4", calData_[4]);

  preferences.end();
}

void save_data_eeprom()
{
  if (!preferences.begin("settings", false))
    Serial.println("Failed to initialize settings memory");

  preferences.putBool("master_mode", MASTER_MODE);
  preferences.putFloat("temp_offset", temp_offset);
  preferences.putInt("min_backlight", MIN_BACKLIGHT);

  // Save touch screen calibration
  preferences.putUShort("caldata_0", calData[0]);
  preferences.putUShort("caldata_1", calData[1]);
  preferences.putUShort("caldata_2", calData[2]);
  preferences.putUShort("caldata_3", calData[3]);
  preferences.putUShort("caldata_4", calData[4]);

  preferences.end();
}

void setup_wifi()
{
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void get_touch_averaged(int max_touches, float &x, float &y)
{
  int touch_count = 0;
  float _x = 0, _y = 0;
  
  /*
  while (touch_count < max_touches)
  {
    if (ts.touched()) 
    {
      //TS_Point p = ts.getPoint();
      _x += p.x;
      _y += p.y;
      touch_count++;

      Serial.print("Calib point: x = ");
      Serial.print(p.x);
      Serial.print(", y = ");
      Serial.print(p.y);
      Serial.println();
    }
    delay(100);
  }
  */
  
  x = _x/max_touches;
  y = _y/max_touches;
}

void calibrate_touch()
{
    tft.fillScreen(BACK_COL);
    tft.setCursor(20, 0);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(FRONT_COL, BACK_COL);

    tft.println("Touch corners as indicated");

    delay(500);

    tft.setTextFont(1);
    tft.println();

    tft.calibrateTouch(calData, TFT_MAGENTA, BACK_COL, 15);
    tft.setTouch(calData);
    tft.setTextColor(TFT_GREEN, BACK_COL);
    tft.println("Calibration complete!");

    Serial.printf("Touch Calibration Data: [%d, %d, %d, %d, %d]\n", calData[0], calData[1], calData[2], calData[3], calData[4]);

    delay(500);
    tft.fillScreen(BACK_COL);
}

void reconnect_mqtt() 
{
  Serial.print("Attempting MQTT connection...");
  if (mqtt_client.connect("ESP8266Client", "esp32", "esp32")) 
  //if (mqtt_client.connect("ESP8266Client", mqtt_user, mqtt_password))
  {
      Serial.println("connected");

      mqtt_client.subscribe("esp32/output");
  } else {
      Serial.print("failed, rc=");
      Serial.println(mqtt_client.state());
  }
}

void callback(char* topic, byte* message, unsigned int length)
{
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp;
  
  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

  // If a message is received on the topic esp32/output, you check if the message is either "on" or "off". 
  // Changes the output state according to the message
  if (String(topic) == "esp32/output") {
    Serial.print("Changing output to ");
    if(messageTemp == "on"){
      Serial.println("on");
      //digitalWrite(ledPin, HIGH);
    }
    else if(messageTemp == "off"){
      Serial.println("off");
      //digitalWrite(ledPin, LOW);
    }
  }
}

void draw_onoff_button()
{
    uint32_t onoff_color = pressed_onoff ? TFT_RED : GRAY_COL;
    tft.fillCircle(280, 210, 17, onoff_color);
    tft.fillCircle(280, 210, 13, BACK_COL);
    tft.fillRect(280-3, 210-20, 6, 10, BACK_COL);
    tft.fillRect(280-2, 210-20, 4, 15, onoff_color);
}

void draw_current_temp()
{
   // Escribimos temperatura
    tft.setTextColor(FRONT_COL, BACK_COL);  // Text colour
    tft.setTextFont(6);
    int txt_x = 170;
    txt_x += tft.drawFloat(temp2, 1, txt_x, 20);
    tft.setTextFont(1);
    txt_x += tft.drawString(" o", txt_x, 20);
    tft.setTextFont(4);
    txt_x += tft.drawString("C", txt_x, 20);

    // Escribimos presión
    tft.setTextColor(TFT_BLUE, BACK_COL);  // Text colour
    tft.setTextFont(4);
    txt_x = 200;
    txt_x += tft.drawNumber(int(pres),txt_x, 70);
    tft.setTextFont(2);
    tft.drawString(" mbar", txt_x, 70);
    
    // Escribimos humedad relativa
    tft.setTextColor(TFT_GREEN, BACK_COL);  // Text colour
    tft.setTextFont(4);
    txt_x = 225;
    txt_x += tft.drawNumber(int(hr), txt_x, 100);
    tft.setTextFont(2);
    tft.drawString(" %HR", txt_x, 100);
  
}

bool set_screen_backlight(long lastTouch, long now, bool forced)
{
  if (forced) {
    ledcWrite(LEDC_CHANNEL_0, 255);
    return true;
  }

  const long t1 = 5000;
  const long t2 = t1 + 3000;

  if (now - lastTouch < t1) {
    ledcWrite(LEDC_CHANNEL_0, 255);
    return true;
  }
  else if (now - lastTouch < t2) {
    float y = 255 - (255 - MIN_BACKLIGHT)*(now - lastTouch - t1)/(float)(t2 - t1);
    ledcWrite(LEDC_CHANNEL_0, (int)y);
    return true;
  }
  else {
    ledcWrite(LEDC_CHANNEL_0, MIN_BACKLIGHT);
    return false;
  }
}

void meas_values(long now)
{
    if (now - lastMeas < 5000) return;

    temp_sensor.requestTemperatures(); 
    temp = bmp.readTemperature();
    pres = bmp.readPressure()/100;
    hr = bmp.readHumidity();
    temp2 = temp_sensor.getTempCByIndex(0) + temp_offset;

    Serial.print("Temp: ");
    Serial.println(temp);
    
    lastMeas = now;
}

void draw_sun(int x0, int y0, int r, uint32_t color)
{
  tft.fillCircle(x0, y0, r - 3, color);
  for (int i = 0; i < 12; i++)
  {
      float cx = coseno[i];//cosf(30.0f * i);
      float sx = seno[i];//sinf(30.0f * i);
      int x1 = round(x0 + (r-1)*cx);
      int x2 = round(x0 + (r+2)*cx);
      int y1 = round(y0 + (r-1)*sx);
      int y2 = round(y0 + (r+2)*sx);
      tft.drawLine(x1,y1, x2,y2, color);
  }
}

void draw_snow(int x0, int y0, int r, uint32_t color)
{

  for (int i = 0; i < 6; i++)
  {
    float cx = coseno[2*i];//(60.0f * i);
    float sx = seno[2*i];//sinf(60.0f * i);

    tft.drawLine(x0, y0, round(x0 + r*cx), round(y0 + r*sx), color);

    int px1 = round(x0 + 0.5*r*cx);
    int py1 = round(y0 + 0.5*r*sx);

    int idx_izq = i > 0 ? 2*i - 1 : 11;
    int idx_dch = i < 6 ? 2*i + 1 : 1;

    int px2 = round(x0 + 0.75*r*coseno[idx_izq]);
    int py2 = round(y0 + 0.75*r*seno[idx_izq]);

    int px3 = round(x0 + 0.75*r*coseno[idx_dch]);
    int py3 = round(y0 + 0.75*r*seno[idx_dch]);

    tft.drawLine(px1, py1, px2, py2, color);
    tft.drawLine(px1, py1, px3, py3, color);
  }

}

bool get_pressed_point(long now, uint16_t* x, uint16_t* y)
{
  // Pressed will be set true is there is a valid touch on the screen
  const uint16_t threshold = 600u;
  bool pressed = tft.getTouch(x, y, threshold);
  if (pressed) lastTouch = now;

  return pressed;
}

int currMenuPage = 0;
const int MenuPages = 2;

void handle_menu_screen(long now)
{
    // TODO:
    // Añadir histéresis

    // Dibujo zona común
    tft.setTextFont(2);
    tft.setTextColor(FRONT_COL, BACK_COL);

    tft.drawString("Exit", 20, 200);

    // Dibujamos control páginas
    tft.drawNumber(currMenuPage+1, 150, 200);
    tft.drawString("/", 165, 200);
    tft.drawNumber(MenuPages, 180, 200);

    int page_arr_x[] = {120, 210};
    int page_arr_y = 200;
    // Dibujamos controles
    tft.fillTriangle(page_arr_x[0], page_arr_y, page_arr_x[0], page_arr_y+16, page_arr_x[0]-10, page_arr_y+8, FRONT_COL);
    tft.fillTriangle(page_arr_x[1], page_arr_y, page_arr_x[1], page_arr_y+16, page_arr_x[1]+10, page_arr_y+8, FRONT_COL);
    

    // Draw page content
    if (currMenuPage == 0)
    {
      tft.drawString("Temp. Offset", 20, 20);
      tft.drawString("Master mode", 20, 50);
      tft.drawString("Min backlight", 20, 80);
      //tft.drawString("Calibrate", 20, 80);
      //tft.drawString("Reset Calib.", 20, 110);
      //tft.drawString("Restart", 20, 140);
      
      tft.drawFloat(temp_offset, 1, 230, 20);
      MASTER_MODE ? tft.drawString("Yes", 230, 50) : tft.drawString(" No ", 230, 50);
      tft.drawNumber(MIN_BACKLIGHT, 230, 80);

      int x1 = 200;
      int x2 = 290;
      int y_pos[] = {20, 50, 80};
      // Dibujamos controles
      for (int i = 0; i < 3; i++) {
          tft.fillTriangle(x1, y_pos[i], x1, y_pos[i]+16, x1-10, y_pos[i]+8, FRONT_COL);
          tft.fillTriangle(x2, y_pos[i], x2, y_pos[i]+16, x2+10, y_pos[i]+8, FRONT_COL);
      }
    }
    else if (currMenuPage == 1)
    {
      tft.drawString("Calibrate touch", 20, 20);
      tft.drawString("Reset Calib.", 20, 50);
      tft.drawString("Configure WiFi", 20, 80);
      tft.drawString("Restart", 20, 110);
    }
    
    // Check press event
    bool pressed = false;
    uint16_t x = 0, y = 0; // To store the touch coordinates
    if (now - lastTouch > 200) pressed = get_pressed_point(now, &x, &y);

    
    bool pressed_exit = false;
    bool pressed_calibrate = false;
    bool pressed_reset_calib = false;
    bool pressed_set_wifi = false;
    bool pressed_prev_page = false;
    bool pressed_next_page = false;

    // Eventos táctiles
    if (pressed) 
    {
        if (touch_debug) tft.fillCircle(x, y, 2, TFT_MAGENTA);

        // Botón Exit
        if (x >= 20 && x <= 75 && y >= 200 && y <= 216) pressed_exit = true;

        // Controles de página
        if (x >= page_arr_x[0] - 10 && x <= page_arr_x[0] && y >= page_arr_y && y <= page_arr_y + 16) {
            pressed_prev_page = true;
        }
        if (x >= page_arr_x[1] && x <= page_arr_x[1] + 10 && y >= page_arr_y && y <= page_arr_y + 16) {
            pressed_next_page = true;
        }

        if (currMenuPage == 0)
        {
          // Control offset temperatura
          int x1 = 200;
          int x2 = 290;
          int y_pos[] = {20, 50, 80};
          if (x >= x1 - 10 && x <= x1 && y >= y_pos[0] && y <= y_pos[0] + 16) {
              tft.fillRect(210, 20, 280, 16, BACK_COL);
              temp_offset -= 0.1;
          }
          if (x >= x2 && x <= x2 + 10 && y >= y_pos[0] && y <= y_pos[0] + 16) {
              tft.fillRect(210, 20, 280, 16, BACK_COL);
              temp_offset += 0.1;
          }

          // Control modo maestro
          if (x >= x1 - 10 && x <= x1 && y >= y_pos[1] && y <= y_pos[1] + 16) MASTER_MODE = !MASTER_MODE;
          if (x >= x2 && x <= x2 + 10 && y >= y_pos[1] && y <= y_pos[1] + 16) MASTER_MODE = !MASTER_MODE;

          // Control del backlight
          if (x >= x1 - 10 && x <= x1 && y >= y_pos[2] && y <= y_pos[2] + 16) {
              tft.fillRect(210, 80, 280, 16, BACK_COL);
              MIN_BACKLIGHT--; 
              if (MIN_BACKLIGHT < 0) MIN_BACKLIGHT = 0;
          }
          if (x >= x2 && x <= x2 + 10 && y >= y_pos[2] && y <= y_pos[2] + 16) {
              tft.fillRect(210, 80, 280, 16, BACK_COL);
              MIN_BACKLIGHT++;
              if (MIN_BACKLIGHT > 255) MIN_BACKLIGHT = 255;
          }
        }
        else if (currMenuPage == 1)
        {
          // Calibrate
          if (x >= 20 && x <= 120 && y >= 20 && y <= 20 + 16) pressed_calibrate = true;

          // Reset calib
          if (x >= 20 && x <= 100 && y >= 50 && y <= 50 + 16) pressed_reset_calib = true;

          // Set WiFi
          if (x >= 20 && x <= 100 && y >= 80 && y <= 80 + 16) pressed_set_wifi = true;

          // Restart
          if (x >= 20 && x <= 100 && y >= 110 && y <= 110 + 16) {
              Serial.println("Restarting...");
              sleep(1);
              ESP.restart();
          }
        }
        
    }

    if (pressed_exit) {
        save_data_eeprom();
        tft.fillScreen(BACK_COL);
        MENU_MODE = false;
        draw_onoff_button(); // TODO: Esto no debería estar aquí
        return;
    }

    if (pressed_calibrate) {
        tft.fillScreen(BACK_COL);
        calibrate_touch();
        return;
    }

    if (pressed_set_wifi) {
      tft.fillScreen(BACK_COL);
      Serial.println("Setting wifi connection");
      // enable wifi
      // set webpage
      return;
    }

    if (pressed_reset_calib) {
        for (int i = 0; i < 5; i++) calData[i] = calData_[i];
        tft.setTouch(calData);
    }

    if (pressed_next_page) 
    {
      currMenuPage++;
      if (currMenuPage >= MenuPages) currMenuPage = 0;
      tft.fillScreen(BACK_COL);
    }
    if (pressed_prev_page)
    {
      currMenuPage--;
      if (currMenuPage < 0) currMenuPage = MenuPages - 1;
      tft.fillScreen(BACK_COL);
    }

}

void handle_main_screen(long now)
{
 // Actualizamos medición de valores
  meas_values(now);

  // Rutina para apagar la pantalla pasados 2 segundos
  bool screen_active = set_screen_backlight(lastTouch, now, false);

  // Gestión de la conexión con MQTT
  bool mqtt_active = false;
  if (ACTIVE_WIFI)
  {
    mqtt_active = mqtt_client.connected();
    if (!mqtt_active && (now - lastMqttConn) > 10000) {
        reconnect_mqtt();
        lastMqttConn = now;
    }
    if (mqtt_active) mqtt_client.loop();
  }

  // Gestión de la pantalla táctil
  bool pressed = false;
  bool touch_activated = true;
  uint16_t x = 0, y = 0; // To store the touch coordinates
  
  if (now -lastTouch > 200) pressed = get_pressed_point(now, &x, &y);

  bool pressed_up = false;
  bool pressed_down = false;
  bool pressed_menu = false;
  bool pressed_cold = false;
  bool pressed_hot = false;

  bool draw_onoff = false;

  // Eventos táctiles
  if (pressed && screen_active) 
  {
    if (touch_debug) tft.fillCircle(x, y, 2, TFT_MAGENTA);

    if (x >= 15 && x <= 75 && y >= 95 && y <= 120) pressed_up = true;
    if (x >= 80 && x <= 140 && y >= 95 && y <= 120) pressed_down = true;

    if (x >= 20 && x <= 50 && y >= 155 && y <= 185) pressed_cold = true;
    if (x >= 80 && x <= 110 && y >= 155 && y <= 185) pressed_hot = true;

    if (x >= 150 && x <= 190 && y >= 215 && y <= 240) pressed_menu = true;

    // Encendido/Apagado: sólo permitimos una acción cada 2seg.
    if (x >= 280-17 && x <= 280+16 && y >= 210-17 && y <= 210+17 && now-lastOnOff > 2000) {
      if (!pressed_onoff) draw_onoff = true;
      pressed_onoff = true;
      lastOnOff = now;
    }
  }
  else
  {
    if (pressed_onoff) {
      pressed_onoff = false;
      draw_onoff = true;
    }
  }

  if (pressed_onoff) {
    airzone_active = !airzone_active;
  }

  // Gestión del modo frío/calor
  if (pressed_cold) ClimaMode = 1;
  else if (pressed_hot) ClimaMode = 2;

  // Gestión de la temperatura del termostato
  if (airzone_active)
  {
      if (pressed_up) set_temp += 0.5;
      if (pressed_down) set_temp -= 0.5;

      if (set_temp > 30) set_temp = 30;
      if (set_temp < 15) set_temp = 15;

      // Demanda de frío
      if (temp2 > set_temp + 0.1)  digitalWrite(outQ1, HIGH);
      else digitalWrite(outQ1, LOW);
      
      // Demanda de calor
      if (temp2 < set_temp - 0.1)  digitalWrite(outQ2, HIGH);
      else digitalWrite(outQ2, LOW);
  }
  else
  {
    digitalWrite(outQ1, LOW);
    digitalWrite(outQ2, LOW);
  }


  // Gestión y dibujado del botón de encendido
  if (draw_onoff)
  {
    draw_onoff_button();

    if (!airzone_active)
      tft.fillRect(0, 0, 160, 200, BACK_COL); // Si apagamos, borramos los controles del termostato
  }

  
  // Comprobamos si es necesario limpiar la pantalla
  //if (screen_redraw) tft.fillScreen(TFT_WHITE);

  // Gestión del menu
  if (pressed_menu) 
  {
    // BORRAMOS PANTALLA
    tft.fillScreen(BACK_COL);
    MENU_MODE = true;
    return;
  }

  // Dibujamos controles del termostato
  if (airzone_active)
  {
    const int tX1 = 15;
    const int tW = 60;
    
    const int tH = 35;

    const int tX2 = 80;
    const int tY1 = 120;
    const int tY2 = 120 - 35;

    tft.fillTriangle(tX1, tY1, tX1+tW, tY1, tX1+tW/2, tY1-tH, TFT_RED);
    tft.fillTriangle(tX2, tY2, tX2+tW, tY2, tX2+tW/2, tY2+tH, TFT_CYAN);

    tft.setTextColor(FRONT_COL, BACK_COL);  // Text colour
    tft.setTextFont(6);
    int txt_x = 20 + tft.drawFloat(set_temp, 1, 20, 20);
    tft.setTextFont(1);
    txt_x += tft.drawString(" o", txt_x, 20);
    tft.setTextFont(4);
    txt_x += tft.drawString("C", txt_x, 20);
  }

    // Gestión modo frío/calor
    if (MASTER_MODE)
    {
        tft.setTextFont(4);

        uint32_t cold_col = ClimaMode == 1 ? TFT_CYAN : GRAY_COL;
        draw_snow(35, 170, 15, cold_col);

        uint32_t heat_col = ClimaMode == 2 ? TFT_ORANGE : GRAY_COL;
        draw_sun(95, 170, 15, heat_col);

        if (ClimaMode == 1) // modo frío
        {
            digitalWrite(outQ3, HIGH);
            digitalWrite(outQ4, LOW);
        }
        else if (ClimaMode == 2) // modo calor
        {
            digitalWrite(outQ3, LOW);
            digitalWrite(outQ4, HIGH);
        }
    }
  
    // Dibujamos pantalla
    if (now - lastMsg > 2000)
    {
        draw_current_temp();

        // Escribimos wifi, mqtt y menú
        tft.setTextFont(2);
        if (ACTIVE_WIFI) tft.setTextColor(FRONT_COL, BACK_COL);
        else tft.setTextColor(GRAY_COL, BACK_COL);
        tft.drawString("WiFi", 20, 215);
        if (mqtt_active) tft.setTextColor(FRONT_COL, BACK_COL);
        else tft.setTextColor(GRAY_COL, BACK_COL);
        tft.drawString("MQTT", 60, 215);

        if (pressed_menu) tft.setTextColor(TFT_RED, BACK_COL);
        else tft.setTextColor(FRONT_COL, BACK_COL);
        tft.drawString("MENU", 150, 215);

        // Enviamos datos por mqtt
        if (mqtt_active)
        {
            //Serial.println("Sending MQTT topics...");
            mqtt_client.publish(temperature_topic, String(temp2).c_str(), true);
            mqtt_client.publish(humidity_topic, String(hr).c_str(), true);
            mqtt_client.publish(pressure_topic, String(pres).c_str(), true);
        }

        lastMsg = now;
    }
}

void loop() 
{
    esp_task_wdt_reset();

    // TODO:
    // Activar programación OTA?

    long now = millis();

    // MENU SCREEN
    if (MENU_MODE == true)
    {
        handle_menu_screen(now);
        return;
    }
    // MAIN SCREEN
    handle_main_screen(now);
}