"""Host checks of the actual portable crop and LED mapping implementation."""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

class Pixels(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        lib = Path(cls.tmp.name) / 'pixels.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
            str(ROOT/'projects/gsp_sub_board/app/sub_board_pixels.c'), '-lm', '-o', str(lib)], check=True)
        cls.lib=C.CDLL(str(lib))
        cls.lib.sb_crop_uyvy.argtypes=[C.POINTER(C.c_uint8),C.c_size_t,C.c_uint,C.c_uint,C.c_size_t,C.POINTER(C.c_uint16)]
        cls.lib.sb_crop_uyvy.restype=C.c_bool
    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def test_center_crop_with_padded_stride(self):
        w,h,stride=640,512,1312
        src=(C.c_uint8*(stride*h))()
        for y in range(h):
            for x in range(0,w,2):
                v=235 if 80<=x<560 and 16<=y<496 else 16
                src[y*stride+x*2:y*stride+x*2+4]=[128,v,128,v]
        out=(C.c_uint16*(480*480))()
        self.assertTrue(self.lib.sb_crop_uyvy(src,len(src),w,h,stride,out))
        self.assertTrue(all(p==0xffff for p in out))
    def test_invalid_input_does_not_touch_output(self):
        src=(C.c_uint8*(480*480*2))()
        out=(C.c_uint16*(480*480))();out[0]=1234
        for w,h,stride,size in [(479,480,960,len(src)),(480,479,960,len(src)),(480,480,0,len(src)),(480,480,960,10),(481,480,962,len(src))]:
            self.assertFalse(self.lib.sb_crop_uyvy(src,size,w,h,stride,out))
            self.assertEqual(out[0],1234)
    def test_rotation_and_hsv_breath(self):
        for i in range(64):
            self.assertEqual(self.lib.sb_matrix_index(0,i),i)
            self.assertEqual(self.lib.sb_matrix_index(1,i),63-i)
        rgb=(C.c_uint8*3)()
        colors = []
        for pixel in range(64):
            self.lib.sb_color(50,pixel,rgb)
            colors.append(tuple(rgb))
        self.assertGreater(len(set(colors)), 32)
        self.assertTrue(all(max(color) == 96 for color in colors))
        self.assertTrue(all(min(color) == 0 for color in colors))
        for t in (0,100,200):
            self.lib.sb_color(t,0,rgb)
            self.assertEqual(list(rgb),[0,0,0])

if __name__=='__main__':unittest.main()
