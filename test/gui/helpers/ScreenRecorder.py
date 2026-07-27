import os
import threading
import time
import mss
import numpy as np
import imageio_ffmpeg

from helpers.ReportHelper import get_screenrecord_file_path

_recording_thread = None
_stop_event = threading.Event()
_video_path = None


def _record_loop(video_path):
    with mss.mss() as sct:
        monitor = sct.monitors[0]
        width, height = monitor["width"], monitor["height"]

        writer = imageio_ffmpeg.write_frames(
            video_path,
            size=(width, height),
            fps=24,
            codec="libx264",
            pix_fmt_out="yuv420p",
            output_params=["-crf", "23"],
        )
        writer.send(None)

        # 1/24 seconds between each frame so we get 24 frames per second
        interval = 1.0 / 24
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


def start_recording(filename):
    global _recording_thread, _video_path

    _video_path = get_screenrecord_file_path(filename)
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
