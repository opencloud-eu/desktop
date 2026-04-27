import psutil
import shutil
import os

from helpers.ConfigHelper import init_config
from helpers.api.provisioning import delete_created_users
from helpers.SpaceHelper import delete_project_spaces
from helpers.ConfigHelper import set_config, get_config
from helpers.FilesHelper import prefix_path_namespace, cleanup_created_paths
from helpers.SetupClientHelper import app
from step_types.types import *  # register all step types


def before_feature(context, feature):
    init_config()


def before_scenario(context, feature):
    set_config("currentUserSyncPath", "")


def after_scenario(context, scenario):
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
    if app() is not None:
        app().quit()
    for process in psutil.process_iter(['pid', 'exe']):
        if process.info['exe'] == get_config("app_path"):
            print("Closing desktop client...")
            psutil.Process(process.info['pid']).kill()
            break
