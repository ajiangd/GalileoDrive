from skyfield.api import load, Topos, Star
import geocoder
import os
import time

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

    os.system('clear')

    print(f"Your latitude: {lat}° \nYour longitude: {lon}°\n")

    # Create observer based on IP-derived coordinates
    observer = earth + Topos(latitude_degrees=lat, longitude_degrees=lon, elevation_m=1000000)
    #observer = earth + Topos(latitude_degrees=lat, longitude_degrees=lon)


    # Compute Target position
    astrometric = observer.at(t).observe(target)
    alt, az, distance = astrometric.apparent().altaz()

    if alt.degrees > 0:
        print(f"{name} Altitude: {alt.degrees:.2f}° \n{name} Azimuth: {az.degrees:.2f}° \n{name} Distance: {distance.km:.2f} km\n\n")
        time.sleep(5)  # Wait for 60 seconds before next update
    else:
        print(f"Target is now below the horizon (Altitude: {alt.degrees:.2f}°).")
        break
