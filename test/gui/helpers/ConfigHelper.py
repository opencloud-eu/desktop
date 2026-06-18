import os
import platform
import builtins
import tempfile
from tempfile import gettempdir
from configparser import ConfigParser
from pathlib import Path

CURRENT_DIR = Path(__file__).resolve().parent
APP_CONFIG_FILE = "opencloud.cfg"
CUMULATIVE_APP_LOG_FILE = "opencloud.log"
CURRENT_APP_LOG_FILE = "app.log"


def is_windows():
    return platform.system() == 'Windows'


def is_linux():
    return platform.system() == 'Linux'


def get_win_user_home():
    return os.environ.get('USERPROFILE', '')


def get_client_root_path():
    if is_windows():
        return os.path.join(get_win_user_home(), 'opencloudtest')
    return os.path.join(gettempdir(), 'opencloudtest')


def get_config_home_linux():
    return os.path.join(tempfile.gettempdir(), 'opencloudtest', '.config')


def get_config_home_win():
    return os.path.join(
        get_win_user_home(), 'AppData', 'Local', 'Temp', 'opencloudtest', '.config'
    )


def get_config_home():
    if is_windows():
        return get_config_home_win()
    return get_config_home_linux()


def get_default_home_dir():
    if is_windows():
        return get_win_user_home()
    return os.environ.get('HOME')


def get_app_env():
    return {
        'XDG_CONFIG_HOME': get_config_home(),
        'APPDATA': get_config_home(),
    }


# map environment variables to config keys
CONFIG_ENV_MAP = {
    'app_path': 'APP_PATH',
    'localBackendUrl': 'BACKEND_HOST',
    'sync_timeout': 'SYNC_TIMEOUT',
    'clientRootSyncPath': 'CLIENT_ROOT_SYNC_PATH',
    'tempFolderPath': 'TEMP_FOLDER_PATH',
    'guiTestReportDir': 'GUI_TEST_REPORT_DIR',
    'record_video_on_failure': 'RECORD_VIDEO_ON_FAILURE',
}

# immutable configs
DEFAULT_PATH_CONFIG = {
    'custom_lib': os.path.abspath(
        os.path.join(os.path.dirname(__file__), 'custom_lib')
    ),
    'home_dir': get_default_home_dir(),
    'clientConfigFile': os.path.join(get_config_home(), "OpenCloud", APP_CONFIG_FILE),
    # allow to record first 5 videos
    'video_record_limit': 5,
    'max_timeout': 60,
    'min_timeout': 5,
    'lowest_timeout': 1,
    'files_for_upload': os.path.join(CURRENT_DIR.parent, 'files-for-upload'),
}

# mutable configs
CONFIG = {
    'app_path': None,
    'localBackendUrl': 'https://localhost:9200/',
    'sync_timeout': 60,
    'clientRootSyncPath': get_client_root_path(),
    'tempFolderPath': os.path.join(get_client_root_path(), 'temp'),
    'guiTestReportDir': os.path.join(CURRENT_DIR.parent, 'reports'),
    'record_video_on_failure': False,
    'syncConnectionName': 'Personal',
    ###############################
    # dynamic configs             #
    ###############################
    # currentAppLogFile: file path to store app logs for the current scenario run, initialized in init_config()
    # appLogFile: file path to store cumulative app logs for the entire test run, initialized in init_config()
    # currentUserSyncPath: path to store the current user's sync data, initialized in init_config()
}

# space membership permission roles mapping
PERMISSION_ROLES = {
    'Viewer': 'b1e2218d-eef8-4d4c-b82d-0f1a1b48f3b5',
    'Editor': 'fb6c3e19-e378-47e5-b277-9732f9de6e21',
}

CONFIG.update(DEFAULT_PATH_CONFIG)

READONLY_CONFIG = list(CONFIG_ENV_MAP.keys()) + list(DEFAULT_PATH_CONFIG.keys())


def read_config_from_file():
    cfg_path = os.path.abspath(os.path.join(CURRENT_DIR.parent, 'config.ini'))
    cfg = ConfigParser()

    if not cfg.read(cfg_path):
        return
    for key in CONFIG:
        if key in CONFIG_ENV_MAP and cfg.get('DEFAULT', CONFIG_ENV_MAP[key]):
            value = cfg.get('DEFAULT', CONFIG_ENV_MAP[key])
            CONFIG[key] = value


def read_config_from_env():
    for key, value in CONFIG_ENV_MAP.items():
        if os.environ.get(value):
            CONFIG[key] = os.environ.get(value)


def normalize_configs():
    for key, value in CONFIG.items():
        if key in ('sync_timeout'):
            CONFIG[key] = builtins.int(value)
        elif key == 'record_video_on_failure':
            CONFIG[key] = value == 'true'
        elif key in (
            'localBackendUrl',
            'clientRootSyncPath',
            'tempFolderPath',
            'guiTestReportDir',
        ):
            # make sure there is always one trailing slash
            CONFIG[key] = value.rstrip('/') + '/'


def init_config():
    # read and override configs from config.ini
    read_config_from_file()

    # read and override configs from environment variables
    read_config_from_env()

    # typecast and normalize config values
    normalize_configs()

    if 'app_path' not in CONFIG or not CONFIG['app_path']:
        raise KeyError('APP_PATH must be set in config.ini or environment variables')
    if not os.path.exists(CONFIG['app_path']):
        raise KeyError(f'App not found: {CONFIG["app_path"]}')
    if not os.path.isfile(CONFIG['app_path']):
        raise KeyError(f'App path is not a file: {CONFIG["app_path"]}')

    ### initialize dynamic config values
    # file to store app logs for the current scenario run
    CONFIG['currentAppLogFile'] = os.path.join(
        CONFIG["guiTestReportDir"], CURRENT_APP_LOG_FILE
    )
    # file to store cumulative app logs for the entire test run
    CONFIG['appLogFile'] = os.path.join(
        CONFIG["guiTestReportDir"], CUMULATIVE_APP_LOG_FILE
    )
    # create report dir if it not exist
    if not os.path.exists(CONFIG['guiTestReportDir']):
        os.makedirs(CONFIG['guiTestReportDir'])
    CONFIG['currentUserSyncPath'] = ''


def get_config(key):
    return CONFIG[key]


def set_config(key, value):
    if key in READONLY_CONFIG:
        raise KeyError(f'Cannot set read-only config: {key}')
    CONFIG[key] = value
