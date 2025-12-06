#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

// ---------------- WIFI ----------------
//const char* ssid = "FiberHGW_TPFAC0_2.4GHz";
//const char* password = "dNWwNHKD";
const char* ssid = "Redmi11";
const char* password = "123456789";

// ---------------- PINS ----------------
const int SERVO_PAN_PIN  = 13;
const int SERVO_TILT_PIN = 12;
const int RELAY_PIN = 26;

// ---------------- SERVO STATE ----------------
Servo servoPan;
Servo servoTilt;

int panAngle  = 90;
int tiltAngle = 90;

const int MIN_PAN  = 0;
const int MAX_PAN  = 180;
const int MIN_TILT = 30;
const int MAX_TILT = 150;

const int FIRE_DURATION = 500;

// ---------------- WEB SERVER ----------------
WebServer server(80);

// ---------------- HTML PAGE ----------------
const char* webpage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>Water Gun Turret</title>
<style>
  body { font-family: Arial; background:#222; color:#eee; text-align:center; padding:20px; }
  h1 { color:#5dc8e1; }
  input[type=range] { width: 300px; }
  button { padding:15px 30px; margin:10px; font-size:20px; background:#e15f5f; border:none;
           color:white; border-radius:10px; cursor:pointer; }
  button:hover { background:#c04040; }
</style>
<script>

let pan = 90;
let tilt = 90;

// ---- WASD movement ----
let moveL=false, moveR=false, moveU=false, moveD=false;
const SPEED = 1;
const MOUSE_S = 0.1;

// ---- Send aim to ESP32 ----
function sendAim() {
  fetch('/aim', {
    method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({pan:Math.round(pan), tilt:Math.round(tilt)})
  });
}

// ---- Keyboard ----
document.addEventListener("keydown", e=>{
  switch(e.key.toLowerCase()){
    case 'd': moveL=true; break;
    case 'a': moveR=true; break;
    case 'w': moveU=true; break;
    case 's': moveD=true; break;
    case 'f': fetch('/fire'); break;
    case 'r': resetPos(); break;
  }
});
document.addEventListener("keyup", e=>{
  switch(e.key.toLowerCase()){
    case 'd': moveL=false; break;
    case 'a': moveR=false; break;
    case 'w': moveU=false; break;
    case 's': moveD=false; break;
  }
});

// ---- Mouse Lock ----
document.addEventListener("click", ()=>{
  document.body.requestPointerLock();
});

document.addEventListener("mousemove", e=>{
  if (document.pointerLockElement !== document.body) return;

  pan  -= e.movementX * MOUSE_S;
  tilt += e.movementY * MOUSE_S;

  pan = Math.min(180, Math.max(0, pan));
  tilt = Math.min(150, Math.max(30, tilt));

  sendAim();
});

// ---- Movement Loop ----
function loop(){
  let changed=false;

  if(moveL){ pan -= SPEED; changed=true; }
  if(moveR){ pan += SPEED; changed=true; }
  if(moveU){ tilt -= SPEED; changed=true; }
  if(moveD){ tilt += SPEED; changed=true; }

  pan = Math.min(180, Math.max(0, pan));
  tilt = Math.min(150, Math.max(30, tilt));

  if(changed) sendAim();
  requestAnimationFrame(loop);
}

document.addEventListener("mousedown", (event) => {
    if (event.button === 0) {
        // Left click = FIRE
        fetch("/fire")
            .catch(err => console.log("Fire error:", err));
    }
    else if (event.button === 2) {
        // Right click = RELOAD
        fetch("/reload")
            .catch(err => console.log("Reload error:", err));
    }
});

// Prevent default right-click menu
document.addEventListener("contextmenu", event => event.preventDefault());


loop();

// ---- Sliders ----
function sliderAim(){
  pan  = parseInt(document.getElementById("pan").value);
  tilt = parseInt(document.getElementById("tilt").value);
  sendAim();
}

// ---- Reset ----
function resetPos(){
  pan = 90;
  tilt = 90;
  sendAim();
  document.getElementById("pan").value = 90;
  document.getElementById("tilt").value = 90;
}

</script>
</head>
<body>

<h1>Water Gun Turret Control</h1>

<h3>Pan</h3>
<input id="pan" type="range" min="0" max="180" value="90" oninput="sliderAim()">

<h3>Tilt</h3>
<input id="tilt" type="range" min="30" max="150" value="90" oninput="sliderAim()">

<br><br>
<button onclick="fetch('/fire')">FIRE!</button>
<button onclick="resetPos()">RESET</button>

</body>
</html>
)rawliteral";

// ----------- JSON PARSER ------------
int extractJsonValue(const String& json, const char* key) {
  String searchStr = String("\"") + key + "\":";
  int keyIndex = json.indexOf(searchStr);
  if (keyIndex == -1) return -1;
  keyIndex += searchStr.length();
  int endIndex = json.indexOf(',', keyIndex);
  if (endIndex == -1) endIndex = json.indexOf('}', keyIndex);
  String valStr = json.substring(keyIndex, endIndex);
  valStr.trim();
  return valStr.toInt();
}

// ------------ SETUP ------------
void setup() {
  Serial.begin(115200);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  servoPan.attach(SERVO_PAN_PIN);
  servoTilt.attach(SERVO_TILT_PIN);
  servoPan.write(panAngle);
  servoTilt.write(tiltAngle);

  // WiFi connect
  Serial.printf("Connecting to %s\n", ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(400);
  }

  Serial.println("\nConnected!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // ----------- ROUTES -----------
  server.on("/", HTTP_GET, [](){
    server.send(200, "text/html", webpage);
  });

  server.on("/aim", HTTP_POST, [](){
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"status\":\"bad\"}");
      return;
    }
    String body = server.arg("plain");
    int p = extractJsonValue(body, "pan");
    int t = extractJsonValue(body, "tilt");

    if (p < 0 || t < 0) {
      server.send(400, "application/json", "{\"status\":\"err\"}");
      return;
    }

    panAngle  = constrain(p, MIN_PAN,  MAX_PAN);
    tiltAngle = constrain(t, MIN_TILT, MAX_TILT);

    servoPan.write(panAngle);
    servoTilt.write(tiltAngle);
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.on("/fire", HTTP_GET, [](){
    digitalWrite(RELAY_PIN, LOW);
    delay(FIRE_DURATION);
    digitalWrite(RELAY_PIN, HIGH);
    server.send(200, "application/json", "{\"status\":\"fired\"}");
  });

  server.on("/fire", HTTP_GET, []() {
    // FIRE ACTION
    digitalWrite(RELAY_PIN, HIGH);
    delay(150);
    digitalWrite(RELAY_PIN, LOW);
    server.send(200, "text/plain", "Fired");
});

server.on("/reload", HTTP_GET, []() {
    // RELOAD ACTION
    // put your reload logic here
    server.send(200, "text/plain", "Reloaded");
});


  server.begin();
  Serial.println("SERVER READY");
}

// ------------ LOOP ------------
void loop() {
  server.handleClient();
}
