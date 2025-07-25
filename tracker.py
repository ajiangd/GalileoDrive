from skyfield.api import load, Topos, Star
import geocoder
import os
import time
import sys
import serial
import logging

logging.basicConfig(
    level=logging.INFO,                      # Could also be DEBUG, WARNING, etc.
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler("/home/ashleyjiang/startracker/logs/logfile.log"),  # Logs go to a file
        logging.StreamHandler()              # Also print to console
    ]
)

logging.info('starting')

"""
#cd startracker
#$ source star-env/bin/activate

#sudo fuser -k /dev/ttyACM0
"""



# === Serial setup ===
ser = serial.Serial('/dev/ttyACM0', 115200, timeout=1)

#print("Listening to Arduino")

#if len(sys.argv) < 2:
    #print("No target specified.")
    #sys.exit(1)
    
#target_name = sys.argv[1].strip().capitalize()


#print(f"Tracking target: {target_name}")

#if target_name == 'Jupiter':
#    target_name = target_name + ' Barycenter'

target_name = 'Jupiter Barycenter'
planets = load('de421.bsp')

logging.info('target name' + target_name)

name_to_id = {
    'Mercury': 1,
    'Venus': 2,
    'Earth': 3,
    'Mars': 4,
    'Jupiter Barycenter': 5,
    'Saturn': 6,
    'Uranus': 7,
    'Neptune': 8,
    'Pluto': 9,
    'Sun': 10,
    'Moon': 301,
}

# Get the name from the command line argument
# target_name = sys.argv[1]

#if target_name not in name_to_id:
#   raise ValueError(f"Invalid target: {target_name}")

earth = planets['earth']
target = planets[target_name]


#name = target.target_name.split(' ', 1)[1].capitalize()

#print("Tracking target until it sets...\n")

while True:
    # g = geocoder.ip('me')  # Get location from public IP
    lat, lon = 39.9, 116.4    # Test coordinates
    # lat, lon = g.latlng    # Calculates location based on IP address

    ts = load.timescale()
    t = ts.now()

    try:
        # Create observer based on IP-derived coordinates
        observer = earth + Topos(latitude_degrees=lat, longitude_degrees=lon, elevation_m=1)
        #observer = earth + Topos(latitude_degrees=lat, longitude_degrees=lon)
    except:
        pass    

    try:
        # Compute Target position
        astrometric = observer.at(t).observe(target)
        alt, az, distance = astrometric.apparent().altaz()
        
    except:
        pass
    
    #os.system('clear')
    #print(f"Observer \n  Latitude: {lat}° \n  Longitude: {lon}°\n")
    #print(f"{target_name} \n  Azimuth: {az.degrees:.2f}° \n  Altitude: {alt.degrees:.2f}° \n  Distance: {distance.km:.2f} km\n")
    
    #if alt.degrees > 0 or alt.degrees < 0:
    if alt.degrees > 0:
        
        command = f"{az.degrees:.2f} {alt.degrees:.2f}\n"
        
        logging.info(f'tx--> Object Angle: {alt.degrees:.2f} | Heading: {az.degrees:.2f}')
        
        #with serial.Serial('/dev/ttyACM0', 115200, timeout=1) as ser:
        try:
            ser.write(command.encode('utf-8'))
        except Exception as e:
            logging.info(e)      
        
    
        try:
            if ser.in_waiting:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                logging.info('<--rx ' + line)
        except Exception as e:
            logging.error(f"Serial read error: {e}")
         
    else:
        #print(f"Target is now below the horizon (Altitude: {alt.degrees:.2f}°).")
        logging.info('x')     
        
