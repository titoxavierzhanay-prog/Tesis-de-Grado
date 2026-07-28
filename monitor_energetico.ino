#include "EmonLib.h"
#include <WiFiMulti.h>
#include <InfluxDbClient.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <WebServer.h>

// --------------------- SENSORES ---------------------
EnergyMonitor emon1;
EnergyMonitor emon2;
EnergyMonitor emon3;

#define CUR_ADC_INPUT1 34
#define CUR_ADC_INPUT2 35
#define CUR_ADC_INPUT3 33

#define VOL_ADC_INPUT1 32
#define VOL_ADC_INPUT2 36
#define VOL_ADC_INPUT3 39

#define DESFASE 1.7

float supplyVoltage = 0, Irms = 0, realPower = 0, apparentPower = 0, powerFactor = 0;
float supplyVoltage2 = 0, Irms2 = 0, realPower2 = 0, apparentPower2 = 0, powerFactor2 = 0;
float supplyVoltage3 = 0, Irms3 = 0, realPower3 = 0, apparentPower3 = 0, powerFactor3 = 0;

// --------------------- WIFI Y CREDENCIALES ---------------------
// Las credenciales reales NO se suben al repositorio.
// Cópialas en un archivo local "secrets.h" (ver secrets.h.example)
// y agrega "secrets.h" a tu .gitignore
#include "secrets.h"

WiFiMulti wifiMulti;
// WIFI_SSID, WIFI_PASSWORD, INFLUXDB_URL, INFLUXDB_TOKEN,
// INFLUXDB_ORG y INFLUXDB_BUCKET se definen en secrets.h

InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);

Point sensor1("corriente1");
Point sensor2("corriente2");
Point sensor3("corriente3");

// Variables de estado para InfluxDB
bool influxdb_connected = false;

// --------------------- OLED ---------------------
#define i2c_Address 0x3c
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

unsigned long previousMillis = 0;
const long interval = 4000;  // 4 segundos para mejor fluidez
int textCounter = 0;
const int numTexts = 6;
bool wifi_connected = false;
bool showing_wifi_status = false;
unsigned long wifi_check_time = 0;
const long wifi_check_interval = 10000;  // CAMBIADO A 10 SEGUNDOS

// Variable para almacenar IP
String localIP = "";
// Variable para almacenar la dirección base de InfluxDB
String influxdbBaseIP = "energy-monitor-unl.local";

// --------------------- WEB - PUERTO 8080 ---------------------
WebServer server(8080);

// Función simple para mostrar estado WiFi en OLED
void mostrar_estado_wifi(String mensaje, String detalle = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // Título centrado
  display.setCursor(20, 2);
  display.println("MONITOR UNL");

  // Línea separadora simple
  for (int i = 10; i < 118; i++) {
    display.drawPixel(i, 12, SH110X_WHITE);
  }

  // Mensaje principal centrado
  int16_t x = (SCREEN_WIDTH - mensaje.length() * 6) / 2;
  if (x < 0) x = 2;
  display.setCursor(x, 22);
  display.println(mensaje);

  // Detalle si existe
  if (detalle.length() > 0) {
    int16_t x2 = (SCREEN_WIDTH - detalle.length() * 6) / 2;
    if (x2 < 0) x2 = 2;
    display.setCursor(x2, 34);
    display.println(detalle);
  }

  // IP si está conectado
  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(2, 50);
    display.print("IP: ");
    display.println(WiFi.localIP());
  }

  display.display();
}

void verificar_wifi() {
  wifi_connected = (WiFi.status() == WL_CONNECTED);
  if (!wifi_connected) {
    showing_wifi_status = true;
    mostrar_estado_wifi("RECONECTANDO...");
    Serial.println("WiFi desconectado - Reconectando...");

    // RECONEXIÓN ACTIVA MEJORADA
    int intentos = 0;
    while (wifiMulti.run() != WL_CONNECTED && intentos < 15) {
      delay(1000);
      Serial.print(".");
      intentos++;

      // Mostrar progreso en OLED
      String progress = "Intento " + String(intentos) + "/15";
      mostrar_estado_wifi("RECONECTANDO...", progress);
    }

    // Verificar si se reconectó
    if (WiFi.status() == WL_CONNECTED) {
      wifi_connected = true;
      localIP = WiFi.localIP().toString();
      Serial.println("\nWiFi reconectado exitosamente!");
      Serial.println("Nueva IP: " + localIP);
      mostrar_estado_wifi("RECONEXION OK", "IP: " + localIP);
      delay(2000);
      showing_wifi_status = false;
    } else {
      Serial.println("\nError: No se pudo reconectar WiFi");
      mostrar_estado_wifi("ERROR WIFI", "Reintentando...");
      // Si no se puede reconectar, reiniciar el WiFi completamente
      WiFi.disconnect();
      delay(1000);
      WiFi.mode(WIFI_OFF);
      delay(1000);
      WiFi.mode(WIFI_STA);
      delay(1000);
      wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
    }
  } else if (showing_wifi_status) {
    localIP = WiFi.localIP().toString();
    mostrar_estado_wifi("CONEXION OK", "Sistema listo");
    Serial.println("WiFi estable - IP: " + localIP);
    delay(2000);
    showing_wifi_status = false;
  }
}

// Función para verificar y reconectar InfluxDB
void verificar_influxdb() {
  if (wifi_connected) {
    Serial.println("Verificando conexión InfluxDB...");
    if (client.validateConnection()) {
      if (!influxdb_connected) {
        influxdb_connected = true;
        Serial.println("InfluxDB conectado exitosamente");
      }
    } else {
      influxdb_connected = false;
      Serial.print("Error InfluxDB: ");
      Serial.println(client.getLastErrorMessage());

      // Intentar configurar certificados para HTTPS si es necesario
      if (client.getLastErrorMessage().indexOf("certificate") != -1) {
        Serial.println("Problema de certificado - configurando modo inseguro");
        client.setInsecure();  // Solo para testing, no recomendado en producción
      }
    }
  } else {
    influxdb_connected = false;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n=== INICIANDO SISTEMA MONITOR ENERGETICO UNL ===");

  pinMode(CUR_ADC_INPUT1, INPUT);
  pinMode(CUR_ADC_INPUT2, INPUT);
  pinMode(CUR_ADC_INPUT3, INPUT);
  pinMode(VOL_ADC_INPUT1, INPUT);
  pinMode(VOL_ADC_INPUT2, INPUT);
  pinMode(VOL_ADC_INPUT3, INPUT);

  // Inicializar OLED
  if (!display.begin(i2c_Address, true)) {
    Serial.println("Error: No se pudo inicializar OLED SH1106");
  } else {
    Serial.println("OLED SH1106 inicializado correctamente");
  }

  display.clearDisplay();
  mostrar_estado_wifi("INICIANDO", "Monitor UNL");
  delay(3000);

  // Sensores y calibracion de sensores
  Serial.println("Inicializando sensores de energía...");
  emon1.voltage(VOL_ADC_INPUT1, 203.8, DESFASE);
  emon1.current(CUR_ADC_INPUT1, 51.6);
  emon2.voltage(VOL_ADC_INPUT2, 198.2, DESFASE);
  emon2.current(CUR_ADC_INPUT2, 51.3);
  emon3.voltage(VOL_ADC_INPUT3, 169.19, DESFASE);
  emon3.current(CUR_ADC_INPUT3, 93.1);

  // WiFi
  Serial.println("Conectando a WiFi...");
  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);

  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (wifiMulti.run() != WL_CONNECTED && attempts < 30) {
    Serial.print(".");
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifi_connected = true;
    localIP = WiFi.localIP().toString();
    Serial.println("\n=== WiFi CONECTADO ===");
    Serial.print("IP Address: ");
    Serial.println(localIP);
    Serial.print("Signal Strength (RSSI): ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");
    mostrar_estado_wifi("WIFI OK", localIP);
    delay(3000);
  } else {
    wifi_connected = false;
    Serial.println("\n=== ERROR WiFi ===");
    Serial.println("No se pudo conectar a WiFi");
    mostrar_estado_wifi("ERROR WIFI", "Modo offline");
    delay(2000);
  }

  // InfluxDB
  if (wifi_connected) {
    Serial.println("\n=== CONFIGURANDO INFLUXDB ===");
    Serial.print("URL: ");
    Serial.println(INFLUXDB_URL);
    Serial.print("Bucket: ");
    Serial.println(INFLUXDB_BUCKET);
    Serial.print("Org: ");
    Serial.println(INFLUXDB_ORG);

    // Configurar cliente InfluxDB
    client.setConnectionParams(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN);

    verificar_influxdb();

    if (influxdb_connected) {
      mostrar_estado_wifi("DB OK", "Sistema listo");
      Serial.println("InfluxDB configurado correctamente");
      delay(1000);
    } else {
      mostrar_estado_wifi("DB ERROR", "Sistema parcial");
      Serial.println("InfluxDB no disponible - continuando sin base de datos");
      delay(2000);
    }
  }

  // Servidor web en puerto 8080
  Serial.println("\n=== INICIANDO SERVIDOR WEB ===");

  server.on("/", []() {
    String page = R"rawliteral(
<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Monitor Trifásico ESP32 - UNL</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/Chart.js/3.9.1/chart.min.js"></script>
<style>
  :root{
    --bg-dark: #0B0C10;
    --panel-dark: #1a1d23;
    --border-dark: #2a2d35;
    --accent1: #2E86FF;
    --accent2: #28A745;
    --accent3: #FFC107;
    --text-light: #E6EEF3;
    --text-primary: #ffffff;
    --muted: #9aa4ad;
    --table-bg: rgba(255,255,255,0.02);
    --voltage-color: #2196F3;
    --current-color: #F44336;
    --real-power-color: #4CAF50;
    --apparent-power-color: #FF9800;
    --power-factor-color: #9C27B0;
  }
  
  html,body{height:100%;margin:0;font-family:'Segoe UI',Roboto,Arial;background:var(--bg-dark);color:var(--text-light);font-size:18px;}
  .container{display:flex;flex-direction:column;min-height:100vh;}
  
  header{display:flex;align-items:center;gap:12px;padding:20px 30px;background:var(--panel-dark);border-bottom:2px solid var(--border-dark);box-shadow:0 4px 12px rgba(0,0,0,0.15);}
  header .title{font-size:28px;font-weight:700;color:var(--text-primary);}
  
  .dashboard{padding:30px;flex:1;}
  
  .cards{display:grid;grid-template-columns:repeat(5,1fr);gap:15px;margin-bottom:30px;}
  .card{background:var(--panel-dark);border:2px solid var(--border-dark);padding:15px;border-radius:15px;box-shadow:0 4px 12px rgba(0,0,0,0.1);}
  .card .label{font-size:12px;color:var(--muted);margin-bottom:5px;}
  .card .value{font-size:20px;font-weight:700;color:var(--text-primary);margin-bottom:5px;}
  .card .description{font-size:10px;color:var(--muted);}
  
  .data-section{background:var(--panel-dark);border:2px solid var(--border-dark);border-radius:15px;padding:25px;margin-bottom:30px;}
  .data-section h3{font-size:24px;margin:0 0 20px 0;color:var(--text-primary);border-bottom:2px solid var(--border-dark);padding-bottom:10px;}
  
  table{width:100%;border-collapse:collapse;color:var(--text-light);}
  th,td{padding:15px 10px;text-align:center;font-size:16px;border-bottom:1px solid var(--border-dark);}
  th{background:var(--border-dark);color:var(--text-primary);}
  .sensor-name{font-weight:600;color:var(--text-primary);}
  
  .charts-section{background:var(--panel-dark);border:2px solid var(--border-dark);border-radius:15px;padding:25px;margin-bottom:30px;}
  .charts-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(400px,1fr));gap:20px;}
  .chart-container{background:rgba(255,255,255,0.02);padding:15px;border-radius:10px;border:1px solid var(--border-dark);}
  .chart-title{text-align:center;font-size:18px;font-weight:600;color:var(--text-primary);margin-bottom:10px;}
  .chart-description{text-align:center;font-size:12px;color:var(--muted);margin-bottom:15px;}
  .chart-canvas{height:300px;}
  
  .telegram-section{background:var(--panel-dark);border:2px solid var(--border-dark);border-radius:15px;padding:20px;margin-bottom:20px;}
  .telegram-link{display:inline-block;color:white;padding:12px 24px;border-radius:8px;text-decoration:none;font-weight:600;margin:5px;background:#0088cc;}
  .telegram-link:hover{background:#0077bb;transform:translateY(-2px);}
  
  .justification-section{background:var(--panel-dark);border:2px solid var(--border-dark);border-radius:15px;padding:25px;margin-bottom:30px;}
  .justification-text{line-height:1.6;text-align:justify;}
  .parameter-list{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:15px;margin-top:20px;}
  .parameter-item{background:rgba(255,255,255,0.05);padding:15px;border-radius:8px;border-left:4px solid var(--accent1);}
  .parameter-title{font-weight:600;color:var(--text-primary);margin-bottom:5px;}
  .parameter-desc{font-size:14px;color:var(--muted);}
  
  .footer{display:flex;justify-content:space-between;align-items:center;padding:15px 30px;color:var(--muted);background:var(--panel-dark);border-top:2px solid var(--border-dark);}
  
  @media (max-width:768px){
    .cards{grid-template-columns:repeat(2,1fr);}
    .parameter-list{grid-template-columns:1fr;}
    .charts-grid{grid-template-columns:1fr;}
    th,td{font-size:14px;padding:10px 5px;}
  }
</style>
</head>
<body>
  <div class="container">
    <header>
      <div class="title">Monitor Energético Trifásico - UNL</div>
    </header>

    <div class="dashboard">
      <div class="telegram-section">
        <h3>🤖 Bot de Telegram</h3>
        <p>Consulta los datos de energía desde Telegram:</p>
        <a href="https://t.me/energia_unl_bot" class="telegram-link" target="_blank">🚀 Sistema Energético UNL</a>
        <small style="color:var(--muted);display:block;margin-top:10px;">
          Nombre del bot: Sistema Energético UNL | Usuario: @energia_unl_bot
        </small>
      </div>

      <div class="cards">
        <div class="card">
          <div class="label">⚡ Voltaje Promedio</div>
          <div class="value" id="avgVoltage">-- V</div>
          <div class="description">Promedio de los 3 sensores</div>
        </div>
        <div class="card">
          <div class="label">🔌 Corriente Total</div>
          <div class="value" id="totalCurrent">-- A</div>
          <div class="description">Suma de corrientes</div>
        </div>
        <div class="card">
          <div class="label">💡 Potencia Real Total</div>
          <div class="value" id="totalRealPower">-- kW</div>
          <div class="description">Potencia real total</div>
        </div>
        <div class="card">
          <div class="label">⚡ Potencia Aparente Total</div>
          <div class="value" id="totalApparentPower">-- kVA</div>
          <div class="description">Potencia aparente total</div>
        </div>
        <div class="card">
          <div class="label">📊 Factor Potencia Prom.</div>
          <div class="value" id="avgPowerFactor">--</div>
          <div class="description">Eficiencia del sistema</div>
        </div>
      </div>

      <div class="data-section">
        <h3>📈 Datos en Tiempo Real</h3>
        <table>
          <thead>
            <tr>
              <th>Sensor</th>
              <th>Voltaje (V)</th>
              <th>Corriente (A)</th>
              <th>P. Real (kW)</th>
              <th>P. Aparente (kVA)</th>
              <th>Factor Potencia</th>
            </tr>
          </thead>
          <tbody>
            <tr>
              <td class="sensor-name">Sensor 1</td>
              <td id="v1">--</td>
              <td id="i1">--</td>
              <td id="p1">--</td>
              <td id="pa1">--</td>
              <td id="pf1">--</td>
            </tr>
            <tr>
              <td class="sensor-name">Sensor 2</td>
              <td id="v2">--</td>
              <td id="i2">--</td>
              <td id="p2">--</td>
              <td id="pa2">--</td>
              <td id="pf2">--</td>
            </tr>
            <tr>
              <td class="sensor-name">Sensor 3</td>
              <td id="v3">--</td>
              <td id="i3">--</td>
              <td id="p3">--</td>
              <td id="pa3">--</td>
              <td id="pf3">--</td>
            </tr>
          </tbody>
        </table>
      </div>

      <div class="charts-section">
        <h3>📊 Gráficos en Tiempo Real</h3>
        <div class="charts-grid">
          <div class="chart-container">
            <div class="chart-title">Sensor 1 - Mediciones</div>
            <div class="chart-description">Voltaje (azul), Corriente (rojo), Potencia Real (verde), Potencia Aparente (naranja), Factor de Potencia (morado)</div>
            <div class="chart-canvas">
              <canvas id="chart1"></canvas>
            </div>
          </div>
          
          <div class="chart-container">
            <div class="chart-title">Sensor 2 - Mediciones</div>
            <div class="chart-description">Voltaje (azul), Corriente (rojo), Potencia Real (verde), Potencia Aparente (naranja), Factor de Potencia (morado)</div>
            <div class="chart-canvas">
              <canvas id="chart2"></canvas>
            </div>
          </div>
          
          <div class="chart-container">
            <div class="chart-title">Sensor 3 - Mediciones</div>
            <div class="chart-description">Voltaje (azul), Corriente (rojo), Potencia Real (verde), Potencia Aparente (naranja), Factor de Potencia (morado)</div>
            <div class="chart-canvas">
              <canvas id="chart3"></canvas>
            </div>
          </div>
        </div>
      </div>

      <div class="justification-section">
        <h3>🎓 Justificación Técnica para Eficiencia Energética Universitaria</h3>
        <div class="justification-text">
          El monitoreo de estas variables eléctricas es fundamental para optimizar la eficiencia energética en edificaciones universitarias. 
          La medición continua permite identificar patrones de consumo, detectar equipos ineficientes y establecer estrategias de ahorro energético 
          que reducen costos operativos y la huella de carbono institucional.
        </div>
        
        <div class="parameter-list">
          <div class="parameter-item">
            <div class="parameter-title">⚡ Voltaje (V)</div>
            <div class="parameter-desc">Monitoreo de estabilidad de red. Variaciones indican problemas de suministro que afectan la eficiencia de equipos.</div>
          </div>
          <div class="parameter-item">
            <div class="parameter-title">🔌 Corriente (A)</div>
            <div class="parameter-desc">Indica la demanda real de energía. Permite detectar sobrecargas y optimizar la distribución de cargas.</div>
          </div>
          <div class="parameter-item">
            <div class="parameter-title">💡 Potencia Real (kW)</div>
            <div class="parameter-desc">Energía útil consumida. Base para cálculos de costos y análisis de eficiencia energética por área o equipo.</div>
          </div>
          <div class="parameter-item">
            <div class="parameter-title">⚡ Potencia Aparente (kVA)</div>
            <div class="parameter-desc">Potencia total del sistema. Su comparación con la potencia real indica la calidad del suministro eléctrico.</div>
          </div>
          <div class="parameter-item">
            <div class="parameter-title">📊 Factor de Potencia</div>
            <div class="parameter-desc">Indicador de eficiencia. Valores bajos (<0.9) significan pérdidas y penalizaciones tarifarias evitables.</div>
          </div>
        </div>
      </div>

      <div class="footer">
        <div id="lastUpdate">Última actualización: --</div>
        <div>IP del Sistema: <span id="systemIP">--</span></div>
      </div>
    </div>
  </div>

<script>
let chart1, chart2, chart3;
const maxDataPoints = 20;

const chartData = {
  sensor1: {
    labels: [],
    voltage: [],
    current: [],
    realPower: [],
    apparentPower: [],
    powerFactor: []
  },
  sensor2: {
    labels: [],
    voltage: [],
    current: [],
    realPower: [],
    apparentPower: [],
    powerFactor: []
  },
  sensor3: {
    labels: [],
    voltage: [],
    current: [],
    realPower: [],
    apparentPower: [],
    powerFactor: []
  }
};

const chartConfig = {
  type: 'line',
  options: {
    responsive: true,
    maintainAspectRatio: false,
    plugins: {
      legend: {
        position: 'bottom',
        labels: {
          color: '#E6EEF3',
          font: {
            size: 10
          }
        }
      }
    },
    scales: {
      x: {
        ticks: {
          color: '#9aa4ad',
          font: {
            size: 10
          }
        },
        grid: {
          color: '#2a2d35'
        }
      },
      y: {
        ticks: {
          color: '#9aa4ad',
          font: {
            size: 10
          }
        },
        grid: {
          color: '#2a2d35'
        }
      }
    },
    elements: {
      line: {
        tension: 0.1
      },
      point: {
        radius: 2
      }
    }
  }
};

function initCharts() {
  const ctx1 = document.getElementById('chart1').getContext('2d');
  const ctx2 = document.getElementById('chart2').getContext('2d');
  const ctx3 = document.getElementById('chart3').getContext('2d');

  chart1 = new Chart(ctx1, {
    ...chartConfig,
    data: {
      labels: chartData.sensor1.labels,
      datasets: [
        {
          label: 'Voltaje (V)',
          data: chartData.sensor1.voltage,
          borderColor: '#2196F3',
          backgroundColor: 'rgba(33, 150, 243, 0.1)',
          fill: false
        },
        {
          label: 'Corriente (A)',
          data: chartData.sensor1.current,
          borderColor: '#F44336',
          backgroundColor: 'rgba(244, 67, 54, 0.1)',
          fill: false
        },
        {
          label: 'P. Real (kW)',
          data: chartData.sensor1.realPower,
          borderColor: '#4CAF50',
          backgroundColor: 'rgba(76, 175, 80, 0.1)',
          fill: false
        },
        {
          label: 'P. Aparente (kVA)',
          data: chartData.sensor1.apparentPower,
          borderColor: '#FF9800',
          backgroundColor: 'rgba(255, 152, 0, 0.1)',
          fill: false
        },
        {
          label: 'Factor Potencia',
          data: chartData.sensor1.powerFactor,
          borderColor: '#9C27B0',
          backgroundColor: 'rgba(156, 39, 176, 0.1)',
          fill: false
        }
      ]
    }
  });

chart2 = new Chart(ctx2, {
    ...chartConfig,
    data: {
      labels: chartData.sensor2.labels,
      datasets: [
        {
          label: 'Voltaje (V)',
          data: chartData.sensor2.voltage,
          borderColor: '#2196F3',
          backgroundColor: 'rgba(33, 150, 243, 0.1)',
          fill: false
        },
        {
          label: 'Corriente (A)',
          data: chartData.sensor2.current,
          borderColor: '#F44336',
          backgroundColor: 'rgba(244, 67, 54, 0.1)',
          fill: false
        },
        {
          label: 'P. Real (kW)',
          data: chartData.sensor2.realPower,
          borderColor: '#4CAF50',
          backgroundColor: 'rgba(76, 175, 80, 0.1)',
          fill: false
        },
        {
          label: 'P. Aparente (kVA)',
          data: chartData.sensor2.apparentPower,
          borderColor: '#FF9800',
          backgroundColor: 'rgba(255, 152, 0, 0.1)',
          fill: false
        },
        {
          label: 'Factor Potencia',
          data: chartData.sensor2.powerFactor,
          borderColor: '#9C27B0',
          backgroundColor: 'rgba(156, 39, 176, 0.1)',
          fill: false
        }
      ]
    }
  });

chart3 = new Chart(ctx3, {
    ...chartConfig,
    data: {
      labels: chartData.sensor3.labels,
      datasets: [
        {
          label: 'Voltaje (V)',
          data: chartData.sensor3.voltage,
          borderColor: '#2196F3',
          backgroundColor: 'rgba(33, 150, 243, 0.1)',
          fill: false
        },
        {
          label: 'Corriente (A)',
          data: chartData.sensor3.current,
          borderColor: '#F44336',
          backgroundColor: 'rgba(244, 67, 54, 0.1)',
          fill: false
        },
        {
          label: 'P. Real (kW)',
          data: chartData.sensor3.realPower,
          borderColor: '#4CAF50',
          backgroundColor: 'rgba(76, 175, 80, 0.1)',
          fill: false
        },
        {
          label: 'P. Aparente (kVA)',
          data: chartData.sensor3.apparentPower,
          borderColor: '#FF9800',
          backgroundColor: 'rgba(255, 152, 0, 0.1)',
          fill: false
        },
        {
          label: 'Factor Potencia',
          data: chartData.sensor3.powerFactor,
          borderColor: '#9C27B0',
          backgroundColor: 'rgba(156, 39, 176, 0.1)',
          fill: false
        }
      ]
    }
  });
}

function updateChartData(sensorData, data) {
  const now = new Date();
  const timeLabel = now.toLocaleTimeString();

  sensorData.labels.push(timeLabel);
  sensorData.voltage.push(parseFloat(data.voltage));
  sensorData.current.push(parseFloat(data.current));
  sensorData.realPower.push(parseFloat(data.realPower));
  sensorData.apparentPower.push(parseFloat(data.apparentPower));
  sensorData.powerFactor.push(parseFloat(data.powerFactor));

  if (sensorData.labels.length > maxDataPoints) {
    sensorData.labels.shift();
    sensorData.voltage.shift();
    sensorData.current.shift();
    sensorData.realPower.shift();
    sensorData.apparentPower.shift();
    sensorData.powerFactor.shift();
  }
}

async function fetchData() {
  try {
    const response = await fetch('/datos');
    const data = await response.json();
    
    document.getElementById('v1').textContent = parseFloat(data.Voltaje1).toFixed(1);
    document.getElementById('i1').textContent = parseFloat(data.Corriente1).toFixed(2);
    document.getElementById('p1').textContent = parseFloat(data.PotenciaReal).toFixed(3);
    document.getElementById('pa1').textContent = parseFloat(data.PotenciaAparente).toFixed(3);
    document.getElementById('pf1').textContent = parseFloat(data.FactorPotencia1).toFixed(2);
    
    document.getElementById('v2').textContent = parseFloat(data.Voltaje2).toFixed(1);
    document.getElementById('i2').textContent = parseFloat(data.Corriente2).toFixed(2);
    document.getElementById('p2').textContent = parseFloat(data.PotenciaReal2).toFixed(3);
    document.getElementById('pa2').textContent = parseFloat(data.PotenciaAparente2).toFixed(3);
    document.getElementById('pf2').textContent = parseFloat(data.FactorPotencia2).toFixed(2);
    
    document.getElementById('v3').textContent = parseFloat(data.Voltaje3).toFixed(1);
    document.getElementById('i3').textContent = parseFloat(data.Corriente3).toFixed(2);
    document.getElementById('p3').textContent = parseFloat(data.PotenciaReal3).toFixed(3);
    document.getElementById('pa3').textContent = parseFloat(data.PotenciaAparente3).toFixed(3);
    document.getElementById('pf3').textContent = parseFloat(data.FactorPotencia3).toFixed(2);
    
    const avgVoltage = (parseFloat(data.Voltaje1) + parseFloat(data.Voltaje2) + parseFloat(data.Voltaje3)) / 3;
    const totalCurrent = parseFloat(data.Corriente1) + parseFloat(data.Corriente2) + parseFloat(data.Corriente3);
    const totalRealPower = parseFloat(data.PotenciaReal) + parseFloat(data.PotenciaReal2) + parseFloat(data.PotenciaReal3);
    const totalApparentPower = parseFloat(data.PotenciaAparente) + parseFloat(data.PotenciaAparente2) + parseFloat(data.PotenciaAparente3);
    const avgPF = (parseFloat(data.FactorPotencia1) + parseFloat(data.FactorPotencia2) + parseFloat(data.FactorPotencia3)) / 3;
    
    document.getElementById('avgVoltage').textContent = avgVoltage.toFixed(1) + ' V';
    document.getElementById('totalCurrent').textContent = totalCurrent.toFixed(2) + ' A';
    document.getElementById('totalRealPower').textContent = totalRealPower.toFixed(3) + ' kW';
    document.getElementById('totalApparentPower').textContent = totalApparentPower.toFixed(3) + ' kVA';
    document.getElementById('avgPowerFactor').textContent = avgPF.toFixed(2);
    
    updateChartData(chartData.sensor1, {
      voltage: data.Voltaje1,
      current: data.Corriente1,
      realPower: data.PotenciaReal,
      apparentPower: data.PotenciaAparente,
      powerFactor: data.FactorPotencia1
    });

    updateChartData(chartData.sensor2, {
      voltage: data.Voltaje2,
      current: data.Corriente2,
      realPower: data.PotenciaReal2,
      apparentPower: data.PotenciaAparente2,
      powerFactor: data.FactorPotencia2
    });

    updateChartData(chartData.sensor3, {
      voltage: data.Voltaje3,
      current: data.Corriente3,
      realPower: data.PotenciaReal3,
      apparentPower: data.PotenciaAparente3,
      powerFactor: data.FactorPotencia3
    });

    chart1.update('none');
    chart2.update('none');
    chart3.update('none');
    
    document.getElementById('lastUpdate').textContent = 'Última actualización: ' + new Date().toLocaleTimeString();
    
  } catch (error) {
    console.error('Error fetching data:', error);
  }
}

fetch('/ip')
  .then(response => response.text())
  .then(ip => {
    document.getElementById('systemIP').textContent = ip + ':8080';
  })
  .catch(error => {
    console.error('Error getting IP:', error);
    document.getElementById('systemIP').textContent = 'No disponible';
  });

document.addEventListener('DOMContentLoaded', function() {
  initCharts();
  fetchData();
  setInterval(fetchData, 2000);
});
</script>
</body>
</html>
)rawliteral";
    server.send(200, "text/html", page);
  });

  server.on("/datos", []() {
    float v1 = abs(supplyVoltage);
    float i1 = abs(Irms);
    float p1 = abs(realPower) / 1000.0;  // Convertir W a kW
    float pa1 = abs(apparentPower) / 1000.0;  // Convertir VA a kVA
    float pf1 = abs(powerFactor);
    if (pf1 > 1.0) pf1 = 1.0;

    float v2 = abs(supplyVoltage2);
    float i2 = abs(Irms2);
    float p2 = abs(realPower2) / 1000.0;  // Convertir W a kW
    float pa2 = abs(apparentPower2) / 1000.0;  // Convertir VA a kVA
    float pf2 = abs(powerFactor2);
    if (pf2 > 1.0) pf2 = 1.0;

    float v3 = abs(supplyVoltage3);
    float i3 = abs(Irms3);
    float p3 = abs(realPower3) / 1000.0;  // Convertir W a kW
    float pa3 = abs(apparentPower3) / 1000.0;  // Convertir VA a kVA
    float pf3 = abs(powerFactor3);
    if (pf3 > 1.0) pf3 = 1.0;

    String json = "{";
    json += "\"Voltaje1\":" + String(v1, 1) + ",\"Corriente1\":" + String(i1, 2) + ",\"PotenciaReal\":" + String(p1, 3) + ",\"PotenciaAparente\":" + String(pa1, 3) + ",\"FactorPotencia1\":" + String(pf1, 2) + ",";
    json += "\"Voltaje2\":" + String(v2, 1) + ",\"Corriente2\":" + String(i2, 2) + ",\"PotenciaReal2\":" + String(p2, 3) + ",\"PotenciaAparente2\":" + String(pa2, 3) + ",\"FactorPotencia2\":" + String(pf2, 2) + ",";
    json += "\"Voltaje3\":" + String(v3, 1) + ",\"Corriente3\":" + String(i3, 2) + ",\"PotenciaReal3\":" + String(p3, 3) + ",\"PotenciaAparente3\":" + String(pa3, 3) + ",\"FactorPotencia3\":" + String(pf3, 2);
    json += "}";
    server.send(200, "application/json", json);
  });

  server.on("/ip", []() {
    server.send(200, "text/plain", WiFi.localIP().toString());
  });

  server.begin();

  if (wifi_connected) {
    Serial.println("=== SERVIDOR WEB INICIADO ===");
    Serial.print("Accede al dashboard en: http://");
    Serial.print(localIP);
    Serial.println(":8080");
    Serial.println("Rutas disponibles:");
    Serial.println("  / - Dashboard principal");
    Serial.println("  /datos - API JSON con datos de sensores");
    Serial.println("  /ip - Obtener IP del dispositivo");
  }

  Serial.println("\n=== SISTEMA INICIADO CORRECTAMENTE ===");
  Serial.println("Monitoreando sensores cada 5 segundos...");
}

void loop() {
  server.handleClient();

  emon1.calcVI(30, 2000);
  supplyVoltage = abs(emon1.Vrms);
  Irms = abs(emon1.calcIrms(1480));
  realPower = abs(emon1.realPower);
  apparentPower = abs(emon1.apparentPower);
  powerFactor = abs(emon1.powerFactor);
  if (powerFactor > 1.0) powerFactor = 1.0;

  emon2.calcVI(30, 2000);
  supplyVoltage2 = abs(emon2.Vrms);
  Irms2 = abs(emon2.calcIrms(1480));
  realPower2 = abs(emon2.realPower);
  apparentPower2 = abs(emon2.apparentPower);
  powerFactor2 = abs(emon2.powerFactor);
  if (powerFactor2 > 1.0) powerFactor2 = 1.0;

  emon3.calcVI(30, 2000);
  supplyVoltage3 = abs(emon3.Vrms);
  Irms3 = abs(emon3.calcIrms(1480));
  if (Irms3 < 0) Irms3 = 0;
  realPower3 = abs(emon3.realPower);
  apparentPower3 = abs(emon3.apparentPower);
  powerFactor3 = abs(emon3.powerFactor);
  if (powerFactor3 > 1.0) powerFactor3 = 1.0;

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    Serial.println("\n╔══════════════════════════════════════════╗");
    Serial.println("║           LECTURA DE SENSORES            ║");
    Serial.println("╠══════════════════════════════════════════╣");
    Serial.print("║ Tiempo: ");
    Serial.print(millis() / 1000);
    Serial.println(" segundos                    ║");
    Serial.println("╠══════════════════════════════════════════╣");
    Serial.println("║ SENSOR 1:                                ║");
    Serial.print("║   Voltaje: ");
    Serial.print(supplyVoltage, 1);
    Serial.println("V                      ║");
    Serial.print("║   Corriente: ");
    Serial.print(Irms, 2);
    Serial.println("A                    ║");
    Serial.print("║   Potencia Real: ");
    Serial.print(realPower / 1000.0, 3);
    Serial.println("kW               ║");
    Serial.print("║   Potencia Aparente: ");
    Serial.print(apparentPower / 1000.0, 3);
    Serial.println("kVA         ║");
    Serial.print("║   Factor Potencia: ");
    Serial.print(powerFactor, 2);
    Serial.println("               ║");
    Serial.println("║                                          ║");
    Serial.println("║ SENSOR 2:                                ║");
    Serial.print("║   Voltaje: ");
    Serial.print(supplyVoltage2, 1);
    Serial.println("V                      ║");
    Serial.print("║   Corriente: ");
    Serial.print(Irms2, 2);
    Serial.println("A                    ║");
    Serial.print("║   Potencia Real: ");
    Serial.print(realPower2 / 1000.0, 3);
    Serial.println("kW               ║");
    Serial.print("║   Potencia Aparente: ");
    Serial.print(apparentPower2 / 1000.0, 3);
    Serial.println("kVA         ║");
    Serial.print("║   Factor Potencia: ");
    Serial.print(powerFactor2, 2);
    Serial.println("               ║");
    Serial.println("║                                          ║");
    Serial.println("║ SENSOR 3:                                ║");
    Serial.print("║   Voltaje: ");
    Serial.print(supplyVoltage3, 1);
    Serial.println("V                      ║");
    Serial.print("║   Corriente: ");
    Serial.print(Irms3, 2);
    Serial.println("A                    ║");
    Serial.print("║   Potencia Real: ");
    Serial.print(realPower3 / 1000.0, 3);
    Serial.println("kW               ║");
    Serial.print("║   Potencia Aparente: ");
    Serial.print(apparentPower3 / 1000.0, 3);
    Serial.println("kVA         ║");
    Serial.print("║   Factor Potencia: ");
    Serial.print(powerFactor3, 2);
    Serial.println("               ║");
    Serial.println("╠══════════════════════════════════════════╣");

    float totalPower = (realPower + realPower2 + realPower3) / 1000.0;
    Serial.print("║ POTENCIA TOTAL: ");
    Serial.print(totalPower, 3);
    Serial.println("kW                ║");

    if (wifi_connected) {
      Serial.print("║ IP del Sistema: ");
      Serial.print(localIP);
      Serial.println(":8080        ║");
      Serial.print("║ Dashboard: http://");
      Serial.print(localIP);
      Serial.println(":8080     ║");
    }

    Serial.print("║ Estado WiFi: ");
    Serial.print(wifi_connected ? "CONECTADO" : "DESCONECTADO");
    Serial.println("        ║");
    Serial.print("║ Estado InfluxDB: ");
    Serial.print(influxdb_connected ? "CONECTADO" : "ERROR");
    Serial.println("       ║");
    Serial.println("╚══════════════════════════════════════════╝");

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SH110X_WHITE);

    if (textCounter == 0) {
      display.setCursor(30, 2);
      display.print("SENSOR 1");

      for (int i = 10; i < 118; i++) {
        display.drawPixel(i, 12, SH110X_WHITE);
      }

      display.setCursor(2, 16);
      display.print("V: ");
      display.print(supplyVoltage, 1);
      display.print("V");

      display.setCursor(2, 26);
      display.print("I: ");
      display.print(Irms, 2);
      display.print("A");

      display.setCursor(2, 36);
      display.print("P: ");
      display.print(realPower / 1000.0, 3);
      display.print("kW");

      display.setCursor(2, 46);
      display.print("PA: ");
      display.print(apparentPower / 1000.0, 3);
      display.print("kVA");

      display.setCursor(2, 56);
      display.print("FP: ");
      display.print(powerFactor, 2);
    } else if (textCounter == 1) {
      display.setCursor(30, 2);
      display.print("SENSOR 2");

      for (int i = 10; i < 118; i++) {
        display.drawPixel(i, 12, SH110X_WHITE);
      }

      display.setCursor(2, 16);
      display.print("V: ");
      display.print(supplyVoltage2, 1);
      display.print("V");

      display.setCursor(2, 26);
      display.print("I: ");
      display.print(Irms2, 2);
      display.print("A");

      display.setCursor(2, 36);
      display.print("P: ");
      display.print(realPower2 / 1000.0, 3);
      display.print("kW");

      display.setCursor(2, 46);
      display.print("PA: ");
      display.print(apparentPower2 / 1000.0, 3);
      display.print("kVA");

      display.setCursor(2, 56);
      display.print("FP: ");
      display.print(powerFactor2, 2);
    } else if (textCounter == 2) {
      display.setCursor(30, 2);
      display.print("SENSOR 3");

      for (int i = 10; i < 118; i++) {
        display.drawPixel(i, 12, SH110X_WHITE);
      }

      display.setCursor(2, 16);
      display.print("V: ");
      display.print(supplyVoltage3, 1);
      display.print("V");

      display.setCursor(2, 26);
      display.print("I: ");
      display.print(Irms3, 2);
      display.print("A");

      display.setCursor(2, 36);
      display.print("P: ");
      display.print(realPower3 / 1000.0, 3);
      display.print("kW");

      display.setCursor(2, 46);
      display.print("PA: ");
      display.print(apparentPower3 / 1000.0, 3);
      display.print("kVA");

      display.setCursor(2, 56);
      display.print("FP: ");
      display.print(powerFactor3, 2);
    } else if (textCounter == 3) {
      display.setCursor(25, 2);
      display.print("WEB - UNL");

      for (int i = 10; i < 118; i++) {
        display.drawPixel(i, 12, SH110X_WHITE);
      }

      display.setCursor(2, 20);
      display.print("Acceder via web:");

      display.setCursor(2, 35);
      display.print(localIP);
      display.print(":8080");

      display.setCursor(2, 50);
      display.print("Dashboard UNL");
    } else if (textCounter == 4) {
      display.setCursor(25, 2);
      display.print("RESUMEN");

      for (int i = 10; i < 118; i++) {
        display.drawPixel(i, 12, SH110X_WHITE);
      }

      float totalPower = (realPower + realPower2 + realPower3) / 1000.0;
      display.setCursor(2, 18);
      display.print("P.Total: ");
      display.print(totalPower, 3);
      display.print("kW");

      float totalCurrent = Irms + Irms2 + Irms3;
      display.setCursor(2, 28);
      display.print("I.Total: ");
      display.print(totalCurrent, 2);
      display.print("A");

      float avgVoltage = (supplyVoltage + supplyVoltage2 + supplyVoltage3) / 3;
      display.setCursor(2, 38);
      display.print("V.Prom: ");
      display.print(avgVoltage, 1);
      display.print("V");

      display.setCursor(2, 48);
      display.print("Tiempo: ");
      display.print(millis() / 60000);
      display.print(" min");
    } else if (textCounter == 5) {
      display.setCursor(15, 2);
      display.print("BOT TELEGRAM");

      for (int i = 10; i < 118; i++) {
        display.drawPixel(i, 12, SH110X_WHITE);
      }

      display.setCursor(2, 20);
      display.print("Consultar datos");
      display.setCursor(2, 30);
      display.print("energia UNL via");
      display.setCursor(2, 40);
      display.print("Telegram Bot");

      display.setCursor(2, 54);
      display.print("@energia_unl_bot");
    }

    display.display();
    textCounter++;
    if (textCounter >= numTexts) textCounter = 0;

    if (wifi_connected && influxdb_connected) {
      Serial.println("Enviando datos a InfluxDB...");

      sensor1.clearFields();
      sensor1.addField("Voltaje1", supplyVoltage);
      sensor1.addField("Corriente1", Irms);
      sensor1.addField("PotenciaReal", realPower / 1000.0);
      sensor1.addField("PotenciaAparente", apparentPower / 1000.0);
      sensor1.addField("FactorPotencia1", powerFactor);

      sensor2.clearFields();
      sensor2.addField("Voltaje2", supplyVoltage2);
      sensor2.addField("Corriente2", Irms2);
      sensor2.addField("PotenciaReal2", realPower2 / 1000.0);
      sensor2.addField("PotenciaAparente2", apparentPower2 / 1000.0);
      sensor2.addField("FactorPotencia2", powerFactor2);

      sensor3.clearFields();
      sensor3.addField("Voltaje3", supplyVoltage3);
      sensor3.addField("Corriente3", Irms3);
      sensor3.addField("PotenciaReal3", realPower3 / 1000.0);
      sensor3.addField("PotenciaAparente3", apparentPower3 / 1000.0);
      sensor3.addField("FactorPotencia3", powerFactor3);

      bool success1 = client.writePoint(sensor1);
      bool success2 = client.writePoint(sensor2);
      bool success3 = client.writePoint(sensor3);

      if (success1 && success2 && success3) {
        Serial.println("Datos enviados a InfluxDB correctamente");
      } else {
        Serial.println("Error enviando datos a InfluxDB:");
        if (!success1) Serial.println("  - Error sensor1: " + client.getLastErrorMessage());
        if (!success2) Serial.println("  - Error sensor2: " + client.getLastErrorMessage());
        if (!success3) Serial.println("  - Error sensor3: " + client.getLastErrorMessage());

        influxdb_connected = false;
      }
    } else if (wifi_connected && !influxdb_connected) {
      verificar_influxdb();
    }
  }

  if (currentMillis - wifi_check_time >= wifi_check_interval) {
    wifi_check_time = currentMillis;
    verificar_wifi();
  }
}
