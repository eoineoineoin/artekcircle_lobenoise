import array
import struct
import re
import time
import sys

recordings = {}
cur_samples = None
f = open("brainwave-recordings", "r")
for ln in f.readlines():
    ln = ln.strip()
    if ln.startswith("#"):
        timestamp, label = ln[1:].split(" ", 1)
        cur_samples = []
        recordings[label] = {'timestamp' : int(timestamp), 'samples' : cur_samples}
    elif ln.startswith('+'):
        columns = ln[1:].split(" ")
        hexdata = columns[0]
        bindata = array.array('B', [int(hexdata[x : x + 2], 16) for x in range(0, len(hexdata), 2)])
        if bindata[0] != 1:
            # Ignore (wrong packet type)
            continue
        bindata = bindata[1:]
        # From Bernhart's code:
        # Timestamp (6)
        # Wireless Frame PDN (1)
        # Wireless Frame Misc (1)
        # ADC Channel 0 64/62 13.5b samples (128/124)
        # Temperature Code (1)
        # Accelerometer (6)
        # ED Measurement (1)
        timestamp = sum([bindata[5 - i] << (8 * i) for i in range(6)])
        pdn = bindata[6]
        misc = bindata[7]
        samples = bindata[8:-8]
        adc_data = array.array('h', [((samples[i] * 256 + samples[i + 1] - (65536 if samples[i] & 128 else 0)) >> 2) for i in range(0, len(samples), 2)])
        samples = bindata[-7:-1]
        accel = tuple([(samples[i] * 256 + samples[i + 1] - (65536 if samples[i] & 128 else 0)) for i in range(0, len(samples), 2)])
        cur_samples.append({'timestamp' : timestamp, 'pdn' : pdn, 'misc' : misc, 'adc_data' : adc_data, 'accel' : accel, 'temperature' : bindata[-8], 'ed' : bindata[-1]})
        print cur_samples[-1]

