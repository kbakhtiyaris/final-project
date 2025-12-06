import cv2
import requests
import threading
import time
import numpy as np

# Configuration - Replace with your values
ESP32_IP = "10.89.109.196"  # Your ESP32 IP address
ESP32_PORT = 80
IP_CAM_URL = "http://10.89.109.190:8080/video"  # Your IP cam stream URL

# Frame and servo calibration settings
FRAME_WIDTH = 640
FRAME_HEIGHT = 480
PAN_SCALE = 0.1   # Convert pixel X offset to servo angle degrees
TILT_SCALE = 0.1  # Convert pixel Y offset to servo angle degrees

class WaterGunController:
    def __init__(self, esp32_ip):
        self.esp32_url = f"http://{esp32_ip}"
        self.current_pan = 90
        self.current_tilt = 90
        self.fire_lock = threading.Lock()

    def aim(self, pan, tilt):
        """Send aiming command to ESP32."""
        try:
            data = {"pan": int(pan), "tilt": int(tilt)}
            response = requests.post(
                f"{self.esp32_url}/aim",
                json=data,
                timeout=1
            )
            self.current_pan = pan
            self.current_tilt = tilt
            print(f"[AIM] Pan: {pan:.0f}° | Tilt: {tilt:.0f}°")
        except Exception as e:
            print(f"[ERROR] Aim failed: {e}")

    def fire(self):
        """Trigger water pump."""
        if self.fire_lock.acquire(blocking=False):
            try:
                response = requests.get(f"{self.esp32_url}/fire", timeout=1)
                print("[FIRE] Water pump activated!")
            except Exception as e:
                print(f"[ERROR] Fire failed: {e}")
            finally:
                self.fire_lock.release()

    def reset(self):
        """Reset servos to center."""
        try:
            response = requests.get(f"{self.esp32_url}/reset", timeout=1)
            self.current_pan = 90
            self.current_tilt = 90
            print("[RESET] Returned to center position")
        except Exception as e:
            print(f"[ERROR] Reset failed: {e}")

def mouse_callback(event, x, y, flags, param):
    """Handle mouse click on video frame."""
    if event == cv2.EVENT_LBUTTONDOWN:
        controller = param

        center_x = FRAME_WIDTH // 2
        center_y = FRAME_HEIGHT // 2

        offset_x = x - center_x
        offset_y = y - center_y

        # Convert pixel offsets to servo angles
        new_pan = 90 - (offset_x * PAN_SCALE)
        new_tilt = 90 + (offset_y * TILT_SCALE)  # Invert Y axis


        # Clamp to servo limits
        new_pan = max(30, min(180, new_pan))
        new_tilt = max(60, min(180, new_tilt))

        controller.aim(new_pan, new_tilt)
        print(f"[CLICK] X:{x} Y:{y} → Pan:{new_pan:.0f}° Tilt:{new_tilt:.0f}°")

def main():
    controller = WaterGunController(ESP32_IP)
    print(f"[INIT] Connecting to ESP32 at {ESP32_IP}...")
    time.sleep(1)

    controller.reset()
    time.sleep(0.5)

    print(f"[STREAM] Opening IP camera stream at {IP_CAM_URL}...")
    cap = cv2.VideoCapture(IP_CAM_URL)
    if not cap.isOpened():
        print("[ERROR] Cannot open video stream")
        return

    cv2.namedWindow("Water Gun Targeting System")
    cv2.setMouseCallback("Water Gun Targeting System", mouse_callback, controller)

    print("\n" + "="*50)
    print("WATER GUN TARGETING SYSTEM")
    print("="*50)
    print("Controls:")
    print("  • LEFT CLICK: Aim at target location and fire")
    print("  • SPACEBAR: Reset to center")
    print("  • F: Fire water pump")
    print("  • Q: Quit")
    print("="*50 + "\n")

    while True:
        ret, frame = cap.read()
        if not ret:
            print("[ERROR] Failed to read frame")
            break

        frame = cv2.resize(frame, (FRAME_WIDTH, FRAME_HEIGHT))

        # Draw crosshair at center
        cx, cy = FRAME_WIDTH // 2, FRAME_HEIGHT // 2
        cv2.circle(frame, (cx, cy), 20, (0, 255, 0), 2)
        cv2.line(frame, (cx-30, cy), (cx+30, cy), (0, 255, 0), 2)
        cv2.line(frame, (cx, cy-30), (cx, cy+30), (0, 255, 0), 2)

        # Draw grid lines every 50 pixels
        for i in range(0, FRAME_WIDTH, 50):
            cv2.line(frame, (i, 0), (i, FRAME_HEIGHT), (100,100,100), 1)
        for i in range(0, FRAME_HEIGHT, 50):
            cv2.line(frame, (0, i), (FRAME_WIDTH, i), (100,100,100), 1)

        status_text = f"Pan: {controller.current_pan:.0f}° | Tilt: {controller.current_tilt:.0f}°"
        cv2.putText(frame, status_text, (10,30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0,255,0), 2)
        cv2.putText(frame, "CLICK TO AIM & FIRE", (10, FRAME_HEIGHT - 20), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0,255,255), 2)

        cv2.imshow("Water Gun Targeting System", frame)

        key = cv2.waitKey(1) & 0xFF
        if key == ord('q'):
            print("[EXIT] Quitting application...")
            break
        elif key == ord(' '):
            controller.reset()
        elif key == ord('f'):
            controller.fire()

    cap.release()
    cv2.destroyAllWindows()

if __name__ == "__main__":
    main()
