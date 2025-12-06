#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

// WiFi Credentials - replace with your network infoTilt
//const char* ssid = "FiberHGW_TPFAC0_2.4GHz";
//const char* password = "dNWwNHKD";
const char* ssid = "Redmi11";
const char* password = "123456789";


// Pin configuration
const int SERVO_PAN_PIN = 13;
const int SERVO_TILT_PIN = 12;
const int RELAY_PIN = 26;

// Servo setup
Servo servoPan;
Servo servoTilt;

// Current servo angles
int panAngle = 90;
int tiltAngle = 90;

// Servo bounds
const int MIN_PAN = 0;
const int MAX_PAN = 180;
const int MIN_TILT = 30;
const int MAX_TILT = 150;

// Fire duration (ms)
const int FIRE_DURATION = 500;

WebServer server(80);

// HTML + JS dashboard served by ESP32
const char* webpage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>Water Gun Turret Control</title>
<style>
  body { font-family: Arial, sans-serif; background: #222; color: #eee; text-align: center; padding: 20px; }
  h1 { color: #5dc8e1; }
  input[type=range] { width: 300px; }
  button { font-size: 20px; padding: 15px 30px; background: #e15f5f; color: white; border: none; cursor: pointer; border-radius: 8px; margin-top: 20px; }
  button:hover { background: #d54b4b; }
  .slider-label { font-size: 18px; margin-top: 20px; }
  #status { margin-top: 15px; }
</style>
<script>
let pan = 90;
let tilt = 90;

// WASD movement states
let moveLeft = false;
let moveRight = false;
let moveUp = false;
let moveDown = false;

// Settings
const WASD_SPEED = 1;     // degrees per frame
const MOUSE_SENS   = 0.1;  // lower = smoother, higher = faster

let mouseControlEnabled = true;

// Send updated angles to ESP32
function sendAim() {
  fetch('/aim', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ pan: Math.round(pan), tilt: Math.round(tilt) })
  });
}

function firePump() { fetch('/fire'); }
function resetPosition() {
  pan = 90;
  tilt = 90;
  sendAim();
}

// -------- KEYBOARD CONTROLS (WASD + F + R) -------- //
document.addEventListener("keydown", function(e) {
  switch (e.key.toLowerCase()) {
    case "d": moveLeft  = true; break;
    case "a": moveRight = true; break;
    case "w": moveUp    = true; break;
    case "s": moveDown  = true; break;
    case "f": firePump(); break;
    case "r": resetPosition(); break;
  }
});

document.addEventListener("keyup", function(e) {
  switch (e.key.toLowerCase()) {
    case "d": moveLeft  = false; break;
    case "a": moveRight = false; break;
    case "w": moveUp    = false; break;
    case "s": moveDown  = false; break;
  }
});

// -------- MOUSE AIM CONTROL -------- //
// Click anywhere to activate pointer lock
document.addEventListener("click", function() {
  document.body.requestPointerLock();
});

// Mouse movement event
document.addEventListener("mousemove", function(e) {
  if (document.pointerLockElement !== document.body) return;

  // Move pan/tilt by mouse movement
  pan  -= e.movementX * MOUSE_SENS;
  tilt += e.movementY * MOUSE_SENS;

  // Clamp limits
  pan = Math.min(180, Math.max(0, pan));
  tilt = Math.min(150, Math.max(30, tilt));

  sendAim();
});

// -------- MAIN MOVEMENT LOOP (60 FPS) -------- //
function movementLoop() {
  let changed = false;

  if (moveLeft)  { pan -= WASD_SPEED; changed = true; }
  if (moveRight) { pan += WASD_SPEED; changed = true; }
  if (moveUp)    { tilt -= WASD_SPEED; changed = true; }
  if (moveDown)  { tilt += WASD_SPEED; changed = true; }

  // Clamp
  pan = Math.min(180, Math.max(0, pan));
  tilt = Math.min(150, Math.max(30, tilt));

  if (changed) sendAim();

  requestAnimationFrame(movementLoop);
}

movementLoop();
</script>

</head>
<body>
  <h1>Gun Control panel.</h1>
  <h2>About the project.</h2>
  <h3>This is a water gun turret, it can go from 0 to 180 degrees on the pan axis and 30 to 150 degrees in the tilt axis.\n u can use mouse to control the turret or control it using awsd keys. use f to fire and r to reset.</h3>
  
  <br>
  <button onclick="firePump()">FIRE!</button><br>
  <button onclick="resetPosition()">Reset To Center</button>
  
  <div id="status" style="margin-top:15px;">Status: <span id="statusText">Initializing...</span></div>
</body>
</html>
)rawliteral";

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Setup pins
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  servoPan.attach(SERVO_PAN_PIN);
  servoTilt.attach(SERVO_TILT_PIN);
  servoPan.write(panAngle);
  servoTilt.write(tiltAngle);

  // Connect wifi
  Serial.printf("Connecting to %s\n", ssid);
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed!");
  }

  // Setup endpoints
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", webpage);
  });

  int extractJsonValue(const String& json, const char* key);

  server.on("/aim", HTTP_POST, []() {
    if (server.hasArg("plain")) {
      String body = server.arg("plain");
      int panVal = extractJsonValue(body, "pan");
      int tiltVal = extractJsonValue(body, "tilt");
      if (panVal >= 0 && tiltVal >= 0) {
        panAngle = constrain(panVal, MIN_PAN, MAX_PAN);
        tiltAngle = constrain(tiltVal, MIN_TILT, MAX_TILT);
        servoPan.write(panAngle);
        servoTilt.write(tiltAngle);
        server.send(200, "application/json", "{\"status\":\"ok\"}");
        Serial.printf("Aim updated: Pan=%d, Tilt=%d\n", panAngle, tiltAngle);
        return;
      }
    }
    server.send(400, "application/json", "{\"status\":\"error\"}");
  });

  server.on("/fire", HTTP_GET, []() {
    digitalWrite(RELAY_PIN, LOW);
    delay(FIRE_DURATION);
    digitalWrite(RELAY_PIN, HIGH);
    Serial.println("Fired pump.");
    server.send(200, "application/json", "{\"status\":\"fired\"}");
  });

  server.on("/reset", HTTP_GET, []() {
    panAngle = 90;
    tiltAngle = 90;
    servoPan.write(panAngle);
    servoTilt.write(tiltAngle);
    Serial.println("Reset servos.");
    server.send(200, "application/json", "{\"status\":\"reset\"}");
  });

  server.on("/status", HTTP_GET, []() {
    String res = "{\"pan\":" + String(panAngle) + ",\"tilt\":" + String(tiltAngle) + ",\"battery\":\"Good\"}";
    server.send(200, "application/json", res);
  });

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}

// Extract integer value from simple JSON string
int extractJsonValue(const String& json, const char* key) {
  String searchStr = String("\"") + key + "\":";
  int keyIndex = json.indexOf(searchStr);
  if (keyIndex == -1) return -1;
  keyIndex += searchStr.length();
  int endIndex = json.indexOf(',', keyIndex);
  if (endIndex == -1) endIndex = json.indexOf('}', keyIndex);
  if (endIndex == -1) return -1;
  String valStr = json.substring(keyIndex, endIndex);
  valStr.trim();
  return valStr.toInt();
}
