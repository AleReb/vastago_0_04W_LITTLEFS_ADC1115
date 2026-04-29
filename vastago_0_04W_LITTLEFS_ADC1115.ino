#include <Wire.h>  // Para I2C/SMBus
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <LittleFS.h>  // Para almacenamiento de archivos HTML y JS
#include <SPI.h>
#include <SD.h>
#include <Adafruit_ADS1X15.h>
#include <U8g2lib.h>

String version = "0.04WLFS";
// Credenciales de WiFi
const char* ssid = "Red_Taller_ing";
const char* password = "aiC#aim3Eas7gae";

// Pines para tarjeta SD (SPI) - Mapeo típico LilyGo T8 S3 pero usando hardware SPI configurado manual
#define SPI_SCK  12
#define SPI_MISO 13
#define SPI_MOSI 11
#define SD_CS    10

// Objeto para el ADC1115
Adafruit_ADS1115 ads;

// Objeto para la pantalla OLED
U8G2_SH1106_128X32_VISIONOX_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE);

// Valores instantaneos del ADS1115. Sin promedio para bajar latencia.
int16_t adcRaw = 0;
float adcVoltage = 0;
float weightGrams = 0;
float pressureMmHg = 0;
float weightNewtons = 0;
bool adsReady = false;

// Servidor web y WebSocket
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
////////////////
unsigned long lastDisplayUpdate = 0;
unsigned long lastWebUpdate = 0;
const unsigned long DISPLAY_UPDATE_INTERVAL = 500;
const unsigned long WEB_UPDATE_INTERVAL = 50;
unsigned long lastSdLog = 0;
const unsigned long SD_LOG_INTERVAL = 1000;
bool sdReady = false;

// Puntos de calibración para los datos de I2C y los pesos en Newtons
struct CalibrationPoint {
  long i2cData;
  float weightGrams;
  float weightNewtons;
  bool isTara;  // Nuevo campo para indicar si el punto es la tara
};

// Definir los puntos de calibración con un nuevo campo isTara
// Puede que tengas que ajustar estos valores para los del nuevo ADC1115
CalibrationPoint calibrationPoints[] = {
  { 268,   0.0,  0.0, false },       // Sin peso (0 g)
  { 303,   9.09, 0.09, false },      // 9.09 gramos (aprox 0.09 N)
  { 354,  26.68, 0.26, false },      // 26.68 gramos (aprox 0.26 N)
  { 400,  51.0,  0.51, false },      // 51 gramos (aprox 0.51 N)
  { 458,  73.62, 0.7362, false },    // 73 gramos (aprox 0.73 N)
  { 540, 100.0,  1.0, false }        // Máximo peso (100 g, aprox 1 N)
};

// Variable global para almacenar el valor de tara
long taraValue = 0;

// Función para aplicar la tara
void applyTara() {
  // Guardar la lectura instantanea actual como tara.
  taraValue = adcRaw;

  // Imprimir el valor de tara por el puerto serial
  Serial.print("Tara activada. Valor de tara: ");
  Serial.println(taraValue);
}

// Función para determinar el tipo de contenido según la extensión del archivo
String getContentType(String filename) {
  if (filename.endsWith(".htm")) return "text/html";
  else if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".gif")) return "image/gif";
  else if (filename.endsWith(".jpg")) return "image/jpeg";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".xml")) return "text/xml";
  else if (filename.endsWith(".pdf")) return "application/x-pdf";
  else if (filename.endsWith(".zip")) return "application/x-zip";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

// Función para leer un archivo desde LittleFS y enviarlo al cliente
bool handleFileRead(String path) {
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";
  String contentType = getContentType(path);
  if (LittleFS.exists(path)) {
    File file = LittleFS.open(path, "r");
    server.sendHeader("Cache-Control", path.endsWith(".html") ? "no-cache" : "public, max-age=604800");
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);

  // Inicializar pantalla OLED primero para testear que prende
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.setCursor(0, 15);
  u8g2.print("Booting...");
  u8g2.sendBuffer();

  Wire.begin();  // Inicializa I2C default
  Wire.setClock(400000);

  // Conectar a WiFi (Timeout de 15 segundos)
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  u8g2.clearBuffer();
  u8g2.setCursor(0, 15);
  u8g2.print("Conectando WiFi...");
  u8g2.sendBuffer();

  int timeoutCounter = 0;
  while (WiFi.status() != WL_CONNECTED && timeoutCounter < 15) {
    delay(1000);
    Serial.print(".");
    timeoutCounter++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Conectado a WiFi: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Error de WiFi. Levantando Access Point local...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Vastago_Monitor", "12345678"); // Nombre y contraseña del Wi-Fi generado
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
    
    u8g2.clearBuffer();
    u8g2.setCursor(0, 15);
    u8g2.print("Modo AP Activo");
    u8g2.sendBuffer();
    delay(2000);
  }

  // Iniciar LittleFS
  if (!LittleFS.begin()) {
    Serial.println("Error al montar el sistema de archivos LittleFS");
    return;
  }

  // Inicializar ADS1115
  if (!ads.begin()) {
    Serial.println("Fallo al inicializar ADS!");
  } else {
    ads.setDataRate(RATE_ADS1115_860SPS);
    adsReady = true;
    Serial.println("ADS1115 inicializado correctamente.");
  }

  // Inicializar SPI para Tarjeta SD
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("Error al inicializar la SD");
  } else {
    sdReady = true;
    Serial.println("Tarjeta SD inicializada correctamente.");
    File logFile = SD.open("/datalog.csv", FILE_APPEND);
    if (logFile) {
        if (logFile.size() == 0) {
            logFile.println("ADC,Voltaje,Peso_Newtons,Peso_Gramos,Presion_mmHg");
        }
        logFile.close();
    } else {
        Serial.println("Error abriendo el archivo de log en SD");
    }
  }

  // Servir index.html desde LittleFS para la raíz
  server.on("/", HTTP_GET, []() {
    if(!handleFileRead("/index.html")){
      server.send(404, "text/plain", "Archivo no encontrado");
    }
  });
  
  // Modificar la función para servir `tara.html`
  server.on("/tara.html", HTTP_GET, []() {
    // Aplicar tara cuando se accede al link
    applyTara();
    server.sendHeader("Location", "/");
    server.send(303);  // Redirigir de vuelta al dashboard
  });

  // Servir cualquier otro archivo que exista en LittleFS
  server.onNotFound([](){
    if(!handleFileRead(server.uri())){
      server.send(404, "text/plain", "Archivo no encontrado");
    }
  }); 
  server.begin();
  // Iniciar WebSocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
}

// Función que calcula el peso en newtons a partir del dato del ADS1115 usando interpolación lineal
float calculateWeightNewton(long adcData) {
  // Si hay tara, conservamos el cero de calibracion y restamos el offset actual.
  if (taraValue != 0) {
    adcData = adcData - taraValue + calibrationPoints[0].i2cData;
  }

  int numPoints = sizeof(calibrationPoints) / sizeof(CalibrationPoint);

  // Si el dato está por encima del último punto, realiza extrapolación
  if (adcData > calibrationPoints[numPoints - 1].i2cData) {
    float slope = (calibrationPoints[numPoints - 1].weightNewtons - calibrationPoints[numPoints - 2].weightNewtons) /
                  (calibrationPoints[numPoints - 1].i2cData - calibrationPoints[numPoints - 2].i2cData);
    return calibrationPoints[numPoints - 1].weightNewtons + slope * (adcData - calibrationPoints[numPoints - 1].i2cData);
  }

  // Interpolación lineal entre puntos para valores dentro del rango
  for (int i = 0; i < numPoints - 1; i++) {
    if (adcData >= calibrationPoints[i].i2cData && adcData <= calibrationPoints[i + 1].i2cData) {
      float slope = (calibrationPoints[i + 1].weightNewtons - calibrationPoints[i].weightNewtons) /
                    (calibrationPoints[i + 1].i2cData - calibrationPoints[i].i2cData);
      return calibrationPoints[i].weightNewtons + slope * (adcData - calibrationPoints[i].i2cData);
    }
  }

  return 0.0;  // Retorna 0 si no se encuentra el rango
}

// Función que calcula el peso en gramos a partir del ADS1115
float calculateWeightGrams(long adcData) {
  return calculateWeightNewton(adcData) * 100.0;
}

// Función para convertir gramos a presión en mmHg
float convertGramsToMMHg(float weightInGrams) {
  const float gramsToMmHg = (9.81f / 1000.0f) / (3.14159f * 4.0f * 4.0f * 1e-6f) * 0.00750062f;
  return weightInGrams * gramsToMmHg;
}

void loop() {
  server.handleClient();
  webSocket.loop();
  
  // Si se recibe la letra 't' por el puerto serial, se activa la tara
  if (Serial.available()) {
    char receivedChar = Serial.read();
    if (receivedChar == 't' || receivedChar == 'T') {
      applyTara();  // Llamar a la función de tara
    }
  }

  if (adsReady) {
    adcRaw = ads.readADC_SingleEnded(0);
    adcVoltage = ads.computeVolts(adcRaw);
    weightNewtons = calculateWeightNewton(adcRaw);
    weightGrams = weightNewtons * 100.0f;
    pressureMmHg = convertGramsToMMHg(weightGrams);
  }

  unsigned long currentMillis = millis();

  if (currentMillis - lastWebUpdate >= WEB_UPDATE_INTERVAL) {
    lastWebUpdate = currentMillis;
    char data[128];
    snprintf(data, sizeof(data),
             "{\"weight\":%.2f,\"newtons\":%.3f,\"pressure\":%.2f,\"voltage\":%.6f,\"rawI2C\":%d}",
             weightGrams, weightNewtons, pressureMmHg, adcVoltage, adcRaw);
    webSocket.broadcastTXT(data);
  }

  if (sdReady && currentMillis - lastSdLog >= SD_LOG_INTERVAL) {
    lastSdLog = currentMillis;
    File logFile = SD.open("/datalog.csv", FILE_APPEND);
    if (logFile) {
      logFile.print(adcRaw);
      logFile.print(",");
      logFile.print(adcVoltage, 6);
      logFile.print(",");
      logFile.print(weightNewtons);
      logFile.print(",");
      logFile.print(weightGrams);
      logFile.print(",");
      logFile.println(pressureMmHg);
      logFile.close();
    }
  }
  
  if (currentMillis - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = currentMillis;
    // Muestra los datos crudos y convertidos
    Serial.print("Raw ADC Data: ");
    Serial.print(adcRaw);
    Serial.print(" Voltage: ");
    Serial.print(adcVoltage, 6);
    Serial.print(" Weight in Newtons: ");
    Serial.print(weightNewtons);
    Serial.print(" Weight in Grams: ");
    Serial.print(weightGrams);
    Serial.print(" Pressure in mmHg: ");
    Serial.println(pressureMmHg);

    // Actualizar pantalla OLED
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.setCursor(0, 10);
    if (WiFi.getMode() == WIFI_AP) {
      u8g2.print("AP IP: ");
      u8g2.print(WiFi.softAPIP().toString());
    } else {
      u8g2.print("IP: ");
      u8g2.print(WiFi.localIP().toString());
    }
    
    u8g2.setCursor(0, 25);
    u8g2.print("A0 Raw:");
    u8g2.print(adcRaw);
    u8g2.print(" (");
    u8g2.print(adcVoltage, 3);
    u8g2.print("V)");
    u8g2.sendBuffer();
  }
}

// Evento WebSocket
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_CONNECTED) {
    Serial.println("Cliente conectado");
  } else if (type == WStype_DISCONNECTED) {
    Serial.println("Cliente desconectado");
  }
}
