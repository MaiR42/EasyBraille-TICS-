/*COMENTARIOS

Version 1.0.2

Instalar ARDUINO IDE

Board:

ESP32 byEspressif Systems

Librerias:

PCF8574 by Rob Tillaart

ArduinoWebsockets by Gil Maimon



**** SIMULACION PARA 1 SOLO MODULO BRAILLE ****

=====CODIGO PARA CONFIGURACION CON EL EXPANSOR GPIO, CONSIDERAR QUE LOS BOTONES SE CONECTAN AL ESP32-CAM=====

ORDEN DE DESIGNACION DE PINES PARA LOS MOD. BRAILLE:

6 3
5 2
4 1

ERRORES:
- No se muestran los primeros 3 caracteres
- Revisar si se muestran correctamente los simbolos

CAMBIOS REALIZADOS:
- BAJE RESOLUCIÓN DE LA CÁMARA
- MSJS DE DEBUG EN SETUP()
- OTROS
*/

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>

#include <Wire.h>
#include <PCF8574.h>

#include <ArduinoWebsockets.h> // No funciona nada de la simulacion relacionado a websocket
using namespace websockets;
WebsocketsClient client;

const int NUM_LEDS_POR_MODULO = 6;
PCF8574 expansor(0x20); // Dirección I2C del PCF8574 // El 0x20 puede ser que deba cambiar
String palabra = "";

/*----------------------MODIFICABLES------------------------*/

const char* ssid = "Nombre_red_wifi";
const char* password = "Contraseña_wifi";
const char* serverIP = "192.168.1.100"; // IP del servidor local (PC)
const int serverPort = 8080;

const int NUM_MODULOS = 1; // Cantidad de modulos Braille

// Configuración de pines del expansor para los módulos braille
const int braillePins[NUM_MODULOS][NUM_LEDS_POR_MODULO] = { 
    {0, 1, 2, 3, 4, 5}    // Módulo 1
  };

// Hay que modificar el codigo dependiendo si se agregan o quitan estas variables
int letra1;   // Índices de letras actuales   

#define previous_word_btn 12   // Botón palabra anterior
#define next_word_btn 13    // Botón palabra siguiente
#define BUTTON_PIN 16      // Botón camara

// Config. del expansor en setup()

/*---------------------------------------------------------*/


// -------------Mapa de pines del ESP32-------------

/* PINES DISPONIBLES EN UN ESP32-CAM:

GPIO 0          - Nose pero no tocar uwu arigato
GPIO 1  (TX)    -    NO USAR
GPIO 3  (RX)    -    NO USAR

GPIO 12         - Disponible   BTN ANT
GPIO 13         - Disponible   BTN SIG
GPIO 14         - Disponible   EXPANSOR
GPIO 15         - Disponible   EXPANSOR
GPIO 16         - Disponible   BTN CAM

GPIO 2          - LED interno
GPIO 4          - LED flash

*/

/*----------------------------------------------*/

#define FLASH_GPIO_NUM     4  // Pin del flash
bool flashEnabled = true; 

// Configuracion pines de la camara
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// Mapa de caracteres braille (a-z)
const unsigned char brailleMap[26] = {
  0b000001, // a - ⠁
  0b000011, // b - ⠃
  0b001001, // c - ⠉
  0b011001, // d - ⠙
  0b010001, // e - ⠑
  0b001011, // f - ⠋
  0b011011, // g - ⠛
  0b010011, // h - ⠓
  0b001010, // i - ⠊
  0b011010, // j - ⠚
  0b000101, // k - ⠅
  0b000111, // l - ⠇
  0b001101, // m - ⠍
  0b011101, // n - ⠝
  0b010101, // o - ⠕
  0b001111, // p - ⠏
  0b011111, // q - ⠟
  0b010111, // r - ⠗
  0b001110, // s - ⠎
  0b011110, // t - ⠞
  0b100101, // u - ⠥
  0b100111, // v - ⠧
  0b111010, // w - ⠺
  0b101101, // x - ⠭
  0b111101, // y - ⠽
  0b110101  // z - ⠵
};

// Mapa de números braille (0-9)
const unsigned char numberMap[10] = {
  0b011010, // 0 - ⠚
  0b000001, // 1 - ⠁
  0b000011, // 2 - ⠃
  0b001001, // 3 - ⠉
  0b011001, // 4 - ⠙
  0b010001, // 5 - ⠑
  0b001011, // 6 - ⠋
  0b011011, // 7 - ⠛
  0b010011, // 8 - ⠓
  0b001010  // 9 - ⠊
};

// Caracteres especiales
// Realizar correccion si es necesaria
const unsigned char specialCharMap[][2] = {
  {' ', 0b000000}, // espacio
  {'.', 0b000100}, // punto
  {',', 0b000010}, // coma
  {';', 0b000110}, // punto y coma
  {':', 0b010010}, // dos puntos
  {'?', 0b100110}, // interrogación
  {'!', 0b010110}, // exclamación
  {'-', 0b100100}, // guión
  {'#', 0b111100}  // indicador numérico
};

bool palabra_existe = false;  // Flag para palabra existente

bool buttonPressed = false; // Para boton de la camara
bool lastButtonState = HIGH;

bool expansorDisponible = false;
/*-------------------------------------------------------------------------------*/

void setup() {
  Serial.begin(115200);
  Serial.println("Sistema Braille Iniciado");
  //new
  printResetReason();
  //
  pinMode(FLASH_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_GPIO_NUM, LOW);
  
  //Debug
  Serial.println("DEBUG CONFIG FLASH");

  // Configuración expansor  
  Wire.begin(14, 15); // SDA = GPIO14, SCL = GPIO15 on ESP32-CAM
  Wire.setClock(100000);

    //Debug
    Serial.println("DEBUG CONFIG Expansor (begin y clock)");

  //new 
  
  if (!initExpansor()) {
    Serial.println("ERROR: No se pudo inicializar el expansor PCF8574");
    Serial.println("Continuando sin expansor...");
    // No terminar la ejecución, continuar sin expansor
  }

  //Debug
  Serial.println("DEBUG INICIALIZACION EXPANSOR");
  
  // Conectar a WiFi
  connectToWiFi();

  //Debug
  Serial.println("DEBUG CONEXION WIFI");


   /*Configuracion botones*/
  pinMode(BUTTON_PIN, INPUT_PULLUP); // Boton camara
  pinMode(previous_word_btn, INPUT_PULLUP); // Boton palabra ant
  pinMode(next_word_btn, INPUT_PULLUP); // Boton palabra sig

  //Debug
  Serial.println("DEBUG CONFIG BOTONES CAM, PALABRAS ANT/SIG");

  /*Inicializar Camara*/
  if (!initCamera()) {
    Serial.println("¡Error fatal! No se pudo inicializar la cámara");
    while (true); // Detener ejecución
  }
  //Debug
  Serial.println("DEBUG INICIALIZACION CÁMARA");

  // Configuracion del WebSocket para recibir texto del OCR
  if (WiFi.status() == WL_CONNECTED) {
    client.onMessage(onMessageCallback);
    client.onEvent(onEventsCallback);
    connectWebSocket();
  } else {
    Serial.println("WiFi no disponible - WebSocket no conectado");
  }

  //Debug
  Serial.println("DEBUG INICIALIZACION WEBSOCKET");

  Serial.println("Sistema listo. Presiona el botón para tomar foto.");

  if (expansorDisponible) {
    testLEDs();
  }
  limpiarLEDs();

  delay(500);
  //fin setup()
}



// 3. Función para inicializar y verificar el expansor
bool initExpansor() {
  expansor.begin();
  
  // Verificar si el PCF8574 responde
  Wire.beginTransmission(0x20);
  byte error = Wire.endTransmission();
  
  if (error == 0) {
    Serial.println("PCF8574 encontrado y funcionando");
    expansorDisponible = true;
    
    // Inicializar pines del expansor
    for (int modulo = 0; modulo < NUM_MODULOS; modulo++) {
      for (int led = 0; led < NUM_LEDS_POR_MODULO; led++) {
        expansor.write(braillePins[modulo][led], LOW);
      }
    }
    return true;
  } else {
    Serial.println("PCF8574 no responde en dirección 0x20");
    expansorDisponible = false;
    return false;
  }
}

unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

void loop() {
  // Verificar estado WiFi de forma más segura
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 30000) { // Cada 30 segundos
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi perdido. Reconectando...");
      connectToWiFi();
    }
    lastWiFiCheck = millis();
  }

  // Manejo de botón con protección adicional
  bool currentButtonState = digitalRead(BUTTON_PIN);
  if (lastButtonState == HIGH && currentButtonState == LOW) {
    if (millis() - lastDebounceTime > debounceDelay) {
      buttonPressed = true;
      lastDebounceTime = millis();
    }
  }
  lastButtonState = currentButtonState;

  if (buttonPressed) {
    buttonPressed = false;
    Serial.println("Botón presionado - Tomando foto...");
    if (WiFi.status() == WL_CONNECTED) {
      takePhotoAndSend();
    } else {
      Serial.println("WiFi no disponible - no se puede enviar foto");
    }
  }

  // WebSocket solo si WiFi está conectado
  if (WiFi.status() == WL_CONNECTED) {
    client.poll();
  }

  manejarBotonAnterior();
  manejarBotonSiguiente();
  actualizarEstadoPalabra();
}


/*FUNCIONES PARA EL SISTEMA*/

/*===============F. SOLO CAMARA, ENVIADO Y RECIBIDO DE FOTOS, Y RELACIONADOS===============*/
bool initCamera() {

  
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // Calidad de imagen
  config.frame_size = FRAMESIZE_VGA;  // 640 x 480
  config.jpeg_quality = 12;            // 0-63 (menor = mejor calidad)
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Error inicializando camara: 0x%x", err);
    return false;
  }
  
  return true;
}

void printResetReason() {
  esp_reset_reason_t reason = esp_reset_reason();
  
  Serial.print("Motivo del reinicio: ");
  switch(reason) {
    case ESP_RST_POWERON: Serial.println("Encendido"); break;
    case ESP_RST_SW: Serial.println("Software"); break;
    case ESP_RST_PANIC: Serial.println("Pánico/Excepción"); break;
    case ESP_RST_INT_WDT: Serial.println("Watchdog Interrupción"); break;
    case ESP_RST_TASK_WDT: Serial.println("Watchdog Tarea"); break;
    case ESP_RST_WDT: Serial.println("Watchdog General"); break;
    case ESP_RST_DEEPSLEEP: Serial.println("Deep Sleep"); break;
    case ESP_RST_BROWNOUT: Serial.println("Brownout (Voltaje bajo)"); break;
    default: Serial.println("Desconocido"); break;
  }
}

void connectToWiFi() {
    WiFi.disconnect(true);
    delay(1000);
    WiFi.mode(WIFI_STA);
    
    // AGREGAR: Configurar hostname para evitar conflictos
    WiFi.setHostname("ESP32-CAM-Braille");
  
    Serial.printf("Conectando a %s ", ssid);
    WiFi.begin(ssid, password);
    
    int intentos = 0;
    while (WiFi.status() != WL_CONNECTED && intentos < 30) { // Más intentos
      delay(500);
      Serial.print(".");
      intentos++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nConectado!");
      Serial.print("Dirección IP: ");
      Serial.println(WiFi.localIP());
      
      // AGREGAR: Verificar estabilidad de conexión
      delay(2000); // Esperar estabilización
      
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Conexión WiFi estable");
      } else {
        Serial.println("Conexión WiFi inestable");
      }
    } else {
      Serial.println("\nNo se pudo conectar a WiFi");
      Serial.println("Continuando sin WiFi...");
    }
}

void takePhotoAndSend() {
  Serial.println("Preparando para tomar foto...");
  
  // Activar flash (si está habilitado)
  if(flashEnabled) {
    digitalWrite(FLASH_GPIO_NUM, HIGH);
    delay(200);  
    Serial.println("Flash activado");
  }
  
  // Capturar foto
  camera_fb_t *fb = esp_camera_fb_get();
  
  // Desactivar flash
  if(flashEnabled) {
    digitalWrite(FLASH_GPIO_NUM, LOW);
    Serial.println("Flash desactivado");
  }

  if(!fb) {
    Serial.println("Error: No se pudo capturar la imagen");
    return;
  }

  Serial.printf("Imagen capturada: %zu bytes\n", fb->len);

  // Enviar imagen al servidor =========
  if(WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String serverUrl = "http://" + String(serverIP) + ":" + String(serverPort) + "/upload";
    
    http.begin(serverUrl);
    http.addHeader("Content-Type", "image/jpeg");
    
    Serial.println("Enviando foto a: " + serverUrl);
    int httpResponseCode = http.POST(fb->buf, fb->len);
    
    if(httpResponseCode > 0) {
      String response = http.getString();
      Serial.printf("Respuesta del servidor: %d - %s\n", httpResponseCode, response.c_str());
    } else {
      Serial.printf("Error enviando imagen: %d\n", httpResponseCode);
    }
    
    http.end();
  } else {
    Serial.println("Error: WiFi desconectado");
  }
  // Liberar memoria del framebuffer
  esp_camera_fb_return(fb);
}

void connectWebSocket() {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi no conectado - no se puede conectar WebSocket");
      return;
    }
    
    String wsUrl = "ws://" + String(serverIP) + ":" + String(serverPort) + "/ws";
    
    // Agregar timeout y manejo de errores
    bool connected = client.connect(wsUrl);
    if (connected) {
      Serial.println("WebSocket conectado exitosamente");
    } else {
      Serial.println("Error conectando WebSocket");
    }
}

void onMessageCallback(WebsocketsMessage message) {
  palabra = message.data();
  palabra_existe = (palabra != "");
  letra1 = 0;
  mostrarLetras();
  Serial.println("=== TEXTO RECIBIDO ===");
  Serial.println(palabra);
  Serial.println("========================");
}

void onEventsCallback(WebsocketsEvent event, String data) {
  if(event == WebsocketsEvent::ConnectionOpened) {
    Serial.println("WebSocket conectado!");
  } else if(event == WebsocketsEvent::ConnectionClosed) {
    Serial.println("WebSocket desconectado. Reconectando...");
    delay(1000);
    connectWebSocket();
  }
}



/*===============FUNCIONES DE LAS CELDAS BRAILLE Y RELACIONADOS===============*/

void activarCeldasBraille(String texto) {
  palabra = texto; // Asignar el texto directamente
  letra1 = 0;
  mostrarLetras();
}

void manejarBotonAnterior() {
  static unsigned long lastPrevBtnPress = 0;
  static int lastPrevBtnState = HIGH;
  int currentPrevBtnState = digitalRead(previous_word_btn);
  
  if(currentPrevBtnState == LOW && lastPrevBtnState == HIGH && millis() - lastPrevBtnPress > 300) {
    lastPrevBtnPress = millis();
    if(palabra_existe && letra1 > 0) {
      letra1 -= NUM_MODULOS;
      mostrarLetras();
    }
  }
  lastPrevBtnState = currentPrevBtnState;
}

void manejarBotonSiguiente() {
  static unsigned long lastNextBtnPress = 0;
  static int lastNextBtnState = HIGH;
  int currentNextBtnState = digitalRead(next_word_btn);
  
  if(currentNextBtnState == LOW && lastNextBtnState == HIGH && millis() - lastNextBtnPress > 300) {
    lastNextBtnPress = millis();
    if(palabra_existe && letra1 < palabra.length() - 1) {
      letra1 += NUM_MODULOS;
      mostrarLetras();
    }
  }
  lastNextBtnState = currentNextBtnState;
}

void actualizarEstadoPalabra() {
  palabra_existe = (palabra != "");
}

// Funciones de visualización braille
void mostrarLetras() {
  //new
  if (!palabra_existe || palabra.length() == 0) {
    limpiarLEDs();
    return;
  }
  //
  if (!palabra_existe || letra1 >= palabra.length()) return;

  limpiarLEDs();

  if (letra1 < palabra.length()) mostrarCaracter(0, letra1);

  // Debug
  Serial.print("Mostrando: ");
  if (letra1 < palabra.length()) Serial.print(palabra.charAt(letra1));
}

void mostrarCaracter(int modulo, int posicion) {
  if (posicion >= palabra.length()) return;
  
  char caracter = palabra.charAt(posicion);
  unsigned char patron = 0;

  if (isDigit(caracter)) {
    // Los números se muestran directamente con su patrón
    patron = numberMap[caracter - '0'];
  } 
  else if (isAlpha(caracter)) {
    // Letras
    patron = brailleMap[tolower(caracter) - 'a'];
  }
  else {
    // Caracteres especiales
    bool encontrado = false;
    for (int i = 0; i < sizeof(specialCharMap) / sizeof(specialCharMap[0]); i++) {
      if (specialCharMap[i][0] == caracter) {
        patron = specialCharMap[i][1];
        encontrado = true;
        break;
      }
    }
    
    // Si no se encuentra el carácter, usar patrón vacío
    if (!encontrado) {
      patron = 0b000000;
    }
  }
  
  mostrarPatron(modulo, patron);
}

void mostrarPatron(int modulo, unsigned char patron) {
  if (!expansorDisponible) {
    Serial.println("Expansor no disponible - simulando patrón");
    return;
  }
  
  for (int led = 0; led < NUM_LEDS_POR_MODULO; led++) {
    bool ledEncendido = (patron & (1 << led)) != 0;
    expansor.write(braillePins[modulo][led], ledEncendido ? HIGH : LOW);
  }
}

void limpiarLEDs() {
    if (!expansorDisponible) {
      Serial.println("Expansor no disponible - no se pueden limpiar LEDs");
      return; // SALIR SI NO HAY EXPANSOR
    }
    
    for (int modulo = 0; modulo < NUM_MODULOS; modulo++) {
      for (int led = 0; led < NUM_LEDS_POR_MODULO; led++) {
        expansor.write(braillePins[modulo][led], LOW);
      }
    }
}

void testLEDs() {
  if (!expansorDisponible) {
    Serial.println("Test LEDs omitido - expansor no disponible");
    return;
  }
  
  // Prueba todos los LEDs
  for (int modulo = 0; modulo < NUM_MODULOS; modulo++) {
    for (int led = 0; led < NUM_LEDS_POR_MODULO; led++) {
      expansor.write(braillePins[modulo][led], HIGH);
    }
  }
  delay(1000);
  limpiarLEDs();

  // Prueba por módulo
  for (int modulo = 0; modulo < NUM_MODULOS; modulo++) {
    for (int led = 0; led < NUM_LEDS_POR_MODULO; led++) {
      expansor.write(braillePins[modulo][led], HIGH);
      delay(100);
      expansor.write(braillePins[modulo][led], LOW);
    }
  }
}
