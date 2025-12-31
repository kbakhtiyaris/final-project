
import cv2
import numpy as np
import requests
import time
import threading
from collections import deque

# ------------------------- ESP32 configuration -------------------------
ESP32_IP = "10.42.0.225"   # <-- change to your ESP32 IP
ESP32_PORT = 80
BASE_URL = f"http://{ESP32_IP}:{ESP32_PORT}"

# --------------------------- Video source ------------------------------
VIDEO_SOURCE = 0           # or an IP cam URL

# ------------------------- Servo angle ranges --------------------------
PAN_MIN, PAN_MAX = 0, 180
TILT_MIN, TILT_MAX = 30, 150

# ------------------------- Control parameters --------------------------
SEND_PERIOD = 0.08         # seconds between aim posts (~12–14 Hz)
SMOOTH_ALPHA = 0.10        # 0..1: bigger = snappier, smaller = smoother
MIN_AREA = 800             # minimum blob area to consider (px)
AUTO_FIRE = False          # set True to auto-fire on big area
AUTO_FIRE_AREA = 15000     # area threshold for auto-fire
RELOAD_SECONDS = 0.8       # cooldown after fire

# -------------------------- Global state -------------------------------
class State:
    def __init__(self):
        self.pan = 90
        self.tilt = 90
        self.last_sent = 0.0
        self.reload_until = 0.0
        self.frame_w = 1280
        self.frame_h = 720
        self.target_area = 0
        self.have_target = False

state = State()

# --------------------------- Async HTTP queue --------------------------
# Offload ESP32 requests so video loop never waits on network
class CommandSender:
    def __init__(self, base_url):
        self.base_url = base_url
        self.session = requests.Session()
        self.q = deque(maxlen=100)
        self.stop = False
        self.thread = threading.Thread(target=self.run, daemon=True)
        self.thread.start()

    def run(self):
        while not self.stop:
            if self.q:
                item = self.q.popleft()
                typ, payload = item
                try:
                    if typ == "aim":
                        self.session.post(f"{self.base_url}/aim", json=payload, timeout=0.4)
                    elif typ == "fire":
                        self.session.get(f"{self.base_url}/fire", timeout=0.4)
                except Exception:
                    # swallow; keep loop realtime
                    pass
            else:
                time.sleep(0.002)

    def send_aim(self, pan, tilt):
        self.q.append(("aim", {"pan": int(pan), "tilt": int(tilt)}))

    def send_fire(self):
        self.q.append(("fire", None))

sender = CommandSender(BASE_URL)

# --------------------------- HSV tuner UI -------------------------------
def create_hsv_tuner():
    cv2.namedWindow("HSV Tuner", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("HSV Tuner", 420, 300)
    # Starting point for yellow; tune as needed
    cv2.createTrackbar("H low",  "HSV Tuner", 20, 179, lambda v: None)
    cv2.createTrackbar("H high", "HSV Tuner", 35, 179, lambda v: None)
    cv2.createTrackbar("S low",  "HSV Tuner", 100, 255, lambda v: None)
    cv2.createTrackbar("S high", "HSV Tuner", 255, 255, lambda v: None)
    cv2.createTrackbar("V low",  "HSV Tuner", 100, 255, lambda v: None)
    cv2.createTrackbar("V high", "HSV Tuner", 255, 255, lambda v: None)

def get_hsv_range():
    hL = cv2.getTrackbarPos("H low",  "HSV Tuner")
    hH = cv2.getTrackbarPos("H high", "HSV Tuner")
    sL = cv2.getTrackbarPos("S low",  "HSV Tuner")
    sH = cv2.getTrackbarPos("S high", "HSV Tuner")
    vL = cv2.getTrackbarPos("V low",  "HSV Tuner")
    vH = cv2.getTrackbarPos("V high", "HSV Tuner")
    lower = np.array([hL, sL, vL], dtype=np.uint8)
    upper = np.array([hH, sH, vH], dtype=np.uint8)
    return lower, upper

# --------------------- Yellow detection & centroid ---------------------
def detect_yellow(frame, lower, upper, min_area=800):
    """
    Returns (center, bbox, area, mask)
    center: (cx,cy) or None
    bbox: (x,y,w,h) or None
    area: int
    mask: binary mask (uint8)
    """
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV)
    mask = cv2.inRange(hsv, lower, upper)

    # Keep kernels lightweight to avoid lag
    mask = cv2.GaussianBlur(mask, (5, 5), 0)
    kernel = np.ones((3, 3), np.uint8)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel, iterations=1)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel, iterations=1)

    cnts, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not cnts:
        return None, None, 0, mask

    best = max(cnts, key=cv2.contourArea)
    area = int(cv2.contourArea(best))
    if area < min_area:
        return None, None, 0, mask

    x, y, w, h = cv2.boundingRect(best)
    M = cv2.moments(best)
    if M["m00"] == 0:
        cx, cy = x + w // 2, y + h // 2
    else:
        cx = int(M["m10"] / M["m00"])
        cy = int(M["m01"] / M["m00"])

    return (cx, cy), (x, y, w, h), area, mask

# --------------------------- Mapping & smoothing -----------------------
def pixel_to_servo(cx, cy, w, h):
    """
    Map centroid (pixels) -> servo angles.
    X is inverted, Y is not inverted.
    """
    inv_x = w - cx
    pan  = (inv_x / w) * (PAN_MAX - PAN_MIN) + PAN_MIN
    tilt = (cy    / h) * (TILT_MAX - TILT_MIN) + TILT_MIN
    pan  = max(PAN_MIN,  min(PAN_MAX,  pan))
    tilt = max(TILT_MIN, min(TILT_MAX, tilt))
    return pan, tilt

def smooth_step(current, target, alpha):
    return current + alpha * (target - current)

# --------------------------- Threaded capture --------------------------
class VideoStream:
    """
    Grabs frames in a dedicated thread and always provides the freshest frame.
    """
    def __init__(self, src=0, width=1280, height=720, fps=30):
        self.cap = cv2.VideoCapture(src)
        # Hints to camera/driver: small buffer, target fps, MJPG if available
        self.cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)     # may be ignored by some backends
        self.cap.set(cv2.CAP_PROP_FPS, fps)
        self.cap.set(cv2.CAP_PROP_FRAME_WIDTH, width)
        self.cap.set(cv2.CAP_PROP_FRAME_HEIGHT, height)
        try:
            fourcc = cv2.VideoWriter_fourcc(*'MJPG')  # often lower latency
            self.cap.set(cv2.CAP_PROP_FOURCC, fourcc)
        except Exception:
            pass

        self.lock = threading.Lock()
        self.frame = None
        self.stopped = False
        self.thread = threading.Thread(target=self.update, daemon=True)
        self.thread.start()

    def update(self):
        while not self.stopped:
            ok, f = self.cap.read()
            if not ok:
                # brief sleep to avoid tight loop if camera fails
                time.sleep(0.005)
                continue
            with self.lock:
                self.frame = f

    def read(self):
        with self.lock:
            return self.frame

    def release(self):
        self.stopped = True
        time.sleep(0.02)
        try:
            self.cap.release()
        except Exception:
            pass

# -------------------------------- Main ---------------------------------
def main():
    global AUTO_FIRE

    stream = VideoStream(VIDEO_SOURCE, width=1280, height=720, fps=30)

    create_hsv_tuner()

    cv2.namedWindow("Water Gun Control", cv2.WINDOW_NORMAL)
    cv2.resizeWindow("Water Gun Control", 960, 540)

    # Show mask optionally; press 'm' to toggle
    show_mask = True

    print("🟡 Yellow tracking mode (mouse control removed).")
    print("Keys:  Q = quit,  F = fire once,  A = toggle auto-fire,  M = toggle mask")

    while True:
        frame = stream.read()
        if frame is None:
            # camera not ready yet
            time.sleep(0.002)
            continue

        state.frame_w = frame.shape[1]
        state.frame_h = frame.shape[0]

        # 1) Detect yellow
        lower, upper = get_hsv_range()
        center, bbox, area, mask = detect_yellow(frame, lower, upper, MIN_AREA)
        state.target_area = area
        state.have_target = center is not None

        # 2) Visual feedback
        if bbox is not None:
            x, y, w, h = bbox
            cv2.rectangle(frame, (x, y), (x + w, y + h), (0, 255, 255), 2)
            cv2.circle(frame, center, 5, (0, 255, 255), -1)
            cv2.putText(frame, f"area:{area}", (x, y - 8),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 255), 2)
        else:
            cv2.putText(frame, "No yellow target", (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 2)

        # 3) Convert centroid -> servo target, smooth, and send @ ~14 Hz
        now = time.time()
        if center is not None:
            tgt_pan, tgt_tilt = pixel_to_servo(center[0], center[1], state.frame_w, state.frame_h)
            state.pan  = smooth_step(state.pan,  tgt_pan,  SMOOTH_ALPHA)
            state.tilt = smooth_step(state.tilt, tgt_tilt, SMOOTH_ALPHA)

            if (now - state.last_sent) >= SEND_PERIOD:
                sender.send_aim(state.pan, state.tilt)  # non-blocking
                state.last_sent = now

            # Optional auto fire on large blob
            if AUTO_FIRE and (area > AUTO_FIRE_AREA) and (now > state.reload_until):
                sender.send_fire()  # non-blocking
                state.reload_until = now + RELOAD_SECONDS

        # 4) Overlay HUD
        hud = [
            f"Pan:{int(state.pan)}  Tilt:{int(state.tilt)}",
            f"Area:{state.target_area}  AutoFire:{'ON' if AUTO_FIRE else 'OFF'}",
            f"Resolution:{state.frame_w}x{state.frame_h}"
        ]
        for i, t in enumerate(hud):
            cv2.putText(frame, t, (10, 60 + i * 24),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.65, (0, 255, 0), 2)

        # 5) Show frames
        cv2.imshow("Water Gun Control", frame)
        if show_mask:
            cv2.imshow("Yellow Mask", mask)

        # 6) Keys
        k = cv2.waitKey(1) & 0xFF
        if k == ord('q'):
            break
        elif k == ord('f'):
            if time.time() > state.reload_until:
                sender.send_fire()
                state.reload_until = time.time() + RELOAD_SECONDS
        elif k == ord('a'):
            AUTO_FIRE = not AUTO_FIRE
            print(f"Auto-fire: {'ON' if AUTO_FIRE else 'OFF'}")
        elif k == ord('m'):
            show_mask = not show_mask

    stream.release()
    cv2.destroyAllWindows()

# ---------------------------- Entrypoint -------------------------------
if __name__ == "__main__":
    try:
        # Small OpenCV optimization flag
        cv2.setUseOptimized(True)
    except Exception:
        pass
    try:
        main()
    except KeyboardInterrupt:
        print("\n👋 Shutting down...")
