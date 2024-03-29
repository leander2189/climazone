#include <Adafruit_BME280.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include <SPI.h>
#include <TFT_eSPI.h>

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

#include <esp_task_wdt.h>

#include <SinricPro.h>
#include <SinricProThermostat.h>

#include <ArduinoOTA.h>

#define WDT_TIMEOUT 120




Adafruit_BME280 bmp;
TFT_eSPI tft = TFT_eSPI();       // Invoke custom library
OneWire oneWire(32);
DallasTemperature temp_sensor(&oneWire);
WebServer web_server(80);


float temp_offset = 0.0;
float temp = 21.0;
float temp2 = 22.0;
float pres = 1013;
float hr = 50.0;


// Factory settings for screen calib
uint16_t calData_[5] = { 427, 3445, 323, 3434, 7 };
// Actual settings for screen calib
uint16_t calData[5];

float set_temp = 20.0;

long lastMsg = 0;
long lastMeas = -1000000;
long lastTouch = 0;
long lastOnOff = 0;

long lastSent_Temp = 0;
long lastSent_SetTemp = 0;
float last_SetTemp = 0;

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
bool is_OTA_ongoing = false;

// Variables del menú
enum MenuModes { MAIN_SCREEN, MENU, WIFI_SETUP};
MenuModes MENU_MODE = MAIN_SCREEN;
int currMenuPage = 0;
const int MenuPages = 2;

// Ajustar el valor mínimo de la pantalla en modo "sleep"
int MIN_BACKLIGHT = 15;


// Internet settings
String Wifi_ssid;
String Wifi_password;
String Sinric_key;
String Sinric_secret;
String Device_id;
String Device_name;


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
  draw_onoff_button();

  //while (!Serial && (millis() <= 1000));

  // Connect to wifi
  init_wifi();

  // Connect to Sinric
  init_sinric();

  // Init OTA
  init_OTA();


}

// Configuration settings
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

  // Load internet settings
  Wifi_ssid = preferences.getString("wifi_ssid", "");
  Wifi_password = preferences.getString("wifi_password", "");
  Sinric_key = preferences.getString("sinric_key", "");
  Sinric_secret = preferences.getString("sinric_secret", "");
  Device_id = preferences.getString("device_id", "");
  Device_name = preferences.getString("device_name", "");

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

  // Load internet settings
  preferences.putString("wifi_ssid", Wifi_ssid);
  preferences.putString("wifi_password", Wifi_password);
  preferences.putString("sinric_key", Sinric_key);
  preferences.putString("sinric_secret", Sinric_secret);
  preferences.putString("device_id", Device_id);
  preferences.putString("device_name", Device_name);

  preferences.end();
}

// Touch screen
bool get_pressed_point(long now, uint16_t* x, uint16_t* y)
{
  // Pressed will be set true is there is a valid touch on the screen
  const uint16_t threshold = 600u;
  bool pressed = tft.getTouch(x, y, threshold);
  if (pressed) lastTouch = now;

  return pressed;
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

// Drawing utilities
void draw_onoff_button()
{
    uint32_t onoff_color = airzone_active ? FRONT_COL : GRAY_COL;
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
    if (pres < 1000) txt_x += tft.drawString(" ", txt_x, 70); // Handle padding
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
    if (now - lastMeas < 30000ul) return;

    temp_sensor.requestTemperatures(); 
    temp = bmp.readTemperature();
    pres = bmp.readPressure()/100;
    hr = bmp.readHumidity();
    temp2 = temp_sensor.getTempCByIndex(0) + temp_offset;

    Serial.printf("Temp (18DS20): %.1fºC | Temp (BMP): %.1fºC | Pressure: %.0f mbar | HR: %.0f %%\n", temp2, temp, pres, hr);
    
    lastMeas = now;
}

void handle_menu_screen(long now)
{
    // TODO:
    // Añadir histéresis

    // Dibujo zona común
    tft.setTextFont(2);
    tft.setTextColor(TFT_RED, BACK_COL);
    tft.drawString("Exit", 20, 200);
    tft.setTextColor(FRONT_COL, BACK_COL);

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
      tft.drawString(WiFi.localIP().toString(), 140, 80);
      tft.drawNumber(WiFi.RSSI(), 260, 80);
      tft.drawString("dBm", 290, 80);
      //tft.printf("%d dBm\r\n", WiFi.RSSI());
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
        MENU_MODE = MAIN_SCREEN;
        draw_onoff_button(); // TODO: Esto no debería estar aquí
        return;
    }

    if (pressed_calibrate) {
        tft.fillScreen(BACK_COL);
        calibrate_touch();
        return;
    }

    if (pressed_set_wifi) {
      set_webserver();
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

void set_airzone_active(bool active)
{
  airzone_active = active;

  if (!active)
      tft.fillRect(0, 0, 160, 200, BACK_COL); // Si apagamos, borramos los controles del termostato

  draw_onoff_button();
}

bool get_airzone_active() { return airzone_active; }

void handle_main_screen(long now)
{
 // Actualizamos medición de valores
  meas_values(now);
  send_sinric_temp(temp2, hr, now);

  // Rutina para apagar la pantalla pasados 2 segundos
  bool screen_active = set_screen_backlight(lastTouch, now, false);

  // Gestión de la pantalla táctil
  bool pressed = false;
  uint16_t x = 0, y = 0; // To store the touch coordinates
  if (now - lastTouch > 200) pressed = get_pressed_point(now, &x, &y);

  bool pressed_up = false;
  bool pressed_down = false;
  bool pressed_menu = false;
  bool pressed_cold = false;
  bool pressed_hot = false;
  bool pressed_onoff = false;
  
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
      //if (!pressed_onoff) draw_onoff = true;
      pressed_onoff = true;
      lastOnOff = now;
    }
  }

  if (pressed_onoff) {
    set_airzone_active(!get_airzone_active());
    sent_sinric_power(get_airzone_active());
  }

  // Gestión del modo frío/calor
  if (pressed_cold) ClimaMode = 1;
  else if (pressed_hot) ClimaMode = 2;
  if (pressed_cold || pressed_hot) send_sinric_mode(ClimaMode);

  // Gestión de la temperatura del termostato
  if (get_airzone_active())
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

      send_sinric_set_temp(set_temp, now);
  }
  else
  {
    digitalWrite(outQ1, LOW);
    digitalWrite(outQ2, LOW);
  }

  // Gestión del menu
  if (pressed_menu) 
  {
    // BORRAMOS PANTALLA
    tft.fillScreen(BACK_COL);
    MENU_MODE = MENU;
    return;
  }

  // Dibujamos controles del termostato
  if (get_airzone_active())
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

        // Escribimos wifi, menú
        tft.setTextFont(2);
        if (WiFi.isConnected()) tft.setTextColor(FRONT_COL, BACK_COL);
        else tft.setTextColor(GRAY_COL, BACK_COL);
        tft.drawString("WiFi", 20, 215);

        if (pressed_menu) tft.setTextColor(TFT_RED, BACK_COL);
        else tft.setTextColor(FRONT_COL, BACK_COL);
        tft.drawString("MENU", 150, 215);

        lastMsg = now;
    }
}

void loop() 
{
  long now = millis();
  esp_task_wdt_reset();

  ArduinoOTA.handle();
  if (is_OTA_ongoing) return; 

  SinricPro.handle();

  switch (MENU_MODE)
  {
  case MAIN_SCREEN:
    handle_main_screen(now);
    break;
  case MENU:
    handle_menu_screen(now);
    break;
  case WIFI_SETUP:
    handle_webserver_screen(now);
    break;
  default:
    break;
  }
}

// Funciones de conectividad
void init_wifi()
{
  if (Wifi_ssid == "") return; // Check that there is a network defined

  Serial.print("Connecting to ");
  Serial.println(Wifi_ssid);
  WiFi.begin(Wifi_ssid, Wifi_password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.printf("RSSI: %d dBm\r\n", WiFi.RSSI());
}

void set_webserver()
{
  Serial.println("Setting wifi connection");
  MENU_MODE = WIFI_SETUP;
  tft.fillScreen(BACK_COL);

  // Init wifi as AP
  WiFi.mode(WIFI_MODE_AP);
  Serial.print("Setting up WiFi AP...");
  String ap_name = "ESP_" + WiFi.macAddress();
  ap_name.replace(":", "_");
  WiFi.softAP(ap_name);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  tft.setTextFont(2);
  tft.setTextColor(FRONT_COL, BACK_COL);
  tft.drawString("1. Connect to WiFi network:", 30, 30);
  tft.setTextColor(TFT_GREEN, BACK_COL);
  tft.drawString(ap_name, 30, 50);

  tft.setTextColor(FRONT_COL, BACK_COL);
  tft.drawString("2. Open web address:", 30, 80);
  tft.setTextColor(TFT_GREEN, BACK_COL);
  tft.drawString(IP.toString(), 30, 110);
  tft.setTextColor(FRONT_COL, BACK_COL);
  tft.drawString("3. Fill in form", 30, 140);

  tft.setTextColor(TFT_RED, BACK_COL);
  tft.drawString("Exit", 20, 200);

  web_server.on("/", handle_OnConnect);
  web_server.on("/post", handle_OnSubmit);
  web_server.onNotFound(handle_NotFound);
  web_server.begin();
}

void handle_OnConnect() 
{
  Serial.println("HTML requested");
  web_server.send(200, "text/html", SendHTML()); // 3
}

void handle_NotFound()
{
  web_server.send(404, "text/plain", "La pagina no existe");
}

void handle_OnSubmit()
{

  int n_args = web_server.args();
  Serial.printf("Request got with %d args \n", n_args);

  // Cogemos parámetros
  if (n_args == 6) {
    Wifi_ssid = web_server.arg(0);
    Serial.printf("SSID_Name = %s\n", Wifi_ssid.c_str());
    Wifi_password = web_server.arg(1);
    Serial.printf("Password = %s\n", Wifi_password.c_str());
    Sinric_key = web_server.arg(2);
    Serial.printf("Sinric_Key = %s\n", Sinric_key.c_str());
    Sinric_secret = web_server.arg(3);
    Serial.printf("Sinric Secret = %s\n", Sinric_secret.c_str());
    Device_id = web_server.arg(4);
    Serial.printf("Device_Id = %s\n", Device_id.c_str());
    Device_name = web_server.arg(5);
    Serial.printf("Device_Name = %s\n", Device_name.c_str());
  } else {
      Serial.println("Not expected number of args");
  }

  web_server.send(200, "text/plain", "OK");

  // Mostramos info en la pantalla
  tft.fillScreen(BACK_COL);
  tft.setTextFont(2);
  tft.setTextColor(FRONT_COL, BACK_COL);
  tft.drawString("Updating settings...", 30, 30);

  // Guardamos la eeprom
  save_data_eeprom();

  // Cerramos el servidor web
  web_server.close();
  
  // Conectamos wifi
  tft.drawString("Connecting to WiFi...", 30, 60);
  init_wifi();
  init_sinric();
  init_OTA();

  // Volvemos a pantalla principal
  tft.fillScreen(BACK_COL);
  draw_onoff_button();
  MENU_MODE = MAIN_SCREEN;
}

void handle_webserver_screen(long now)
{
  web_server.handleClient();

  // Check press event
  bool pressed = false;
  uint16_t x = 0, y = 0; // To store the touch coordinates
  if (now - lastTouch > 200) pressed = get_pressed_point(now, &x, &y);
  // Botón Exit
  if (x >= 20 && x <= 75 && y >= 200 && y <= 216)
  {
    // Cerramos el servidor web
    web_server.close();
  
    // Volvemos a pantalla principal
    tft.fillScreen(BACK_COL);
    draw_onoff_button();
    MENU_MODE = MAIN_SCREEN;
    return;
  }
}

String SendHTML() 
{
  // Cabecera de todas las paginas WEB
  String ptr = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>WiFi Settings</title>
<style>
    body {
        font-family: Arial, sans-serif;
        margin: 0;
        padding: 0;
        display: flex;
        justify-content: center;
        align-items: center;
        height: 100vh;
        background-color: #f2f2f2;
    }
    .container {
        background-color: #fff;
        padding: 20px;
        border-radius: 8px;
        box-shadow: 0px 0px 10px 0px rgba(0,0,0,0.1);
    }
    .form-group {
        margin-bottom: 20px;
    }
    .form-group label {
        display: block;
        font-weight: bold;
        margin-bottom: 5px;
    }
    .form-group input {
        width: 100%;
        padding: 10px;
        border: 1px solid #ccc;
        border-radius: 4px;
        box-sizing: border-box;
        font-size: 16px;
    }
    .btn-submit {
        background-color: #4CAF50;
        color: white;
        padding: 10px 20px;
        border: none;
        border-radius: 4px;
        cursor: pointer;
        font-size: 16px;
    }
    .btn-submit:hover {
        background-color: #45a049;
    }
</style>
</head>
<body>

<div class="container">
    <h2>WiFi Settings</h2>
    <form id="wifi-form" action="/post">
        <div class="form-group">
            <label for="ssid">SSID Name:</label>
            <input type="text" id="ssid" name="ssid" required>
        </div>
        <div class="form-group">
            <label for="password">WiFi Password:</label>
            <input type="password" id="password" name="password">
        </div>
        <div class="form-group">
            <label for="app-key">Sinric App Key:</label>
            <input type="text" id="app-key" name="app-key">
        </div>
        <div class="form-group">
            <label for="app-secret">Sinric App Secret:</label>
            <input type="text" id="app-secret" name="app-secret">
        </div>
        <div class="form-group">
            <label for="device-id">Device Id:</label>
            <input type="text" id="device-id" name="device-id">
        </div>
        <div class="form-group">
            <label for="device-name">Device Name:</label>
            <input type="text" id="device-name" name="device-name">
        </div>
        <button type="submit" class="btn-submit">Submit</button>
    </form>
</div>

</body>
</html>
  )rawliteral"; 
  return ptr;
}

// Funciones de Sinric
void init_sinric() 
{
  if (!WiFi.isConnected()) return;

  SinricProThermostat &myThermostat = SinricPro[Device_id];
  myThermostat.onPowerState(onPowerState);
  myThermostat.onTargetTemperature(onTargetTemperature);
  myThermostat.onAdjustTargetTemperature(onAdjustTargetTemperature);
  myThermostat.onThermostatMode(onThermostatMode);

  // setup SinricPro
  SinricPro.onConnected([](){ Serial.println("[Sinric] Connected to SinricPro"); }); 
  SinricPro.onDisconnected([](){ Serial.println("[Sinric] Disconnected from SinricPro"); });
  Serial.printf("Sinric Key: %s\n", Sinric_key.c_str());
  Serial.printf("Sinric secret: %s\n", Sinric_secret.c_str());
  Serial.printf("Sinric device id: %s\n", Device_id.c_str());
  SinricPro.begin(Sinric_key, Sinric_secret);
}

bool onPowerState(const String &deviceId, bool &state) 
{
  set_airzone_active(state);
  Serial.printf("[Sinric] Thermostat %s turned %s\r\n", deviceId.c_str(), state ?"on":"off");

  return true; // request handled properly
}

bool onTargetTemperature(const String &deviceId, float &temperature) 
{
  set_temp = temperature;
  Serial.printf("[Sinric] Thermostat %s set temperature to %f\r\n", deviceId.c_str(), temperature);

  return true;
}

bool onAdjustTargetTemperature(const String & deviceId, float &temperatureDelta) 
{
  set_temp += temperatureDelta;  // calculate absolut temperature
  Serial.printf("Thermostat %s changed temperature about %f to %f", deviceId.c_str(), temperatureDelta, set_temp);

  return true;
}

bool onThermostatMode(const String &deviceId, String &mode) 
{
  Serial.printf("Thermostat %s set to mode %s\r\n", deviceId.c_str(), mode.c_str());

  if (mode == "HEAT") ClimaMode = 2;
  if (mode == "COOL") ClimaMode = 1;
  //lastTouch = millis();

  return true;
}

void sent_sinric_power(bool active)
{
  // send powerstate event
  SinricProThermostat &myThermostat = SinricPro[Device_id];
  myThermostat.sendPowerStateEvent(active); // send the new powerState to SinricPro server
}

void send_sinric_temp(float temp, float hr, long now) 
{
  if (now - lastSent_Temp < 60000) return;  
  //if (temp2 == lastemp2 || hr == lasthr) return; // if no values changed do nothing...
  SinricProThermostat &myThermostat = SinricPro[Device_id];
  bool res = myThermostat.sendTemperatureEvent(temp, hr);
  if (!res) Serial.println("[Sinric] Send Temp event failed");

  //lastemp2 = temp2;  // save actual temperature for next compare
  //lasthr = hr;        // save actual humidity for next compare
  lastSent_Temp = now;       // save actual time for next compare
}

void send_sinric_set_temp(float temp, long now) 
{
  if (now - lastSent_SetTemp < 30000) return;  
  if (abs(temp - last_SetTemp) < 0.1) return; // if no values changed do nothing...

  SinricProThermostat &myThermostat = SinricPro[Device_id];
  myThermostat.sendTargetTemperatureEvent(temp);

  last_SetTemp = temp;  // save actual temperature for next compare
  lastSent_SetTemp = now;       // save actual time for next compare
}

void send_sinric_mode(int mode)
{
  SinricProThermostat &myThermostat = SinricPro[Device_id];
  if (mode == 1) myThermostat.sendThermostatModeEvent("COOL");
  else if (mode == 2) myThermostat.sendThermostatModeEvent("HEAT");
}

int last_percentage = -1;

// Funciones OTA
void init_OTA()
{
  if (!WiFi.isConnected()) return;

  ArduinoOTA.onStart([&]() 
  {
    is_OTA_ongoing = true;

    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
    
    set_screen_backlight(lastTouch, millis(), true);
    tft.fillScreen(BACK_COL);
    tft.setTextFont(4);
    tft.setTextColor(FRONT_COL, BACK_COL);
    tft.drawString("OTA Update", 90, 30);
    tft.drawRect(28, 78, 264, 34, FRONT_COL);
  });

  ArduinoOTA.onEnd([]() 
  {
    is_OTA_ongoing = false;
    Serial.println("\nEnd");

    tft.setTextColor(TFT_GREEN, BACK_COL);
    tft.drawString("Complete!", 90, 150);
    delay(1000);
    tft.fillScreen(BACK_COL);
  });

  

  ArduinoOTA.onProgress([&](unsigned int progress, unsigned int total) 
  {
    int percentage = 100*progress/total;
    if (percentage > last_percentage && percentage % 4 == 0)
    {
      Serial.printf("Progress: %u%%\r\n", percentage);
      int x = 30+percentage*2.5;
      tft.fillRect(x, 80, 9, 30, TFT_GREEN);
      last_percentage = percentage;
    }

  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }

    tft.setTextColor(TFT_RED, BACK_COL);
    tft.drawString("Error :(", 80, 180);
    delay(1000);
    is_OTA_ongoing = false;
    ESP.restart();
  });

  ArduinoOTA.setTimeout(5000);
  ArduinoOTA.begin();
  Serial.println("OTA iniciado");
}