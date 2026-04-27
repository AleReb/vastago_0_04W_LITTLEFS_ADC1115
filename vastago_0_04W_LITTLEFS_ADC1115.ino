t#include <Wire.h>  // Para I2C/SMBus
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

// Variables para los valores promediados
long i2cAvg = 0;  // Promedio de lectura I2C / ADC
const int SAMPLE_WINDOW = 8;  // Ventana corta para ver cambios rapidos en la web
long i2cSum = 0;
float weightSum = 0;
float pressureSum = 0;
int sampleCount = 0;
float weightAvgImGR = 0;
float pressureAvg = 0;
float promNewton = 0;
float voltageAvg = 0;
float voltageSum = 0;

// Servidor web y WebSocket
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);
////////////////
// Variable para controlar el intervalo de 100 ms
unsigned long lastUpdate = 0;
int delaytime = 500;
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
  // Guardar el valor de la lectura actual de i2cAvg como tara
  taraValue = i2cAvg;  

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
            logFile.println("ADC_Promedio,Voltaje,Peso_Newtons,Peso_Gramos,Presion_mmHg");
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

// Función que calcula el peso en newtons a partir del promedio de datos I2C/ADC usando interpolación lineal
float calculateWeightNewton(long avgI2CData) {
  // Si el valor de tara está configurado, ajustamos la medición
  if (taraValue != 0) {
    avgI2CData -= taraValue;  // Restamos el valor de tara de la lectura actual
  }

  int numPoints = sizeof(calibrationPoints) / sizeof(CalibrationPoint);

  // Si el dato está por encima del último punto, realiza extrapolación
  if (avgI2CData > calibrationPoints[numPoints - 1].i2cData) {
    float slope = (calibrationPoints[numPoints - 1].weightNewtons - calibrationPoints[numPoints - 2].weightNewtons) /
                  (calibrationPoints[numPoints - 1].i2cData - calibrationPoints[numPoints - 2].i2cData);
    return calibrationPoints[numPoints - 1].weightNewtons + slope * (avgI2CData - calibrationPoints[numPoints - 1].i2cData);
  }

  // Interpolación lineal entre puntos para valores dentro del rango
  for (int i = 0; i < numPoints - 1; i++) {
    if (avgI2CData >= calibrationPoints[i].i2cData && avgI2CData <= calibrationPoints[i + 1].i2cData) {
      float slope = (calibrationPoints[i + 1].weightNewtons - calibrationPoints[i].weightNewtons) /
                    (calibrationPoints[i + 1].i2cData - calibrationPoints[i].i2cData);
      return calibrationPoints[i].weightNewtons + slope * (avgI2CData - calibrationPoints[i].i2cData);
    }
  }

  return 0.0;  // Retorna 0 si no se encuentra el rango
}

// Función que calcula el peso en gramos a partir del promedio
float calculateWeightGrams(long avgI2CData) {
  float newtons = calculateWeightNewton(avgI2CData);
  float grams = newtons * 100.0;  // Convertir de newtons a gramos
  return grams;
}

// Función para convertir gramos a presión en mmHg
float convertGramsToMMHg(float weightInGrams) {
  // Convertir gramos a Newtons (1 g = 9.81 m/s^2 -> 9.81 N/kg)
  float weightInNewtons = weightInGrams * 9.81 / 1000.0;

  // Área del sensor (radio = 4 mm, ya que el diámetro es de 8 mm)
  float radius = 4.0;                        // mm
  float area = 3.14159 * (radius * radius);  // mm^2
  // Convertir área a metros cuadrados
  area = area * 1e-6;  // Convertir mm^2 a m^2
  // Calcular la presión en Pascales
  float pressurePascals = weightInNewtons / area;  // N/m^2 = Pa
  // Convertir Pascales a mmHg
  float pressureMMHg = pressurePascals * 0.00750062;  // Pa a mmHg
  return pressureMMHg;
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

  // Obtener los datos del sensor ADS1115 (Pin A0)
  short i2cData = ads.readADC_SingleEnded(0);
  float voltage = ads.computeVolts(i2cData);
  float weightGrams = calculateWeightGrams(i2cData);

  // Sumar las lecturas para calcular el promedio
  i2cSum += i2cData;
  voltageSum += voltage;
  weightSum += weightGrams;
  pressureSum += convertGramsToMMHg(weightGrams);  // Calcular la presión promedio con el peso
  sampleCount++;

  // Si se alcanzan el número de muestras, calculamos los promedios
  if (sampleCount >= SAMPLE_WINDOW) {
    i2cAvg = i2cSum / SAMPLE_WINDOW;
    voltageAvg = voltageSum / SAMPLE_WINDOW;
    weightAvgImGR = weightSum / SAMPLE_WINDOW;
    pressureAvg = pressureSum / SAMPLE_WINDOW;
    promNewton = calculateWeightNewton(i2cAvg);
    
    // Enviar los datos promedio por WebSocket
    String data = "{\"weight\": " + String(weightAvgImGR) +
                  ", \"newtons\": " + String(promNewton) +
                  ", \"pressure\": " + String(pressureAvg) +
                  ", \"voltage\": " + String(voltageAvg, 6) +
                  ", \"rawI2C\": " + String(i2cAvg) + "}";

    webSocket.broadcastTXT(data);

    // Guardar en la tarjeta SD a menor frecuencia para no frenar la lectura/web.
    unsigned long now = millis();
    if (sdReady && now - lastSdLog >= SD_LOG_INTERVAL) {
        lastSdLog = now;
        File logFile = SD.open("/datalog.csv", FILE_APPEND);
        if (logFile) {
            logFile.print(i2cAvg);
            logFile.print(",");
            logFile.print(voltageAvg, 6);
            logFile.print(",");
            logFile.print(promNewton);
            logFile.print(",");
            logFile.print(weightAvgImGR);
            logFile.print(",");
            logFile.println(pressureAvg);
            logFile.close();
        }
    }

    // Reiniciar acumuladores y contador para la siguiente ventana de muestras
    i2cSum = 0;
    voltageSum = 0;
    weightSum = 0;
    pressureSum = 0;
    sampleCount = 0;
  }
  
  unsigned long currentMillis = millis();
  // Se ejecuta cada x ms
  if (currentMillis - lastUpdate >= delaytime) {
    lastUpdate = currentMillis;
    // Muestra los datos crudos y convertidos
    Serial.print("Raw ADC Data: ");
    Serial.print(i2cData);
    Serial.print(" PROM ADC Data: ");
    Serial.print(i2cAvg);
    Serial.print(" Voltage: ");
    Serial.print(voltageAvg, 6);
    Serial.print(" Weight in Newtons: ");
    Serial.print(promNewton);
    Serial.print(" Weight in Grams: ");
    Serial.print(weightAvgImGR);
    Serial.print(" Pressure in mmHg: ");
    Serial.println(pressureAvg);

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
    u8g2.print(i2cData);
    u8g2.print(" (");
    u8g2.print(voltage, 3);
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
