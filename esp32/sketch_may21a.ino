#include <WiFi.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include <time.h>
#include "DHT.h"

#include <Firebase_ESP_Client.h>

#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// =====================================
// WIFI
// =====================================

const char* ssid = "VANESSA";
const char* password = "20101011";

// =====================================
// FIREBASE
// =====================================

#define API_KEY "AIzaSyApCSYVej5z3v2V9Qxk8KAztH-YJt1V6Gc"

#define DATABASE_URL "https://horta-inteligente-3e307-default-rtdb.firebaseio.com/"

FirebaseData fbdo;

FirebaseAuth auth;

FirebaseConfig config;

bool signupOK = false;

// =====================================
// NTP
// =====================================

const char* ntpServer = "pool.ntp.org";

const long gmtOffset_sec = -10800;

const int daylightOffset_sec = 0;

// =====================================
// DHT11
// =====================================

#define DHTPIN 4
#define DHTTYPE DHT11

DHT dht(DHTPIN, DHTTYPE);

// =====================================
// SENSORES
// =====================================

#define LDR_PIN 34
#define SOLO_PIN 32

// =====================================
// RELÉ
// =====================================

#define RELE_PIN 25

// =====================================
// IRRIGAÇÃO
// =====================================

#define LIMITE_UMIDADE 38.0

#define TEMPO_REGA 3000

#define TEMPO_ESPERA 10000

#define TEMPO_REGA_MANUAL 1000

// =====================================
// SERVIDOR
// =====================================

WebServer server(80);

// =====================================
// VARIÁVEIS
// =====================================

float temperatura;
float umidadeAr;

int luminosidade;

float leituraSolo;
float porcentagemSolo;

float mediaTemperatura = 0;
float mediaUmidadeAr = 0;
float mediaSolo = 0;
float mediaLuminosidade = 0;

int totalLeituras = 0;

String statusIrrigacao = "DESLIGADA";

String logsHTML = "";

bool regaManualSolicitada = false;

bool ultimoSoloSeco = false;

bool primeiroStatusSolo = true;

unsigned long ultimoHistorico = 0;

// =====================================
// HORÁRIO
// =====================================

String obterHorario() {

  struct tm timeinfo;

  if (!getLocalTime(&timeinfo)) {

    return "SEM HORA";
  }

  char horario[30];

  strftime(
    horario,
    sizeof(horario),
    "%H:%M:%S",
    &timeinfo
  );

  return String(horario);
}

// =====================================
// LOG
// =====================================

void adicionarLog(String mensagem) {

  String logCompleto =
    "[" +
    obterHorario() +
    "] " +
    mensagem;

  Serial.println(logCompleto);

  if (
    mensagem.indexOf(
      "☁ Status atualizado Firebase"
    ) == -1
  ) {

    logsHTML =
      "<p>" +
      logCompleto +
      "</p>" +
      logsHTML;
  }

  if (
    logsHTML.length() > 15000
  ) {

    logsHTML =
      logsHTML.substring(0, 15000);
  }
}

// =====================================
// FIREBASE
// =====================================

void enviarFirebase(
  bool salvarHistorico = false
) {

  FirebaseJson json;

  json.set(
    "temperatura",
    temperatura
  );

  json.set(
    "umidadeAr",
    umidadeAr
  );

  json.set(
    "luminosidade",
    luminosidade
  );

  json.set(
    "umidadeSolo",
    String(
      porcentagemSolo,
      1
    )
  );

  json.set(
    "statusIrrigacao",
    statusIrrigacao
  );

  json.set(
    "horario",
    obterHorario()
  );

  bool sucessoAtual =
    Firebase.RTDB.setJSON(
      &fbdo,
      "/statusAtual",
      &json
    );

  if (!sucessoAtual) {

    adicionarLog(
      "❌ Firebase erro: " +
      fbdo.errorReason()
    );
  }

  if (salvarHistorico) {

    bool sucessoHistorico =
      Firebase.RTDB.pushJSON(
        &fbdo,
        "/historico",
        &json
      );

    if (sucessoHistorico) {

      adicionarLog(
        "📚 Histórico salvo"
      );
    }

    else {

      adicionarLog(
        "❌ Erro histórico: " +
        fbdo.errorReason()
      );
    }
  }
}

// =====================================
// HTML
// =====================================

String paginaHTML() {

  return R"rawliteral(

<!DOCTYPE html>
<html>

<head>

<meta charset="UTF-8">

<title>Horta Inteligente</title>

<style>

body {

  font-family: Arial;

  background:
  linear-gradient(
    to right,
    #c8f7c5,
    #e8ffe8
  );

  text-align: center;

  padding: 20px;
}

.botao {

  display: inline-block;

  margin: 10px;

  padding: 14px 24px;

  background: #2e7d32;

  color: white;

  text-decoration: none;

  border-radius: 12px;

  font-weight: bold;
}

</style>

</head>

<body>

<h1>🌱 Horta Inteligente</h1>

<a class="botao" href="/logs">

VER LOGS

</a>

<a class="botao" href="/regar">

REGAR MANUALMENTE

</a>

</body>
</html>

)rawliteral";
}

// =====================================
// LOGS
// =====================================

String paginaLogs() {

  String html = R"rawliteral(

<!DOCTYPE html>
<html>

<head>

<meta charset="UTF-8">

<meta http-equiv="refresh" content="2">

<style>

body {

  background: #111;

  color: #00ff88;

  font-family: Arial;

  padding: 20px;
}

p {

  background: #222;

  padding: 10px;

  border-radius: 8px;
}

.botao {

  display: inline-block;

  margin-bottom: 20px;

  padding: 12px 18px;

  background: red;

  color: white;

  text-decoration: none;

  border-radius: 10px;
}

</style>

</head>

<body>

<h1>📜 Logs</h1>

<a class="botao" href="/limparlogs">

LIMPAR LOGS

</a>

)rawliteral";

  html += logsHTML;

  html += "</body></html>";

  return html;
}

// =====================================
// SETUP
// =====================================

void setup() {

  Serial.begin(115200);

  dht.begin();

  pinMode(RELE_PIN, OUTPUT);

  digitalWrite(RELE_PIN, HIGH);

  adicionarLog(
    "Conectando WiFi..."
  );

  WiFi.begin(
    ssid,
    password
  );

  while (
    WiFi.status() != WL_CONNECTED
  ) {

    delay(500);

    Serial.print(".");
  }

  adicionarLog(
    "WiFi conectado!"
  );

  adicionarLog(
    "IP: " +
    WiFi.localIP().toString()
  );

  configTime(
    gmtOffset_sec,
    daylightOffset_sec,
    ntpServer
  );

  // =====================================
  // FIREBASE
  // =====================================

  config.api_key = API_KEY;

  config.database_url =
    DATABASE_URL;

  config.token_status_callback =
    tokenStatusCallback;

  if (
    Firebase.signUp(
      &config,
      &auth,
      "",
      ""
    )
  ) {

    signupOK = true;

    adicionarLog(
      "Firebase signup OK"
    );
  }

  else {

    adicionarLog(
      config.signer
      .signupError
      .message
      .c_str()
    );
  }

  Firebase.begin(
    &config,
    &auth
  );

  Firebase.reconnectWiFi(
    true
  );

  adicionarLog(
    "Firebase conectado!"
  );

  // =====================================
  // ROTAS
  // =====================================

  server.enableCORS(true);

  server.on("/", []() {

    server.send(
      200,
      "text/html",
      paginaHTML()
    );
  });

  server.on("/logs", []() {

    server.send(
      200,
      "text/html",
      paginaLogs()
    );
  });

  server.on("/limparlogs", []() {

    logsHTML = "";

    adicionarLog(
      "🗑 Logs apagados"
    );

    server.send(
      200,
      "text/html",
      "Logs apagados"
    );
  });

  server.on("/regar", []() {

    server.sendHeader(
      "Access-Control-Allow-Origin",
      "*"
    );

    regaManualSolicitada =
      true;

    server.send(
      200,
      "text/plain",
      "OK"
    );
  });

  ElegantOTA.begin(&server);

  server.begin();

  adicionarLog(
    "Servidor iniciado!"
  );

  adicionarLog(
    "OTA ativo!"
  );
}

// =====================================
// LOOP
// =====================================

void loop() {

  server.handleClient();

  ElegantOTA.loop();

  // =====================================
  // LEITURAS
  // =====================================

  temperatura =
    dht.readTemperature();

  umidadeAr =
    dht.readHumidity();

  luminosidade =
    analogRead(LDR_PIN);

  // =====================================
  // SOLO COM DECIMAL
  // =====================================

  leituraSolo =
    analogRead(SOLO_PIN);

  porcentagemSolo =
    (
      (
        4095.0 - leituraSolo
      )
      /
      (
        4095.0 - 1200.0
      )
    ) * 100.0;

  porcentagemSolo =
    constrain(
      porcentagemSolo,
      0,
      100
    );

  porcentagemSolo =
    round(
      porcentagemSolo * 10
    ) / 10.0;

  // =====================================
  // STATUS SOLO
  // =====================================

  bool soloSecoAtual =
    (
      porcentagemSolo <
      LIMITE_UMIDADE
    );

  if (
    primeiroStatusSolo ||
    soloSecoAtual != ultimoSoloSeco
  ) {

    if (soloSecoAtual) {

      adicionarLog(
        "🌱 Solo seco ("
        +
        String(
          porcentagemSolo,
          1
        )
        +
        "%)"
      );
    }

    else {

      adicionarLog(
        "✅ Solo úmido ("
        +
        String(
          porcentagemSolo,
          1
        )
        +
        "%)"
      );
    }

    ultimoSoloSeco =
      soloSecoAtual;

    primeiroStatusSolo =
      false;
  }

  // =====================================
  // REGA MANUAL
  // =====================================

  server.handleClient();

  if (
    regaManualSolicitada
  ) {

    adicionarLog(
      "💧 Rega manual iniciada"
    );

    statusIrrigacao =
      "LIGADA";

    if (
      Firebase.ready()
      && signupOK
    ) {

      enviarFirebase(true);
    }

    digitalWrite(
      RELE_PIN,
      LOW
    );

    delay(
      TEMPO_REGA_MANUAL
    );

    digitalWrite(
      RELE_PIN,
      HIGH
    );

    statusIrrigacao =
      "DESLIGADA";

    adicionarLog(
      "✅ Rega manual finalizada"
    );

    regaManualSolicitada =
      false;
  }

  // =====================================
  // REGA AUTOMÁTICA
  // =====================================

  else if (
    soloSecoAtual
  ) {

    statusIrrigacao =
      "LIGADA";

    adicionarLog(
      "💧 Irrigando automaticamente"
    );

    if (
      Firebase.ready()
      && signupOK
    ) {

      enviarFirebase(true);
    }

    digitalWrite(
      RELE_PIN,
      LOW
    );

    delay(
      TEMPO_REGA
    );

    digitalWrite(
      RELE_PIN,
      HIGH
    );

    statusIrrigacao =
      "DESLIGADA";

    adicionarLog(
      "⏳ Aguardando absorção"
    );

    delay(
      TEMPO_ESPERA
    );
  }

  else {

    digitalWrite(
      RELE_PIN,
      HIGH
    );

    statusIrrigacao =
      "DESLIGADA";
  }

  // =====================================
  // HISTÓRICO
  // =====================================

  bool salvarHistorico =
    false;

  if (
    millis() -
    ultimoHistorico
    >
    300000
  ) {

    salvarHistorico =
      true;

    ultimoHistorico =
      millis();
  }

  // =====================================
  // FIREBASE
  // =====================================

  if (
    Firebase.ready()
    && signupOK
  ) {

    enviarFirebase(
      salvarHistorico
    );
  }

  delay(300);
}