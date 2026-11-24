import array
import ctypes as c
import math
import random
import sys
import wave
from pathlib import Path

class Effect(c.Structure):
    pass

ep = c.POINTER(Effect)
callback = c.CFUNCTYPE(c.c_ssize_t, ep, c.c_int32, c.c_int32, c.c_ssize_t, c.c_void_p, c.c_float)
pair = c.POINTER(c.POINTER(c.c_float))
process = c.CFUNCTYPE(None, ep, pair, pair, c.c_int32)

Effect._fields_ = [
    ("magic", c.c_int32), ("dispatch", callback), ("old_process", c.c_void_p),
    ("set", c.c_void_p), ("get", c.c_void_p),
    ("counts", c.c_int32 * 5), ("reserved", c.c_ssize_t * 2),
    ("delay", c.c_int32), ("qualities", c.c_int32 * 2), ("ratio", c.c_float),
    ("object", c.c_void_p), ("user", c.c_void_p), ("uid", c.c_int32),
    ("version", c.c_int32), ("process", process),
]

@callback
def host(effect, opcode, index, value, ptr, opt):
    return 2400 if opcode == 1 else 0

rate, block = 44100, 128
names = ("West Coast", "Boom Bap", "Trap 808", "Drum & Bass", "Lo-Fi Hip Hop", "Drill Grit")
library = c.CDLL(sys.argv[1])
entry = library.VSTPluginMain
entry.argtypes, entry.restype = [callback], ep

def groove():
    rng = random.Random(24471)
    bpm = 98
    step = 60 / bpm / 4
    length = int(step * 16 * rate)
    left = [0.0] * length
    right = [0.0] * length
    def hit(at, duration, pan, signal):
        start = int(step * at * rate)
        for j in range(min(int(duration * rate), length - start)):
            value = signal(j / rate)
            left[start+j] += value * (1 - 0.35 * pan)
            right[start+j] += value * (1 + 0.35 * pan)
    for at in (0, 3, 7, 10, 14):
        hit(at, 0.22, 0, lambda t: 0.53 * math.sin(math.tau * (51*t + 0.8*(1-math.exp(-67*t))))*math.exp(-20*t) + 0.17*math.sin(math.tau*2650*t)*math.exp(-155*t))
    for at in (0.12, 3.12, 7.12, 10.12, 14.12):
        hz = 42 if at < 8 else 49
        hit(at, 0.40, 0, lambda t: 0.28*math.sin(math.tau*hz*t)*math.exp(-6*t)*min(1,t*800)*(1-t/0.40))
    for at in (4, 12):
        hit(at, 0.17, 0.1, lambda t: (0.46 * rng.uniform(-1,1) + 0.18*math.sin(math.tau*178*t))*math.exp(-31*t))
    for at in range(0,16,2):
        pan = 0.7 if at % 4 else -0.7
        hit(at, 0.07, pan, lambda t: 0.16 * rng.uniform(-1,1)*math.exp(-88*t))
    return list(zip(left,right))

def render(program, dry):
    plug = entry(host)
    call = plug.contents.dispatch
    for opcode,value,opt in [(0,0,0),(10,0,rate),(11,block,0),(2,program,0),(12,1,0)]:
        call(plug,opcode,0,value,None,opt)
    label = c.create_string_buffer(32)
    call(plug,5,0,0,c.cast(label,c.c_void_p),0)
    assert label.value.decode() == names[program]
    output = []
    for start in range(0,len(dry),block):
        samples = dry[start:start+block]
        count = len(samples)
        ins = [(c.c_float*count)(*(p[k] for p in samples)) for k in range(2)]
        outs = [(c.c_float*count)() for k in range(2)]
        plug.contents.process(plug,(c.POINTER(c.c_float)*2)(*ins),(c.POINTER(c.c_float)*2)(*outs),count)
        output.extend(zip(*outs))
    call(plug,12,0,0,None,0)
    call(plug,1,0,0,None,0)
    return output

def rms(samples):
    return math.sqrt(sum(l*l+r*r for l,r in samples)/(2*len(samples)))

def high_low_ratio(samples):
    low = high = state = 0.0
    pole = 1-math.exp(-math.tau*500/rate)
    for l,r in samples:
        sample = (l+r)*0.5
        state += pole*(sample-state)
        low += state**2
        high += (sample-state)**2
    return 10*math.log10(high/low)

dry = groove()
sections = [dry] + [render(index,dry) for index in range(6)]
target = min(0.1, min(rms(s)*0.92/max(max(abs(l),abs(r)) for l,r in s) for s in sections))
ratios = []
with wave.open(sys.argv[2],"wb") as wav:
    wav.setparams((2,2,rate,0,"NONE","not compressed"))
    for i,(name, samples) in enumerate(zip(("Dry",)+names,sections),1):
        scale = target/rms(samples)
        crest = 20*math.log10(max(max(abs(l),abs(r)) for l,r in samples)/rms(samples))
        ratio = high_low_ratio(samples)
        ratios.append(ratio)
        print(f"{i}. {name}: peak-to-RMS {crest:.1f} dB, high/low {ratio:.1f} dB")
        wav.writeframes(array.array("h",(round(x*scale*32767) for lr in samples for x in lr)).tobytes())
        wav.writeframes(bytes(4*int(rate*0.32)))
assert ratios[2] - ratios[5] > 5
assert ratios[6] - ratios[3] > 5
