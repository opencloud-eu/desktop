import shutil
import os
from behave.model_core import Status

from helpers import ScreenRecorder
from helpers.ConfigHelper import init_config
from helpers.api.provisioning import delete_created_users
from helpers.SpaceHelper import delete_project_spaces
from helpers.ConfigHelper import get_config
from helpers.FilesHelper import prefix_path_namespace, cleanup_created_paths
from helpers.AppHelper import close_and_kill_app
from helpers.SyncHelper import clear_socket_messages
from helpers.ReportHelper import (
    normalize_scenario_title,
    hit_screenrecord_limit,
    take_screenshot,
    save_app_log,
    cleanup_current_app_log,
)
from step_types.types import *  # register all step types


def before_feature(context, feature):
    init_config()


def before_scenario(context, scenario):
    if (
        os.getenv("CI")
        and get_config("record_video_on_failure")
        and not hit_screenrecord_limit()
    ):
        ScreenRecorder.start_recording(normalize_scenario_title(scenario.name))
    elif hit_screenrecord_limit():
        print("[INFO] Screen recording limit reached.")


def after_step(context, step):
    if step.status in [Status.failed, Status.error] and os.getenv("CI"):
        take_screenshot(normalize_scenario_title(context.scenario.name))


def after_scenario(context, scenario):
    # stop screen recording
    if os.getenv("CI") and get_config("record_video_on_failure"):
        ScreenRecorder.stop_recording(passed=scenario.status == Status.passed)

    # quit the application
    close_and_kill_app()

    # store app log on scenario failure
    if scenario.status in [Status.failed, Status.error] and os.path.exists(
        get_config('currentAppLogFile')
    ):
        save_app_log(scenario)

    # clean up sync dir
    if os.path.exists(get_config("clientRootSyncPath")):
        for entry in os.scandir(get_config("clientRootSyncPath")):
            try:
                if entry.is_file() or entry.is_symlink():
                    print("Deleting file: " + entry.name)
                    os.unlink(prefix_path_namespace(entry.path))
                elif entry.is_dir():
                    print("Deleting folder: " + entry.name)
                    shutil.rmtree(prefix_path_namespace(entry.path))
            except OSError as e:
                print(f"Failed to delete '{entry.name}'.\nReason: {e}.")
    # cleanup paths created outside of the temporary directory during the test
    cleanup_created_paths()
    delete_project_spaces()
    delete_created_users()

    cleanup_current_app_log()
    clear_socket_messages()
