"""Desktop protocol and ground-state regression. Hardware radio/VGA timing is not simulated."""
import ctypes as C
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
lib=C.CDLL(str(ROOT/'build_wireless/wireless_native.dll'))
class Shot(C.Structure):
    _fields_=[('label',C.c_int),('power',C.c_float),('hook',C.c_float),('certainty',C.c_float),('peak',C.c_float),('features',C.c_float*13)]
class Packet(C.Structure):
    _fields_=[(n,C.c_uint32) for n in 'magic version kind session sequence target request operation state phase countdown healthy calibrated joy_x joy_y select_count menu_count samples errors missed rail'.split()]+[('shot',Shot),('calibration_error',C.c_char*128)]
lib.wire_valid.argtypes=[C.POINTER(Packet),C.c_uint,C.c_uint];lib.wire_valid.restype=C.c_bool
lib.wire_newer.argtypes=[C.c_uint32,C.c_uint32];lib.wire_newer.restype=C.c_bool
class WirelessTests(unittest.TestCase):
    def test_protocol_validation(self):
        p=Packet(magic=0x50535031,version=1,kind=2,session=42)
        self.assertEqual(C.sizeof(p),284)
        self.assertTrue(lib.wire_valid(C.byref(p),284,2))
        self.assertFalse(lib.wire_valid(C.byref(p),283,2))
        self.assertFalse(lib.wire_valid(C.byref(p),284,1))
        p.joy_x=4096;self.assertFalse(lib.wire_valid(C.byref(p),284,2));p.joy_x=2048
        p.state=5;p.shot.power=float('nan');self.assertFalse(lib.wire_valid(C.byref(p),284,2))
        p.shot.power=.5;p.shot.hook=2;self.assertFalse(lib.wire_valid(C.byref(p),284,2))
        p.shot.hook=0;self.assertTrue(lib.wire_valid(C.byref(p),284,2))
        p.kind=1;p.operation=99;self.assertFalse(lib.wire_valid(C.byref(p),284,1))
    def test_ordering(self):
        self.assertTrue(lib.wire_newer(2,1));self.assertFalse(lib.wire_newer(1,1))
        self.assertFalse(lib.wire_newer(1,2));self.assertTrue(lib.wire_newer(0,0xffffffff))
    def test_ground_state_recovery(self):
        self.assertEqual(lib.test_remote(),0)
    def test_joystick_without_imu_and_after_disconnect(self):
        self.assertEqual(lib.test_remote_joystick_without_imu(),0)
if __name__=='__main__':unittest.main()
