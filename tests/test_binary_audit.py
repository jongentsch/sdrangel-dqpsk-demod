"""Synthetic PE fixtures check release privacy gates without private recordings."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('audit_binary', Path(__file__).resolve().parents[1] / 'tools/audit_binary.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def fixture(extra=b'', debug_type=None):
    data = bytearray(1024)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 60, 128)
    data[128:132] = b'PE\0\0'
    struct.pack_into('<HH', data, 132, 0x8664, 1)
    struct.pack_into('<H', data, 148, 240)
    struct.pack_into('<H', data, 152, 0x20b)
    struct.pack_into('<IIII', data, 400, 512, 0x1000, 512, 512)
    if debug_type is not None:
        struct.pack_into('<II', data, 312, 0x1000, 28)
        struct.pack_into('<I', data, 524, debug_type)
    return bytes(data) + extra


class BinaryAuditTests(unittest.TestCase):
    def run_audit(self, extra=b'', denylist=(), debug_type=None):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'synthetic.dll'
            path.write_bytes(fixture(extra, debug_type))
            return module.audit(path, denylist)

    def test_clean_binary(self):
        self.assertTrue(self.run_audit()['passed'])

    def test_public_url_is_not_a_drive_path(self):
        value = 'https://example.invalid/project\0'
        self.assertTrue(self.run_audit(value.encode('utf-16le'))['passed'])

    def test_ascii_home_path(self):
        self.assertFalse(self.run_audit(b'C:\\Users\\TestAccount\\build\\file.cpp\0')['passed'])

    def test_utf16_email(self):
        self.assertFalse(self.run_audit('synthetic@example.invalid\0'.encode('utf-16le'))['passed'])

    def test_utf16be_private_value(self):
        value = 'SYNTHETIC-ID-12345'
        self.assertFalse(self.run_audit(value.encode('utf-16be'), [value])['passed'])

    def test_codeview_metadata(self):
        self.assertFalse(self.run_audit(debug_type=2)['passed'])
        self.assertTrue(self.run_audit(debug_type=13)['passed'])


if __name__ == '__main__':
    unittest.main()
