"""Provisioning must fit the pinned Iris RPC limit, including a real PEM."""
import importlib.util
import json
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location('coop_pair', Path(__file__).parents[1] / 'tools/pair.py')
pair = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pair)


class PairingTests(unittest.TestCase):
    def test_certificate_payload_round_trip_with_system_wifi(self):
        payload = {'certificate': '-----BEGIN CERTIFICATE-----\n' + 'A' * 1800,
                   'token': 'a' * 64, 'uri': 'wss://192.0.2.1:19773/mosaico/v1',
                   'use_system_wifi': True}
        collected = bytearray()
        chunks = list(pair.pairing_chunks(payload))
        self.assertGreater(len(chunks), 1)
        for chunk in chunks:
            self.assertLessEqual(len(chunk), 1024)
            total, offset = struct.unpack('<HH', chunk[:4])
            self.assertEqual(offset, len(collected))
            collected.extend(chunk[4:])
        self.assertEqual(total, len(collected))
        self.assertEqual(json.loads(collected), payload)
        self.assertNotIn('password', json.loads(collected))

    def test_overflow_rejected_before_any_rpc(self):
        with self.assertRaises(ValueError):
            list(pair.pairing_chunks({'certificate': 'A' * 6144}))


if __name__ == '__main__':
    unittest.main()
