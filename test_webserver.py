import serial, time, subprocess

time.sleep(3)
s = serial.Serial('/dev/ttyUSB0', 115200, timeout=1)
s.read_all()
s.write(b'CMD:WEB\n')
s.flush()
time.sleep(30)
out = s.read_all().decode(errors='replace')
lines = [l.strip() for l in out.split('\n') if l.strip() and 'webserver' in l]
print("Serial:", " | ".join(lines[-6:]))
s.close()
time.sleep(2)
r = subprocess.run(["curl","-s","--max-time","10","http://192.168.1.17/"],capture_output=True,text=True,timeout=15)
print("HTTP /:", r.stdout[:60] if r.stdout else "(empty)")
r2 = subprocess.run(["curl","-s","--max-time","10","http://192.168.1.17/files"],capture_output=True,text=True,timeout=15)
print("HTTP /files:", r2.stdout[:80] if r2.stdout else "(empty)")
