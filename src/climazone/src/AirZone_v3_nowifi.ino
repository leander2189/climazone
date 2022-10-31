#include <Adafruit_BME280.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TFT_eSPI.h> // Hardware-specific library
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

#include <WiFi.h>
#include <PubSubClient.h>

const char* ssid     = "Casa";
const char* password = "lemi2015";

Adafruit_BME280 bmp;
TFT_eSPI tft = TFT_eSPI();       // Invoke custom library
OneWire oneWire(32);
DallasTemperature temp_sensor(&oneWire);
XPT2046_Touchscreen ts(8);
WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);

float temp = 21.0;
float temp2 = 22.0;
float pres = 1013;
float hr = 50.0;
int counter = 0;
uint16_t calData[5] = { 210, 3648, 145, 3642, 7 };


float v0 = 3825;
float v1 = 340;
float w0 = 3800;
float w1 = 310;

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

// Salidas digitales
const int outQ1 = 33; // Demanda de frío
const int outQ2 = 27; // Demanda de calor
const int outQ3 = 12;
const int outQ4 = 13;
//const long histeresis = 60000;


bool led_state = false;
//const int ledPin = 27;
const int screenBL = 25;

bool airzone_active = false;

bool touch_debug = false;
//bool screen_redraw = false;

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

// Ajustar el valor mínimo de la pantalla en modo "sleep"
const int MIN_BACKLIGHT = 5;

void setup() {
  // put your setup code here, to run once:

  // Configuramos salidas digitales del Climazone
  pinMode(outQ1, OUTPUT);
  digitalWrite(outQ1, LOW);
  pinMode(outQ2, OUTPUT);
  digitalWrite(outQ2, LOW);
  pinMode(outQ3, OUTPUT);
  digitalWrite(outQ3, LOW);
  pinMode(outQ4, OUTPUT);
  digitalWrite(outQ4, LOW);


  //pinMode(ledPin, OUTPUT);
  //pinMode(screenBL, OUTPUT);
  ledcSetup(LEDC_CHANNEL_0, LEDC_BASE_FREQ, LEDC_TIMER_13_BIT);
  ledcAttachPin(screenBL, LEDC_CHANNEL_0);
  //digitalWrite(screenBL, HIGH);
  
  
  bmp.begin(0x76); // Dirección I2C del dispositivo, encontrada con I2C-Scanner
  temp_sensor.begin();
  
  Serial.begin(38400);
  
  tft.init();
  ts.begin();
  tft.setRotation(1);
  ts.setRotation(1);

  tft.fillScreen(BACK_COL);
  //tft.setTouch(calData);
  while (!Serial && (millis() <= 1000));

  if (ACTIVE_WIFI)
  {
    setup_wifi();
    mqtt_client.setServer(mqtt_server, 1883);
    mqtt_client.setCallback(callback);
  }

  draw_onoff_button();
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
  
  while (touch_count < max_touches)
  {
    if (ts.touched()) 
    {
      TS_Point p = ts.getPoint();
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
  
  x = _x/max_touches;
  y = _y/max_touches;
}

void calibrate_touch()
{
  //digitalWrite(screenBL, HIGH);
  ledcWrite(0, 255);
  tft.fillScreen(TFT_WHITE);
  tft.setCursor(20, 0);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_RED, TFT_WHITE);
  tft.println("Touch corners as indicated");
  delay(2000);

  // Esquina 1
  tft.drawLine(0, 0, 30, 30, TFT_RED);
  tft.drawLine(0, 0, 20, 0, TFT_RED);
  tft.drawLine(0, 0, 0, 20, TFT_RED);

  float x1 = 0, y1 = 0;
  get_touch_averaged(5, x1, y1);
  tft.fillScreen(TFT_WHITE);
  delay(1000);

  // Esquina 2
  tft.drawLine(320,0, 320-30, 30, TFT_RED);
  tft.drawLine(320,0, 320-20, 0, TFT_RED);
  tft.drawLine(320,0, 320, 20, TFT_RED);

  float x2 = 0, y2 = 0;
  get_touch_averaged(5, x2, y2);
  tft.fillScreen(TFT_WHITE);
  delay(1000);

  // Esquina 3
  tft.drawLine(320, 240, 320-30, 240-30, TFT_RED);
  tft.drawLine(320, 240, 320-20, 240, TFT_RED);
  tft.drawLine(320, 240, 320, 240-20, TFT_RED);

  float x3 = 0, y3 = 0;
  get_touch_averaged(5, x3, y3);
  tft.fillScreen(TFT_WHITE);
  delay(1000);

  // Esquina 4
  tft.drawLine(0, 240, 30, 240-30, TFT_RED);
  tft.drawLine(0, 240, 20, 240, TFT_RED);
  tft.drawLine(0, 240, 0, 240-20, TFT_RED);

  float x4 = 0, y4 = 0;
  get_touch_averaged(5, x4, y4);

  // Calculamos resultados
  v0 = 0.5*(x1+x4);
  v1 = 0.5*(x2+x3);

  w0 = 0.5*(y1+y2);
  w1 = 0.5*(y3+y4);

  Serial.print("V0 = ");
  Serial.print(v0);
  Serial.print(", V1 = ");
  Serial.print(v1);
  Serial.println();

  Serial.print("W0 = ");
  Serial.print(w0);
  Serial.print(", W1 = ");
  Serial.print(w1);
  Serial.println();

  tft.fillScreen(TFT_WHITE);
  tft.setTextColor(TFT_GREEN, TFT_WHITE);
  tft.println("Calibration complete!");
  delay(2000);
  tft.fillScreen(TFT_WHITE);
}

TS_Point calculate_touch_point(TS_Point p)
{
  TS_Point p2;

  float x = 320*(p.x - v0)/(v1-v0);
  float y = 240*(p.y - w0)/(w1-w0);
  Serial.print(", XX = ");
  Serial.print(x);
  Serial.print(", YY = ");
  Serial.print(y);
  Serial.println();
  p2.x = (uint16_t)x;
  p2.y = (uint16_t)y;
  p2.z = p.z;
  return p2;
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
  temp2 = temp_sensor.getTempCByIndex(0);
  
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


void loop() 
{
  long now = millis();

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
  uint16_t x = 0, y = 0, z = 0; // To store the touch coordinates
  if (touch_activated && ts.touched() && now - lastTouch > 200) {
    TS_Point p = ts.getPoint();

    if (p.z > 600) {
      pressed = true;
      lastTouch = now;

      Serial.print("Pressure = ");
      Serial.print(p.z);
      Serial.print(", x = ");
      Serial.print(p.x);
      Serial.print(", y = ");
      Serial.print(p.y);
      Serial.println();
  
      TS_Point p2 = calculate_touch_point(p);
      x = p2.x; y = p2.y; z = p2.z;
    }
  }


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
      if (temp2 > set_temp + 0.1) 
      {
        digitalWrite(outQ1, HIGH);
      }
      else
      {
        digitalWrite(outQ1, LOW);
      }
      
      // Demanda de calor
      if (temp2 < set_temp - 0.1) 
      {
        digitalWrite(outQ2, HIGH);
      }
      else
      {
        digitalWrite(outQ2, LOW);
      }
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


  // Gestión del menu --> TODO: Revisar
  if (pressed_menu) 
  {
    calibrate_touch();
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
    //if (pressed_up) tft.fillTriangle(10, 150, 10+tW, 150, 10+tW/2, 120, TFT_RED);
    //else tft.fillTriangle(10, 150, 70, 150, 40, 120, TFT_DARKGREY);

    tft.fillTriangle(tX2, tY2, tX2+tW, tY2, tX2+tW/2, tY2+tH, TFT_CYAN);
    //if (pressed_down) tft.fillTriangle(10, 160, 70, 160, 40, 190, TFT_CYAN);
    //else tft.fillTriangle(10, 160, 70, 160, 40, 190, TFT_DARKGREY);

    tft.setTextColor(FRONT_COL, BACK_COL);  // Text colour
    tft.setTextFont(6);
    int txt_x = 20 + tft.drawFloat(set_temp, 1, 20, 20);
    tft.setTextFont(1);
    txt_x += tft.drawString(" o", txt_x, 20);
    tft.setTextFont(4);
    txt_x += tft.drawString("C", txt_x, 20);

    



  }

  // Gestión modo frío/calor
  {
    tft.setTextFont(4);
    //if (ClimaMode == 1) tft.setTextColor(TFT_BLACK, TFT_CYAN);
    //else tft.setTextColor(TFT_BLACK, TFT_WHITE);
    //tft.drawString("Fr", 20, 150);

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

  // Si hemos redibujado la pantalla, desactivamos este evento
  //if (screen_redraw) screen_redraw = false;
}
