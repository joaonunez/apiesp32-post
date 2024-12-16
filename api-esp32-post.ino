#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <FirebaseESP32.h>
#include <DHT.h>

// Pines y configuraciones
#define MQ135_PIN_AOUT 32
#define DHT_PIN 33
#define DHT_TYPE DHT11
#define PM10_PIN 34
#define BOOT_PIN 0  // Pin del botón BOOT (GPIO 0)

// Objetos
FirebaseData firebaseData;
FirebaseConfig firebaseConfig;
FirebaseAuth firebaseAuth;
Preferences preferences;
WebServer server(80);
DHT dht(DHT_PIN, DHT_TYPE);

// Variables dinámicas
String ssid, password, dispositivoID, userEmail, userPassword;

void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(BOOT_PIN, INPUT_PULLUP);  // Configurar BOOT como entrada con pull-up

  // Verificar si se presionó el botón BOOT durante 5 segundos
  if (detectarPresionProlongada()) {
    Serial.println("⚠️ Botón BOOT presionado por 5 segundos. Restableciendo configuración...");
    resetearConfiguracion();
    iniciarServidorWeb();  // Inicia el servidor web para configuración desde cero
  } else {
    // Leer datos de memoria persistente
    preferences.begin("esp32-config", false);
    ssid = preferences.getString("ssid", "");
    password = preferences.getString("password", "");
    dispositivoID = preferences.getString("device_id", "");
    userEmail = preferences.getString("email", "");
    userPassword = preferences.getString("userpass", "");
    preferences.end();

    // Si no hay credenciales, iniciar modo AP
    if (ssid == "" || password == "") {
      Serial.println("❌ No hay credenciales. Iniciando servidor web para configuración...");
      iniciarServidorWeb();
    } else {
      conectarWiFi();
      configurarFirebase();
    }
  }
}

void loop() {
  server.handleClient();  // Maneja peticiones del servidor web
  if (WiFi.status() == WL_CONNECTED) {
    actualizarDatosFirebase();
    delay(10000);  // Intervalo de 10 segundos
  }
}

// ========================= FUNCIONES =========================

// Detectar si el botón BOOT se mantiene presionado por 5 segundos
bool detectarPresionProlongada() {
  int tiempoPresionado = 0;
  while (digitalRead(BOOT_PIN) == LOW) {
    delay(100);
    tiempoPresionado += 100;

    if (tiempoPresionado >= 5000) {  // 5 segundos
      return true;
    }
  }
  return false;
}

// Resetear configuración
void resetearConfiguracion() {
  preferences.begin("esp32-config", false);
  preferences.clear();
  preferences.end();
  Serial.println("✅ Configuración restablecida. Listo para reconfiguración.");
}

// Servidor web para configuración
void iniciarServidorWeb() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32_Config", "12345678");
  Serial.println("Servidor Web iniciado. Conéctate a 'ESP32_Config'");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", HTTP_GET, []() {
    String html = R"rawliteral(
      <!DOCTYPE html>
      <html lang="es">
      <head>
        <meta charset="UTF-8">
        <title>Configuración del ESP32</title>
        <style>
          body { font-family: Arial; background-color: #f4f4f9; color: #333; text-align: center; }
          form { background: #fff; padding: 20px; margin: auto; width: 50%; box-shadow: 0 2px 5px rgba(0,0,0,0.3); border-radius: 10px; }
          input { margin: 10px; padding: 10px; width: 80%; }
          input[type="submit"] { background-color: #4CAF50; color: white; border: none; cursor: pointer; }
          input[type="submit"]:hover { background-color: #45a049; }
        </style>
      </head>
      <body>
        <h1>Configuración del ESP32</h1>
        <form method="POST" action="/save">
          <input type="text" name="ssid" placeholder="SSID Wi-Fi" required><br>
          <input type="password" name="password" placeholder="Contraseña Wi-Fi" required><br>
          <input type="text" name="device_id" placeholder="ID del Dispositivo" required><br>
          <input type="email" name="email" placeholder="Correo Electrónico" required><br>
          <input type="password" name="userpass" placeholder="Contraseña de Usuario" required><br>
          <input type="submit" value="Guardar Configuración">
        </form>
      </body>
      </html>
    )rawliteral";
    server.send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, []() {
    ssid = server.arg("ssid");
    password = server.arg("password");
    dispositivoID = server.arg("device_id");
    userEmail = server.arg("email");
    userPassword = server.arg("userpass");

    preferences.begin("esp32-config", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.putString("device_id", dispositivoID);
    preferences.putString("email", userEmail);
    preferences.putString("userpass", userPassword);
    preferences.end();

    server.send(200, "text/html", "<h1>✅ Configuración guardada correctamente.</h1><p>Reiniciando...</p>");
    delay(1000);
    ESP.restart();
  });

  server.begin();
}

void conectarWiFi() {
  Serial.print("Conectando a Wi-Fi: ");
  Serial.println(ssid);
  WiFi.begin(ssid.c_str(), password.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\n✅ Conectado a Wi-Fi");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void configurarFirebase() {
  firebaseConfig.api_key = "AIzaSyDtQJo1wtbbGDfN8Ki0jivaldKmmR3gFPA";
  firebaseConfig.database_url = "https://ecoash-96aed-default-rtdb.firebaseio.com/";
  firebaseAuth.user.email = userEmail;
  firebaseAuth.user.password = userPassword;

  Firebase.begin(&firebaseConfig, &firebaseAuth);
  Firebase.reconnectWiFi(true);
  Serial.println("✅ Firebase configurado");
}

void actualizarDatosFirebase() {
  float temperatura = dht.readTemperature();
  float humedad = dht.readHumidity();
  int valorCrudo = analogRead(MQ135_PIN_AOUT);
  float ppm = 116.6020682 * pow(((float)valorCrudo / 4095.0), -2.769034857);
  float co = ppm * 0.5;

  int pm10Crudo = analogRead(PM10_PIN);
  float pm10 = map(pm10Crudo, 0, 4095, 0, 1000);
  float pm2_5 = pm10 * 0.65;

  String path = "/dispositivos/" + dispositivoID + "/";
  subirValorFirebase(path, "temperatura", temperatura);
  subirValorFirebase(path, "humedad", humedad);
  subirValorFirebase(path, "CO2", ppm);
  subirValorFirebase(path, "CO", co);
  subirValorFirebase(path, "PM10", pm10);
  subirValorFirebase(path, "PM2_5", pm2_5);
}

void subirValorFirebase(String path, String nombre, float valor) {
  if (Firebase.setFloat(firebaseData, path + nombre, valor)) {
    Serial.println("✅ " + nombre + " actualizado: " + String(valor));
  } else {
    Serial.print("❌ Error al subir " + nombre + ": ");
    Serial.println(firebaseData.errorReason());
  }
}
