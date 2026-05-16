import logging
import os
import sys
import time
import serial
import yaml
from skyfield.api import load, Topos

TARGETS = {
    1: ("Mercury",          "mercury"),
    2: ("Venus",            "venus"),
    3: ("Mars",             "mars"),
    4: ("Jupiter",          "jupiter barycenter"),
    5: ("Saturn",           "saturn barycenter"),
    6: ("Uranus",           "uranus barycenter"),
    7: ("Neptune",          "neptune barycenter"),
    8: ("Moon",             "moon"),
    9: ("Sun",              "sun"),
}


def load_config(path="config.yaml"):
    with open(path) as f:
        return yaml.safe_load(f)


def setup_logging(cfg):
    log_file = cfg["logging"]["file"]
    os.makedirs(os.path.dirname(log_file), exist_ok=True)
    level = getattr(logging, cfg["logging"]["level"].upper(), logging.INFO)
    logging.basicConfig(
        level=level,
        format="%(asctime)s - %(levelname)s - %(message)s",
        handlers=[
            logging.FileHandler(log_file),
            logging.StreamHandler(),
        ],
    )


def select_target():
    print("\nAvailable targets:")
    for num, (name, _) in TARGETS.items():
        print(f"  {num}. {name}")
    while True:
        try:
            choice = int(input("Select target (1-9): ").strip())
            if choice in TARGETS:
                return TARGETS[choice]
            print("Invalid choice, try again.")
        except ValueError:
            print("Enter a number.")


def compute_position(observer, target, ts):
    t = ts.now()
    astrometric = observer.at(t).observe(target)
    alt, az, distance = astrometric.apparent().altaz()
    return az.degrees, alt.degrees


def run_tracker(cfg, ser, observer, target_body, ts):
    interval = cfg["tracker"]["update_interval_s"]
    while True:
        try:
            az, alt = compute_position(observer, target_body, ts)
        except Exception as e:
            logging.error(f"Position compute error: {e}")
            time.sleep(interval)
            continue

        if alt > 0:
            cmd = f"AZ:{az:.2f} ALT:{alt:.2f}\n"
            logging.info(f"tx --> AZ:{az:.2f} ALT:{alt:.2f}")
            try:
                ser.write(cmd.encode("utf-8"))
            except Exception as e:
                logging.error(f"Serial write error: {e}")
        else:
            logging.info(f"Below horizon (ALT:{alt:.2f}) — not sending")
            try:
                ser.write(b"CMD:STOP\n")
            except Exception as e:
                logging.error(f"Serial write error: {e}")

        try:
            line = ""
            if ser.in_waiting:
                line = ser.readline().decode("utf-8", errors="ignore").strip()
            if line:
                logging.info(f"rx <-- {line}")
        except Exception as e:
            logging.error(f"Serial read error: {e}")

        time.sleep(interval)


def main():
    cfg = load_config()
    setup_logging(cfg)

    if "--check-config" in sys.argv:
        obs = cfg["observer"]
        print(f"Observer: lat={obs['latitude']} lon={obs['longitude']} elev={obs['elevation_m']}m")
        print("Config OK")
        return

    logging.info("Star tracker starting")

    ts = load.timescale()
    planets = load(cfg["tracker"]["ephemeris"])
    earth = planets["earth"]

    obs_cfg = cfg["observer"]
    observer = earth + Topos(
        latitude_degrees=obs_cfg["latitude"],
        longitude_degrees=obs_cfg["longitude"],
        elevation_m=obs_cfg["elevation_m"],
    )

    ser_cfg = cfg["serial"]

    while True:
        target_label, target_key = select_target()
        logging.info(f"Target selected: {target_label}")

        try:
            target_body = planets[target_key]
        except Exception as e:
            logging.error(f"Could not load target '{target_key}': {e}")
            continue

        # Show initial position before opening serial
        try:
            az, alt = compute_position(observer, target_body, ts)
            print(f"\n{target_label}: AZ={az:.2f}°  ALT={alt:.2f}°")
            if alt <= 0:
                print("Warning: target is currently below the horizon.")
        except Exception as e:
            logging.error(f"Initial position error: {e}")

        print("Connecting to Arduino... (Ctrl+C to re-select target)\n")
        try:
            with serial.Serial(ser_cfg["port"], ser_cfg["baud"], timeout=1) as ser:
                time.sleep(2)  # allow Arduino to reset after serial open
                run_tracker(cfg, ser, observer, target_body, ts)
        except KeyboardInterrupt:
            print("\nReturning to target menu...")
            logging.info("User interrupted — returning to target menu")
        except serial.SerialException as e:
            logging.error(f"Serial error: {e}")
            print("Serial connection failed. Check the port in config.yaml.")
            time.sleep(2)


if __name__ == "__main__":
    main()
