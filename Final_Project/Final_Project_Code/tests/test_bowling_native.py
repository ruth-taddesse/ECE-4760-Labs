"""Exercise the actual C classifier/physics, replay recorded throws, render actual UI calls.
Build bowling_native.dll with the command in BOWLING_DEMO.md first.
"""
import csv
import ctypes as C
import json
import math
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host'))
from train_bowling import extract, CATEGORIES

F3=C.c_float*3
class Features(C.Structure):
    _fields_=[('count',C.c_uint),('peak',C.c_float),('low',F3),('high',F3),('integral',F3),('sum_a',F3),('previous_g',F3),('previous_t',C.c_float)]
class Shot(C.Structure):
    _fields_=[('label',C.c_int),('power',C.c_float),('hook',C.c_float),('certainty',C.c_float),('peak',C.c_float),('features',C.c_float*13)]
class Tuning(C.Structure):
    _fields_=[(k,C.c_float) for k in ['start_x','aim_deg','speed_gain','hook_gain','friction','hook_start','randomness']]
class Point(C.Structure):
    _fields_=[(k,C.c_float) for k in ['x','y','t']]
class PathResult(C.Structure):
    _fields_=[('points',Point*1600),('count',C.c_uint),('pin',C.c_int),('gutter',C.c_bool),('stopped',C.c_bool),('speed',C.c_float),('hook_accel',C.c_float)]

lib=C.CDLL(str(ROOT/'build/bowling_native.dll'))
lib.bowl_defaults.restype=Tuning
lib.bowl_features_reset.argtypes=[C.POINTER(Features)]
lib.bowl_features_add.argtypes=[C.POINTER(Features),F3,F3,C.c_float]
lib.bowl_classify.argtypes=[C.POINTER(Features),C.POINTER(Shot)];lib.bowl_classify.restype=C.c_bool
lib.bowl_simulate.argtypes=[C.POINTER(Shot),C.POINTER(Tuning),C.c_uint,C.POINTER(PathResult)]
lib.bowl_set.argtypes=[C.POINTER(Tuning),C.c_char_p,C.c_float];lib.bowl_set.restype=C.c_bool
lib.test_render.argtypes=[C.POINTER(Shot),C.POINTER(Tuning),C.c_uint,C.c_float]
lib.test_render.restype=C.POINTER(C.c_ubyte)

def classify(rows):
    f=Features();lib.bowl_features_reset(C.byref(f))
    for r in rows:
        if r['phase']!='motion':continue
        a=F3(*(float(r['corrected_'+k]) for k in ['ax_mps2','ay_mps2','az_mps2']))
        g=F3(*(float(r['corrected_'+k]) for k in ['gx_rads','gy_rads','gz_rads']))
        lib.bowl_features_add(C.byref(f),a,g,int(r['elapsed_us'])/1e6)
    result=Shot();ok=lib.bowl_classify(C.byref(f),C.byref(result))
    return ok,result

def simulate(shot,tuning=None,seed=42):
    tuning=tuning or lib.bowl_defaults();result=PathResult()
    lib.bowl_simulate(C.byref(shot),C.byref(tuning),seed,C.byref(result))
    return result

class BowlingTests(unittest.TestCase):
    def test_straight_and_first_pin(self):
        p=simulate(Shot(power=.5,hook=0))
        self.assertEqual(p.pin,0)
        self.assertFalse(p.gutter)
        self.assertTrue(all(abs(p.points[i].x)<1e-6 for i in range(p.count)))
        self.assertAlmostEqual(p.points[p.count-1].y,5.7-.29,places=4)

    def test_hook_sign_and_symmetry(self):
        left=simulate(Shot(power=.4,hook=-1));right=simulate(Shot(power=.4,hook=1))
        self.assertLess(left.points[left.count-1].x,0)
        self.assertGreater(right.points[right.count-1].x,0)
        self.assertEqual(left.count,right.count)
        for i in range(left.count):self.assertAlmostEqual(left.points[i].x,-right.points[i].x,places=5)

    def test_speed_gain_and_gutter(self):
        slow=simulate(Shot(power=0));fast=simulate(Shot(power=1))
        self.assertGreater(slow.points[slow.count-1].t,fast.points[fast.count-1].t)
        t=lib.bowl_defaults();t.start_x=1.3;t.aim_deg=10
        p=simulate(Shot(power=.5),t)
        self.assertTrue(p.gutter);self.assertEqual(p.pin,-1)
        self.assertAlmostEqual(p.points[p.count-1].x,1.44,places=5)

    def test_randomness_and_tuning_bounds(self):
        shot=Shot(power=.5,hook=.5);t=lib.bowl_defaults()
        a=simulate(shot,t,1);b=simulate(shot,t,2)
        self.assertEqual(bytes(a),bytes(b))
        t.randomness=1
        a=simulate(shot,t,1);b=simulate(shot,t,1234567)
        self.assertNotEqual(bytes(a),bytes(b));self.assertEqual(bytes(a),bytes(simulate(shot,t,1)))
        for key,value in [(b'aim_deg',float('nan')),(b'speed_gain',0),(b'hook_start',1),(b'unknown',1)]:
            self.assertFalse(lib.bowl_set(C.byref(t),key,value))

    def test_stationary_rejected(self):
        f=Features();lib.bowl_features_reset(C.byref(f))
        for i in range(200):lib.bowl_features_add(C.byref(f),F3(0,0,9.80665),F3(0,0,0),.5+i*.01)
        shot=Shot();self.assertFalse(lib.bowl_classify(C.byref(f),C.byref(shot)))

    def test_recorded_feature_parity_and_paths(self):
        groups={}
        with (ROOT/'data/bowling.csv').open(newline='') as f:
            for r in csv.DictReader(f):groups.setdefault(r['trial_id'],[]).append(r)
        model=json.loads((ROOT/'bowling_model_report.json').read_text())
        representatives={};summary=[]
        for rows in groups.values():
            ok,shot=classify(rows);self.assertTrue(ok)
            reference=extract(rows)
            for a,b in zip(shot.features,reference):self.assertAlmostEqual(a,b,delta=2e-4)
            distances=[sum(((a-b)/s)**2 for a,b,s in zip(reference,center,model['scale']))/13 for center in model['centers']]
            self.assertEqual(shot.label,min(range(5),key=lambda i:distances[i]))
            p=simulate(shot);self.assertGreater(p.count,1);self.assertLess(p.count,1600)
            self.assertTrue(all(math.isfinite(p.points[i].x+p.points[i].y+p.points[i].t) for i in range(p.count)))
            self.assertTrue(all(p.points[i].t>p.points[i-1].t for i in range(1,p.count)))
            self.assertTrue(all(abs(p.points[i].x)<=1.44001 for i in range(p.count)))
            label=rows[0]['throw_category'];representatives.setdefault(label,shot)
            summary.append(dict(trial=rows[0]['trial_number'],label=label,prediction=CATEGORIES[shot.label],power=shot.power,hook=shot.hook,first_pin=p.pin+1,end_x=p.points[p.count-1].x))
        (ROOT/'build/bowling_replay_check.json').write_text(json.dumps(summary,indent=2))
        # Native firmware rendering into a stubbed VGA pixel buffer; no hardware timing claim.
        from PIL import Image
        palette=[(0,0,0),(0,85,0),(0,170,0),(0,255,0),(0,0,128),(0,85,255),(0,170,255),(0,255,255),(255,0,0),(255,85,0),(255,170,0),(255,255,0),(255,0,255),(255,85,255),(255,170,255),(255,255,255)]
        tuning=lib.bowl_defaults()
        for name in ['straight_normal','hook_left','hook_right']:
            pixels=lib.test_render(C.byref(representatives[name]),C.byref(tuning),42,20)
            image=Image.frombytes('P',(640,480),C.string_at(pixels,640*480));image.putpalette([v for c in palette for v in c]+[0]*(768-48))
            image.convert('RGB').save(ROOT/'build'/f'bowling_preview_{name}.png')

if __name__=='__main__':unittest.main()
