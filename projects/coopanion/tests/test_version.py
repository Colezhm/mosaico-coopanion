"""One firmware version: CMake project(), the app descriptor and the simulator."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class VersionTests(unittest.TestCase):
    def test_single_version_source(self):
        cmake = re.search(r'project\(coopanion VERSION ([0-9.]+)\)', (ROOT / 'CMakeLists.txt').read_text())
        config = re.search(r'^CONFIG_APP_PROJECT_VER="([^"]+)"$', (ROOT / 'sdkconfig.defaults').read_text(), re.M)
        self.assertIsNotNone(cmake)
        self.assertIsNotNone(config)
        # CONFIG_APP_PROJECT_VER_FROM_CONFIG overrides project(); both must agree.
        self.assertEqual(cmake.group(1), config.group(1))

    def test_no_hardcoded_capability_version(self):
        for name in ['main/coop_board.c', 'pc/link_pc.c']:
            text = (ROOT / name).read_text()
            self.assertIsNone(re.search(r'mosaico-coopanion/\d', text), name)


if __name__ == '__main__':
    unittest.main()
