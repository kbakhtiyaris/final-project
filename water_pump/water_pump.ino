
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ESP32Servo.h>

// WiFi Credentials - replace with your network infoTilt

const char* ssid = "Redmi11";
const char* password = "123456789";



//const char* ssid = "FiberHGW_TPFAC0_2.4GHz";
//const char* password = "dNWwNHKD";

Servo panServo;
Servo tiltServo;

int panPin = 13;     // your pan servo pin
int tiltPin = 12;    // your tilt servo pin
int firePin = 26;    // relay or MOSFET input pin

AsyncWebServer server(80);

// ---- HTML PAGE ----
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<title>Water Gun Turret</title>
<style>
  body { font-family: Arial; background:#1e1e1e; color:#f5f5f5; text-align:center; padding:30px; }
  h1 { color:#5dc8e1; }
  .slider-label { margin-top:20px; font-size:18px; }
  input[type=range] { width:300px; }
  button {
    margin-top:25px; padding:16px 35px; font-size:20px;
    background:#e15f5f; color:white; border:none; border-radius:10px; cursor:pointer;
  }
  #aimArea {
    margin:35px auto; width:300px; height:300px;
    background:#333; border:2px solid #777; position:relative;
  }
  #crosshair {
    width:20px; height:20px; border:2px solid red; border-radius:50%;
    position:absolute; pointer-events:none; transform:translate(-50%,-50%);
  }
</style>
</head>
<body>

<h1>Water Gun Turret Control</h1>

<div id="aimArea">
  <div id="crosshair"></div>
</div>

<div class="slider-label">Tilt Angle</div>
<input type="range" min="0" max="180" id="tiltSlider">

<div class="slider-label">Pan Angle</div>
<input type="range" min="0" max="180" id="panSlider">

<button onclick="fire()">FIRE</button>

<script>
let aimBox = document.getElementById("aimArea");
let cross = document.getElementById("crosshair");
let tiltSlider = document.getElementById("tiltSlider");
let panSlider = document.getElementById("panSlider");

function send(cmd){
  fetch(`/set?${cmd}`).catch(()=>{});
}

tiltSlider.oninput = ()=> send("tilt=" + tiltSlider.value);
panSlider.oninput = ()=> send("pan=" + panSlider.value);

function fire(){
  fetch("/fire").catch(()=>{});
}

aimBox.addEventListener("mousemove", e=>{
  let rect = aimBox.getBoundingClientRect();
  let x = e.clientX - rect.left;
  let y = e.clientY - rect.top;

  cross.style.left = x + "px";
  cross.style.top = y + "px";

  let pan = Math.floor((x / rect.width) * 180);
  let tilt = Math.floor((y / rect.height) * 180);

  send(`pan=${pan}`);
  send(`tilt=${tilt}`);

  panSlider.value = pan;
  tiltSlider.value = tilt;
});
</script>

</body>
</html>
)rawliteral";
// ----------------------

void setup() {
  Serial.begin(115200);

  // Servo setup
  panServo.attach(panPin);
  tiltServo.attach(tiltPin);
  panServo.write(90);
  tiltServo.write(90);

  // Fire pin
  pinMode(firePin, OUTPUT);
  digitalWrite(firePin, LOW);

  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
  Serial.println(WiFi.localIP());

  // Routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send_P(200, "text/html", index_html);
  });

  server.on("/set", HTTP_GET, [](AsyncWebServerRequest *req){
    if (req->hasParam("pan")) {
      int p = req->getParam("pan")->value().toInt();
      panServo.write(p);
    }
    if (req->hasParam("tilt")) {
      int t = req->getParam("tilt")->value().toInt();
      tiltServo.write(t);
    }
    req->send(200, "text/plain", "OK");
  });

  server.on("/fire", HTTP_GET, [](AsyncWebServerRequest *req){
    digitalWrite(firePin, HIGH);
    delay(300); // pump burst time
    digitalWrite(firePin, LOW);
    req->send(200, "text/plain", "FIRE");
  });

  server.begin();
}

void loop() {
  .handleClient();
}
// Extract integer value from simple JSON string 
int extractJsonValue(const String& json, const char* key) 
{ String searchStr = String("\"") + key + "\":"; 
int keyIndex = json.indexOf(searchStr); 
if (keyIndex == -1) return -1;
keyIndex += searchStr.length();
int endIndex = json.indexOf(',', keyIndex);
if (endIndex == -1) endIndex = json.indexOf('}', keyIndex);
if (endIndex == -1) return -1; String valStr = json.substring(keyIndex, endIndex);
valStr.trim(); 
return valStr.toInt(); 
}
