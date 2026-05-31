import serial
import time

port = '/dev/cu.usbmodem11101'
baud = 115200

print(f"Waiting for {port}...")
while True:
    try:
        with serial.Serial(port, baud, timeout=1) as ser:
            print("Connected! Listening...")
            while True:
                line = ser.readline()
                if line:
                    print(line.decode('utf-8', errors='replace').strip())
    except serial.SerialException:
        time.sleep(0.5)
    except KeyboardInterrupt:
        break
