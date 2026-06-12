import os
import re
import pyautogui

from helpers.ConfigHelper import get_config


def normalize_scenario_title(title):
    scenario_title = title.lower().strip()
    scenario_title = re.sub(r'\W', '_', scenario_title)
    return scenario_title


def get_screenrecords_path():
    return os.path.join(get_config("guiTestReportDir"), "recordings")


def get_screenshots_path():
    return os.path.join(get_config("guiTestReportDir"), "screenshots")


def get_screenrecord_file_path(filename):
    report_dir = get_screenrecords_path()
    if not os.path.exists(report_dir):
        os.makedirs(report_dir)

    return os.path.join(report_dir, f"{filename}.mp4")


def hit_screenrecord_limit():
    video_report_dir = get_screenrecords_path()
    if not os.path.exists(video_report_dir):
        return False
    entries = [f for f in os.scandir(video_report_dir) if f.is_file()]
    return len(entries) >= get_config("video_record_limit")


def take_screenshot(filename):
    directory = get_screenshots_path()
    if not os.path.exists(directory):
        os.makedirs(directory)
    file_path = os.path.join(directory, f"{filename}.png")
    try:
        pyautogui.screenshot(file_path)
    except Exception as e:
        print(f"[WARN] Failed to save screenshot: {e}")


def append_scenario_to_app_log(scenario):
    with open(get_config('appLogFile'), 'a') as log_file:
        logs = ["=" * 80]
        logs.append(
            f"Scenario: {scenario.name}\nLocation: {scenario.filename}:{scenario.line}"
        )
        logs.append("-" * 80)
        logs.append("")  # extra line break
        log_file.write("\n".join(logs))


def store_app_log():
    with open(get_config('appLogFile'), 'a') as log_file:
        # client log is stored in utf-16.
        with open(
            get_config('currentAppLogFile'), 'r', encoding='utf-16'
        ) as current_log:
            log_file.write(f"{current_log.read()}\n\n")


def cleanup_current_app_log():
    if os.path.exists(get_config('currentAppLogFile')):
        os.remove(get_config('currentAppLogFile'))
