import shutil
import os
from behave.model_core import Status

from helpers import ScreenRecorder
from helpers.ConfigHelper import init_config, reset_sync_connection_name
from helpers.api.provisioning import delete_created_users
from helpers.SpaceHelper import delete_project_spaces
from helpers.ConfigHelper import get_config
from helpers.FilesHelper import cleanup_created_paths
from helpers.AppHelper import close_and_kill_app
from helpers.SyncHelper import clear_socket_messages, close_socket_connection
from helpers.ReportHelper import (
    normalize_scenario_title,
    hit_screenrecord_limit,
    take_screenshot,
    save_app_log,
    cleanup_current_app_log,
    save_crash_log,
)
from step_types.types import *  # noqa: F403 # register all step types


def before_feature(context, feature):
    init_config()


def before_scenario(context, scenario):
    if get_config("record_video_on_failure") and not hit_screenrecord_limit():
        ScreenRecorder.start_recording(normalize_scenario_title(scenario.name))
    elif hit_screenrecord_limit():
        print("[INFO] Screen recording limit reached.")


def after_step(context, step):
    if step.status in [Status.failed, Status.error] and os.getenv("CI"):
        take_screenshot(normalize_scenario_title(context.scenario.name))


def after_scenario(context, scenario):
    try:
        try:
            if get_config("record_video_on_failure"):ScreenRecorder.stop_recording( passed=scenario.status == Status.passed)
        except Exception as e:
            print(f"[CLEANUP] Failed to stop recording: {e}")

        try:
            close_and_kill_app()
        except Exception as e:
            print(f"[CLEANUP] Failed to close app: {e}")

        try:
            clear_socket_messages()
        except Exception as e:
            print(f"[CLEANUP] Failed to clear socket messages: {e}")

        try:
            close_socket_connection()
        except Exception as e:
            print(f"[CLEANUP] Failed to close socket: {e}")

        try:
            if (
                scenario.status in [Status.failed, Status.error]
                and os.path.exists(get_config("currentAppLogFile"))
            ):
                save_app_log(scenario)
        except Exception as e:
            print(f"[CLEANUP] Failed to save app log: {e}")

        try:
            if os.path.exists(get_config("crash_log_file")):
                save_crash_log(scenario)
        except Exception as e:
            print(f"[CLEANUP] Failed to save crash log: {e}")

        try:
            if os.path.exists(get_config("clientRootSyncPath")):
                shutil.rmtree(
                    get_config("clientRootSyncPath"),
                    ignore_errors=True,
                )
        except Exception as e:
            print(f"[CLEANUP] Failed to remove sync directory: {e}")

        try:
            cleanup_created_paths()
        except Exception as e:
            print(f"[CLEANUP] Failed to cleanup created paths: {e}")

        try:
            cleanup_current_app_log()
        except Exception as e:
            print(f"[CLEANUP] Failed to cleanup app log: {e}")

        try:
            reset_sync_connection_name()
        except Exception as e:
            print(f"[CLEANUP] Failed to reset sync connection name: {e}")

        try:
            delete_project_spaces()
        except Exception as e:
            print(f"[CLEANUP] Failed to delete project spaces: {e}")

    finally:
        try:
            print("[CLEANUP] Deleting created users...")
            delete_created_users()
            print("[CLEANUP] Created users deleted")
        except Exception as e:
            print(f"[CLEANUP] Failed to delete created users: {e}")