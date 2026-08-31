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
    # stop screen recording
    if get_config("record_video_on_failure"):
        ScreenRecorder.stop_recording(passed=scenario.status == Status.passed)

    # quit the application
    close_and_kill_app()
    clear_socket_messages()
    close_socket_connection()

    # store app log on scenario failure
    if scenario.status in [Status.failed, Status.error] and os.path.exists(
        get_config('currentAppLogFile')
    ):
        save_app_log(scenario)

    if os.path.exists(get_config('crash_log_file')):
        save_crash_log(scenario)

    # clean up sync dir
    if os.path.exists(get_config("clientRootSyncPath")):
        shutil.rmtree(get_config("clientRootSyncPath"))

    cleanup_created_paths()
    cleanup_current_app_log()
    reset_sync_connection_name()

    delete_project_spaces()
    delete_created_users()
