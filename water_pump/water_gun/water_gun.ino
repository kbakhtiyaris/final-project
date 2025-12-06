#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

// ========= WIFI ==========
const char* ssid     = "Redmi11";
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
<title>ESP32 Auto-Aim Water Cannon</title>
<style>
body {
  background:#0a0a0a;
  color:#eee;
  font-family:Arial;
  margin:0;
  padding:0;
}

header {
  background:#111;
  padding:15px;
  text-align:center;
  font-size:26px;
  color:#00eaff;
  border-bottom:2px solid #044;
}

#infoBox {
  padding:15px;
  margin:10px;
  background:#111;
  border:1px solid #044;
  border-radius:10px;
  color:#bbb;
  font-size:15px;
}

#aimArea {
  width:100vw;
  height:75vh;
  border-top:2px solid #333;
  border-bottom:2px solid #333;
  background:#1b1b1b;
  position:relative;
  cursor:crosshair;
  overflow:hidden;
}

/* Crosshair */
.crosshair {
  position:absolute;
  top:50%;
  left:50%;
  width:40px;
  height:40px;
  transform:translate(-50%, -50%);
  pointer-events:none;
}

.crosshair:before, .crosshair:after {
  content:"";
  position:absolute;
  background:#00eaff;
}

.crosshair:before {
  width:40px;
  height:2px;
  top:50%;
  left:0;
}

.crosshair:after {
  width:2px;
  height:40px;
  top:0;
  left:50%;
}

footer {
  padding:10px;
  text-align:center;
  color:#666;
}
</style>
</head>

<body>

<header>ESP32 Auto-Aim Water Cannon</header>

<div id="infoBox">
  <b>About the Project:</b><br>
  This system uses an ESP32 running in WiFi Access Point mode.  
  You can aim the water cannon by simply moving your mouse over the screen.  
  <br><br>
  <b>Controls:</b><br>
  ➤ Move Mouse = Aim servos<br>
  ➤ Left Click = Fire water pump<br>
  ➤ Right Click = Reload (custom action)<br>
  ➤ Live crosshair shows exactly where the nozzle is pointing<br>
</div>

<div id="aimArea">
  <div class="crosshair"></div>
</div>

<footer>Designed by Khud Bakhtiyar</footer>

<script>
const aimArea = document.getElementById("aimArea");

// ===== Prevent right-click menu =====
document.addEventListener("contextmenu", e => e.preventDefault());

// ===== Mouse Move = AIM =====
aimArea.addEventListener("mousemove", (event) => {
    let rect = aimArea.getBoundingClientRect();
    let x = event.clientX - rect.left;
    let y = event.clientY - rect.top;

    let pctX = (x / rect.width) * 180;
    let pctY = (y / rect.height) * 180;

    fetch(`/aim?x=${pctX}&y=${pctY}`).catch(err => {});
});

// ===== Mouse Clicks =====
document.addEventListener("mousedown", (event) => {
    if (event.button === 0) {
        fetch("/fire");
    } else if (event.button === 2) {
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
  digitalWrite(RELAY_PIN, LOW);
  delay(150);
  digitalWrite(RELAY_PIN, HIGH);
  server.send(200, "text/plain", "Fired");
}

// Reload action
void handleReload() {
  digitalWrite(RELAY_PIN, LOW);
  delay(300);                // longer pulse = reload
  digitalWrite(RELAY_PIN, HIGH);
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
  digitalWrite(RELAY_PIN, HIGH);

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
