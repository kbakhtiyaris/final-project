import cv2
import requests
import threading
import time
from collections import deque
from datetime import datetime

# ESP32 configuration
ESP32_IP = "10.42.0.225"  # Change to your ESP32 IP
ESP32_PORT = 80
BASE_URL = f"http://{ESP32_IP}:{ESP32_PORT}"

# Video source (0 = default webcam, or IP camera URL)
VIDEO_SOURCE = 0
# VIDEO_SOURCE = "http://192.168.1.50:8080/video"  # For IP camera

# Screen dimensions (where mouse position maps to servo angles)
SCREEN_WIDTH = 1280
SCREEN_HEIGHT = 720

# Servo angle ranges
PAN_MIN, PAN_MAX = 0, 180
TILT_MIN, TILT_MAX = 30, 150

# State management
class ControlState:
    def __init__(self):
        self.pan_angle = 90
        self.tilt_angle = 90
        self.is_firing = False
        self.reload_cooldown = 0
        self.tracked_person_id = None
        self.last_aim_time = 0
        self.frame_width = SCREEN_WIDTH
        self.frame_height = SCREEN_HEIGHT
        
state = ControlState()

def send_aim_command(pan, tilt):
    """Send pan/tilt command to ESP32"""
    try:
        payload = {"pan": int(pan), "tilt": int(tilt)}
        response = requests.post(
            f"{BASE_URL}/aim",
            json=payload,
            timeout=1
        )
        if response.status_code == 200:
            state.pan_angle = pan
            state.tilt_angle = tilt
            return True
    except Exception as e:
        print(f"Aim error: {e}")
    return False

def send_fire_command():
    """Trigger water pump"""
    try:
        response = requests.get(f"{BASE_URL}/fire", timeout=1)
        if response.status_code == 200:
            state.is_firing = True
            state.reload_cooldown = time.time() + 0.8  # 800ms reload
            return True
    except Exception as e:
        print(f"Fire error: {e}")
    return False

def mouse_callback(event, x, y, flags, param):
    global state

    if event == cv2.EVENT_MOUSEMOVE:
        # INVERTED controls:
        # X inverted: use (frame_width - x)
        # Y inverted: do not subtract from frame_height
        inv_x = state.frame_width - x
        inv_y = y

        pan = int((inv_x / state.frame_width) * (PAN_MAX - PAN_MIN) + PAN_MIN)
        tilt = int((inv_y / state.frame_height) * (TILT_MAX - TILT_MIN) + TILT_MIN)

        pan = max(PAN_MIN, min(PAN_MAX, pan))
        tilt = max(TILT_MIN, min(TILT_MAX, tilt))

        if time.time() - state.last_aim_time > 0.05:
            send_aim_command(pan, tilt)
            state.last_aim_time = time.time()
       
        # Only send if changed (throttle at 20Hz)
        if time.time() - state.last_aim_time > 0.05:
            send_aim_command(pan, tilt)
            state.last_aim_time = time.time()
    
    elif event == cv2.EVENT_LBUTTONDOWN:
        # Left click: fire or click on person to track
        if state.reload_cooldown < time.time():
            send_fire_command()
            print("🔫 FIRING!")
        else:
            print("⏳ Reloading...")
    
    elif event == cv2.EVENT_RBUTTONDOWN:
        # Right click: reload/deselect tracking
        if state.tracked_person_id is not None:
            state.tracked_person_id = None
            print("❌ Tracking cancelled")
        else:
            state.reload_cooldown = 0
            print("✅ Reloaded")

def detect_persons(frame):
    """Detect people using HOG + SVM (built-in OpenCV, no extra models needed)"""
    hog = cv2.HOGDescriptor()
    hog.setSVMDetector(cv2.HOGDescriptor_getDefaultPeopleDetector())
    
    # Resize for faster detection
    small_frame = cv2.resize(frame, (640, 480))
    scale = frame.shape[1] / small_frame.shape[1]
    
    detections, weights = hog.detectMultiScale(
        small_frame,
        winStride=(8, 8),
        padding=(8, 8),
        scale=1.05
    )
    
    # Scale back to original frame
    detections = [(int(x*scale), int(y*scale), int(w*scale), int(h*scale)) 
                  for (x, y, w, h) in detections]
    
    return detections

def get_person_center(bbox):
    """Get center point of bounding box"""
    x, y, w, h = bbox
    return (x + w//2, y + h//2)

def find_closest_person(detections, tracked_id=None):
    """Find closest person to current aim or return tracked person"""
    if not detections:
        return None
    
    center_x = state.pan_angle
    center_y = state.tilt_angle
    
    min_dist = float('inf')
    closest = None
    
    for idx, bbox in enumerate(detections):
        px, py = get_person_center(bbox)
        dist = ((px - center_x)**2 + (py - center_y)**2)**0.5
        
        if dist < min_dist:
            min_dist = dist
            closest = (idx, bbox)
    
    return closest

def main():
    global state
    
    cap = cv2.VideoCapture(VIDEO_SOURCE)
    cap.set(cv2.CAP_PROP_FRAME_WIDTH, SCREEN_WIDTH)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, SCREEN_HEIGHT)
    cap.set(cv2.CAP_PROP_FPS, 30)
    
    cv2.namedWindow("Water Gun Control")
    cv2.setMouseCallback("Water Gun Control", mouse_callback)
    
    frame_count = 0
    
    while True:
        ret, frame = cap.read()
        if not ret:
            print("Failed to read frame")
            break
        
        state.frame_width = frame.shape[1]
        state.frame_height = frame.shape[0]
        frame_count += 1
        
        # Detect people every 3 frames (faster processing)
        detections = []
        if frame_count % 3 == 0:
            detections = detect_persons(frame)
        
        # Draw detections
        for idx, (x, y, w, h) in enumerate(detections):
            color = (0, 255, 0) if state.tracked_person_id != idx else (0, 255, 255)
            cv2.rectangle(frame, (x, y), (x+w, y+h), color, 2)
            cv2.putText(frame, f"ID:{idx}", (x, y-5), cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 2)
        
        # Track selected person
        if state.tracked_person_id is not None and state.tracked_person_id < len(detections):
            bbox = detections[state.tracked_person_id]
            px, py = get_person_center(bbox)
            
            # Move turret to track person
            pan = int((px / state.frame_width) * (PAN_MAX - PAN_MIN) + PAN_MIN)
            tilt = int(((state.frame_height - py) / state.frame_height) * (TILT_MAX - TILT_MIN) + TILT_MIN)
            
            pan = max(PAN_MIN, min(PAN_MAX, pan))
            tilt = max(TILT_MIN, min(TILT_MAX, tilt))
            
            if time.time() - state.last_aim_time > 0.1:
                send_aim_command(pan, tilt)
                state.last_aim_time = time.time()
        
        # Draw crosshair at center
        cx, cy = state.frame_width // 2, state.frame_height // 2
        cv2.line(frame, (cx-20, cy), (cx+20, cy), (255, 0, 0), 2)
        cv2.line(frame, (cx, cy-20), (cx, cy+20), (255, 0, 0), 2)
        cv2.circle(frame, (cx, cy), 5, (255, 0, 0), -1)
        
        # Draw status overlay
        status_text = []
        status_text.append(f"Pan: {state.pan_angle}° | Tilt: {state.tilt_angle}°")
        status_text.append(f"People detected: {len(detections)}")
        
        if state.tracked_person_id is not None:
            status_text.append(f"🎯 Tracking ID: {state.tracked_person_id}")
        else:
            status_text.append("Aim: Mouse | Fire: L-Click | Reload: R-Click")
        
        if state.reload_cooldown > time.time():
            status_text.append(f"⏳ Reload: {state.reload_cooldown - time.time():.1f}s")
        else:
            status_text.append("✅ Ready to fire")
        
        # Draw status text
        for i, text in enumerate(status_text):
            cv2.putText(frame, text, (10, 30 + i*25), 
                       cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)
        
        # Display frame
        cv2.imshow("Water Gun Control", frame)
        
        # Key controls
        key = cv2.waitKey(1) & 0xFF
        if key == ord('q'):
            break
        elif key == ord('c'):
            # Reset tracking
            state.tracked_person_id = None
            print("Tracking cancelled")
    
    cap.release()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    print("🔫 Water Gun Control System")
    print("=" * 50)
    print(f"ESP32 URL: {BASE_URL}")
    print("\n📌 Controls:")
    print("  • Move mouse → Pan/Tilt")
    print("  • Left-click → Fire")
    print("  • Right-click → Reload / Deselect tracking")
    print("  • Click on person → Track that person")
    print("  • 'Q' key → Quit")
    print("  • 'C' key → Cancel tracking")
    print("=" * 50)
    
    try:
        main()
    except KeyboardInterrupt:
        print("\n👋 Shutting down...")
