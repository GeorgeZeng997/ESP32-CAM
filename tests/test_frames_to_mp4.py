import re
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "tools"))

import frames_to_mp4  # noqa: E402


class TestFramesToMp4(unittest.TestCase):
    def test_load_manifest_and_estimate_fps(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            session = Path(tmp) / "run_0001"
            session.mkdir()
            for i in range(3):
                (session / f"frame_{i:06d}.jpg").write_bytes(b"\xff\xd8\xff\xd9")

            manifest = (
                "frame_index,filename,uptime_ms,bytes,width,height\n"
                "0,frame_000000.jpg,1000,4,800,600\n"
                "1,frame_000001.jpg,1200,4,800,600\n"
                "2,frame_000002.jpg,1400,4,800,600\n"
                "3,../outside.jpg,1600,4,800,600\n"
                "4,missing.jpg,1800,4,800,600\n"
            )
            (session / "manifest.csv").write_text(manifest, encoding="utf-8")

            records = frames_to_mp4.load_manifest(session)
            self.assertEqual(len(records), 3)
            self.assertAlmostEqual(frames_to_mp4.estimate_fps(records), 5.0, places=2)

            concat_script = frames_to_mp4.build_ffconcat_script(session, records)
            self.assertIn("ffconcat version 1.0", concat_script)
            self.assertEqual(concat_script.count("duration 0.2000"), 3)

            rc = frames_to_mp4.main([str(session), "--dry-run"])
            self.assertEqual(rc, 0)

    def test_fallback_unindexed_frames(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            session = Path(tmp) / "run_legacy"
            session.mkdir()
            (session / "frame_000000.jpg").write_bytes(b"\xff\xd8\xff\xd9")
            (session / "frame_000001.jpg").write_bytes(b"\xff\xd8\xff\xd9")

            records = frames_to_mp4.discover_unindexed_frames(session)
            self.assertEqual(len(records), 2)
            self.assertEqual(records[1].filename, "frame_000001.jpg")

    def test_repo_hygiene_and_camera_guards(self) -> None:
        cjk_re = re.compile(r"[\u4e00-\u9fff]")
        for path in REPO_ROOT.rglob("*"):
            if ".git" in path.parts or "__pycache__" in path.parts or not path.is_file():
                continue
            text = path.read_text(encoding="utf-8")
            self.assertIsNone(cjk_re.search(text), f"Unexpected CJK character in {path}")

        pins_header = (REPO_ROOT / "src" / "camera_pins.h").read_text(encoding="utf-8")
        self.assertTrue(pins_header.startswith("#pragma once"))


if __name__ == "__main__":
    unittest.main()
