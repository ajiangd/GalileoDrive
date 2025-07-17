from skyfield.api import load, Topos, Star
import geocoder
import os
import time
import serial

"""
cd startracker
$ source star-env/bin/activate

sudo fuser -k /dev/ttyACM0


"""

# === Serial setup ===
ser = serial.Serial('/dev/ttyACM0', 115200, timeout=1)
time.sleep(2)
transmitcounter = 0

print("Listening to Arduino")
        

planets = load('de421.bsp')
earth = planets['earth']
target = planets['venus']

name = target.target_name.split(' ', 1)[1].capitalize()

print("Tracking target until it sets...\n")

while True:
    # g = geocoder.ip('me')  # Get location from public IP
    lat, lon = 39.9, 116.4    # Test coordinates
    # lat, lon = g.latlng    # Calculates location based on IP address

    ts = load.timescale()
    t = ts.now()


    
    # Create observer based on IP-derived coordinates
    observer = earth + Topos(latitude_degrees=lat, longitude_degrees=lon, elevation_m=1000000)
    #observer = earth + Topos(latitude_degrees=lat, longitude_degrees=lon)


    # Compute Target position
    astrometric = observer.at(t).observe(target)
    alt, az, distance = astrometric.apparent().altaz()
    
    os.system('clear')
    print(f"Observer \n  Latitude: {lat}° \n  Longitude: {lon}°\n")
    print(f"{name} \n  Azimuth: {az.degrees:.2f}° \n  Altitude: {alt.degrees:.2f}° \n  Distance: {distance.km:.2f} km\n")


    if alt.degrees > 0:
        transmitcounter += 1
        
        command = f"{az.degrees:.2f} {alt.degrees:.2f}\n"
        
        #with serial.Serial('/dev/ttyACM0', 115200, timeout=1) as ser:
        ser.write(command.encode('utf-8'))
        
        print(f"Tranmission Counter: {transmitcounter} \n")
        print(f"--> Arduino: {az.degrees:.2f} {alt.degrees:.2f}")
        time.sleep(0.5)
        
        time.sleep(0.2)
        while ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            print(f"<-- Arduino: {line}\n")
        
    else:
        print(f"Target is now below the horizon (Altitude: {alt.degrees:.2f}°).")
        break
        
    time.sleep(5)
