import shutil
import os
import re
import pyautogui
from behave.model_core import Status
from datetime import datetime

from helpers import ScreenRecorder
from helpers.ConfigHelper import init_config
from helpers.api.provisioning import delete_created_users
from helpers.SpaceHelper import delete_project_spaces
from helpers.ConfigHelper import get_config
from helpers.FilesHelper import prefix_path_namespace, cleanup_created_paths
from helpers.AppHelper import close_and_kill_app
from helpers.SyncHelper import clear_socket_messages
from step_types.types import *  # register all step types


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


def cleanup_app_log():
    if os.path.exists(get_config('currentAppLogFile')):
        os.remove(get_config('currentAppLogFile'))


def before_feature(context, feature):
    init_config()


def before_scenario(context, scenario):
    if os.getenv("CI"):
        ScreenRecorder.start_recording(scenario)


def after_step(context, step):
    if step.status in [Status.failed, Status.error] and os.getenv("CI"):
        scenario = context.scenario.name.lower()
        scenario = re.sub(r'[^a-zA-Z0-9_]', '_', scenario)
        timestamp = datetime.now().strftime("%d-%b-%Y_%H-%M-%S")
        screenshots_dir = os.path.join(get_config("guiTestReportDir"), "screenshots")
        os.makedirs(screenshots_dir, exist_ok=True)

        file_path = os.path.join(screenshots_dir, f"{scenario}_{timestamp}.png")
        pyautogui.screenshot(file_path)


def after_scenario(context, scenario):

    # stop screen recording
    if os.getenv("CI"):
        ScreenRecorder.stop_recording(passed=scenario.status == Status.passed)

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
    # quit the application
    close_and_kill_app()

    # store app log on scenario failure
    if scenario.status in [Status.failed, Status.error] and os.path.exists(
        get_config('currentAppLogFile')
    ):
        append_scenario_to_app_log(scenario)
        store_app_log()
    cleanup_app_log()
    clear_socket_messages()
