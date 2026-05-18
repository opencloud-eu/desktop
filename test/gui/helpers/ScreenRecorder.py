import os
import re
import threading
import time
import mss
import numpy as np
import imageio_ffmpeg
from datetime import datetime

from helpers.ConfigHelper import get_config


_recording_thread = None
_stop_event = threading.Event()
_video_path = None


def _build_video_path(scenario):
    safe_name = re.sub(r"[^a-zA-Z0-9_]", "_", scenario.name)
    timestamp = datetime.now().strftime("%d-%b-%Y_%H-%M-%S")

    recordings_dir = os.path.join(get_config("guiTestReportDir"), "recordings")
    os.makedirs(recordings_dir, exist_ok=True)

    return os.path.join(recordings_dir, f"{safe_name}_{timestamp}.mp4")


def _record_loop(video_path):
    with mss.mss() as sct:
        monitor = sct.monitors[0]
        width, height = monitor["width"], monitor["height"]

        writer = imageio_ffmpeg.write_frames(
            video_path,
            size=(width, height),
            fps=24,
            codec="libx264",
            output_params=["-crf", "23", "-pix_fmt", "yuv420p"],
        )
        writer.send(None)

        interval = 1.0 / 24 # 1/24 seconds between each frame so we get 24 frames per second
        next_frame_at = time.monotonic()

        while not _stop_event.is_set():
            frame = sct.grab(monitor)
            # mss gives BGRA — drop alpha, flip B and R channels to get RGB
            rgb = np.flip(np.array(frame)[:, :, :3], axis=2).tobytes()
            writer.send(rgb)

            next_frame_at += interval
            sleep_for = next_frame_at - time.monotonic()
            if sleep_for > 0:
                time.sleep(sleep_for)

        writer.close()


def start_recording(scenario):
    global _recording_thread, _video_path

    _video_path = _build_video_path(scenario)
    _stop_event.clear()

    _recording_thread = threading.Thread(target=_record_loop, args=(_video_path,), daemon=True)
    _recording_thread.start()


def stop_recording(passed):
    global _recording_thread, _video_path

    if _recording_thread is None:
        return

    _stop_event.set()
    _recording_thread.join()
    _recording_thread = None

    if passed and os.path.exists(_video_path):
        os.remove(_video_path)

    _video_path = None
