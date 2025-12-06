#include <WiFi.h>
#include <WebServer.h>
#include <Servo.h>

// ========= WIFI ==========
//const char* ssid = "FiberHGW_TPFAC0_2.4GHz";
//const char* password = "dNWwNHKD";
const char* ssid = "Redmi11";
const char* password = "123456789";

// ========= PINS ==========
#define SERVO_X_PIN  13
#define SERVO_Y_PIN  12
#define RELAY_PIN    26    // Water pump relay

// ========= OBJECTS =========
WebServer server(80);

Servo servoX;
Servo servoY;

// Default aim angles
int angleX = 90;
int angleY = 90;

String htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>ESP32 Aimbot</title>
<style>
body { background:#111; color:white; text-align:center; font-family:sans-serif; }
#aimArea {
  width:100vw;
  height:90vh;
  border:2px solid #444;
  background:#222;
  cursor:crosshair;
}
h2 { color:#0f0; }
</style>
</head>

<body>
<h2>ESP32 AIMBOT CONTROLLER</h2>
<p>Move mouse = aim | Left click = FIRE | Right click = RELOAD</p>

<div id="aimArea"></div>

<script>
const aimArea = document.getElementById("aimArea");

// ===== Prevent default right-click menu =====
document.addEventListener("contextmenu", e => e.preventDefault());

// ===== Mouse Move = Aim =====
aimArea.addEventListener("mousemove", (event) => {
    let rect = aimArea.getBoundingClientRect();
    let x = event.clientX - rect.left;
    let y = event.clientY - rect.top;

    let pctX = (x / rect.width) * 180;
    let pctY = (y / rect.height) * 180;

    fetch(`/aim?x=${pctX}&y=${pctY}`)
      .catch(err => console.log(err));
});

// ===== Mouse Clicks =====
document.addEventListener("mousedown", (event) => {
    if (event.button === 0) {
        fetch("/fire");
    }
    else if (event.button === 2) {
        fetch("/reload");
    }
});
</script>

</body>
</html>
)rawliteral";

// ======================================================
//                    SERVER HANDLERS
// ======================================================

// Main page
void handleRoot() {
  server.send(200, "text/html", htmlPage);
}

// Aim update
void handleAim() {
  if (server.hasArg("x")) angleX = constrain(server.arg("x").toInt(), 0, 180);
  if (server.hasArg("y")) angleY = constrain(server.arg("y").toInt(), 0, 180);

  servoX.write(angleX);
  servoY.write(angleY);

  server.send(200, "text/plain", "OK");
}

// Fire relay
void handleFire() {
  digitalWrite(RELAY_PIN, HIGH);
  delay(150);
  digitalWrite(RELAY_PIN, LOW);
  server.send(200, "text/plain", "Fired");
}

// Reload action
void handleReload() {
  digitalWrite(RELAY_PIN, HIGH);
  delay(300);                // longer pulse = reload
  digitalWrite(RELAY_PIN, LOW);
  server.send(200, "text/plain", "Reloaded");
}

// ======================================================
//                      SETUP
// ======================================================
void setup() {
  Serial.begin(115200);

  // Servos
  servoX.attach(SERVO_X_PIN);
  servoY.attach(SERVO_Y_PIN);
  servoX.write(angleX);
  servoY.write(angleY);

  // Relay
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  // WiFi AP Mode
  WiFi.softAP(ssid, password);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // Server routes
  server.on("/", handleRoot);
  server.on("/aim", handleAim);
  server.on("/fire", handleFire);
  server.on("/reload", handleReload);

  server.begin();
  Serial.println("Server started");
}

// ======================================================
//                      LOOP
// ======================================================
void loop() {
  server.handleClient();
}
