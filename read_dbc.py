import sys
f = open(r'd:\Photon\dbc\CarCAN.dbc', 'rb')
data = f.read()
f.close()
text = data.decode('latin-1')
for line in text.split('\n'):
    s = line.strip()
    if s.startswith('SG_') or s.startswith('BO_'):
        print(s[:120])

print("=== WAVESCULPTOR ===")
f2 = open(r'd:\Photon\dbc\prohelion_wavesculptor22.dbc', 'rb')
data2 = f2.read()
f2.close()
text2 = data2.decode('latin-1')
for line2 in text2.split('\n'):
    s2 = line2.strip()
    if s2.startswith('SG_') or s2.startswith('BO_'):
        print(s2[:120])
