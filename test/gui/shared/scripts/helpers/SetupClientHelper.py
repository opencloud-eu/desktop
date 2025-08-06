import uuid
import os
import subprocess
from urllib.parse import urlparse
from os import makedirs
from os.path import exists, join
import test
import psutil
import squish
from PySide6.QtCore import QSettings, QUuid, QUrl, QJsonValue

from helpers.SpaceHelper import get_space_id, get_personal_space_id
from helpers.ConfigHelper import get_config, set_config, is_windows
from helpers.SyncHelper import listen_sync_status_for_item
from helpers.api.utils import url_join
from helpers.UserHelper import get_displayname_for_user, get_password_for_user
from helpers.ReportHelper import is_video_enabled
from helpers.api import provisioning



def substitute_inline_codes(value):
    value = value.replace('%local_server%', get_config('localBackendUrl'))
    value = value.replace('%client_root_sync_path%', get_config('clientRootSyncPath'))
    value = value.replace('%current_user_sync_path%', get_config('currentUserSyncPath'))
    value = value.replace(
        '%local_server_hostname%', urlparse(get_config('localBackendUrl')).netloc
    )
    value = value.replace('%home%', get_config('home_dir'))

    return value


def get_client_details(context):
    client_details = {
        'server': '',
        'user': '',
        'password': '',
        'sync_folder': '',
        'oauth': False,
    }
    for row in context.table[0:]:
        row[1] = substitute_inline_codes(row[1])
        if row[0] == 'server':
            client_details.update({'server': row[1]})
        elif row[0] == 'user':
            client_details.update({'user': row[1]})
        elif row[0] == 'password':
            client_details.update({'password': row[1]})
        elif row[0] == 'sync_folder':
            client_details.update({'sync_folder': row[1]})
    return client_details


def create_user_sync_path(username):
    # '' at the end adds '/' to the path
    user_sync_path = join(get_config('clientRootSyncPath'), username, '')

    if not exists(user_sync_path):
        makedirs(user_sync_path)

    set_current_user_sync_path(user_sync_path)
    return user_sync_path


def create_space_path(username, space='Personal'):
    user_sync_path = create_user_sync_path(username)
    space_path = join(user_sync_path, space, '')
    if not exists(space_path):
        makedirs(space_path)
    return space_path


def set_current_user_sync_path(sync_path):
    set_config('currentUserSyncPath', sync_path)


def get_resource_path(resource='', user='', space=''):
    sync_path = get_config('currentUserSyncPath')
    if user:
        sync_path = user
    space = space or get_config('syncConnectionName')
    sync_path = join(sync_path, space)
    sync_path = join(get_config('clientRootSyncPath'), sync_path)
    resource = resource.replace(sync_path, '').strip('/').strip('\\')
    return join(
        sync_path,
        resource,
    )


def parse_username_from_sync_path(sync_path):
    return sync_path.split('/').pop()


def get_temp_resource_path(resource_name):
    return join(get_config('tempFolderPath'), resource_name)


def get_current_user_sync_path():
    return get_config('currentUserSyncPath')


def start_client():
    squish.startApplication(
        'opencloud -s'
        + f' --logfile {get_config("clientLogFile")}'
        + ' --logdebug'
        + ' --logflush'
    )
    if is_video_enabled():
        test.startVideoCapture()
    else:
        test.log(
            f'Video recordings reached the maximum limit of {get_config("video_record_limit")}.'
            + 'Skipping video recording...'
        )


def get_polling_interval():
    polling_interval = '''
[OpenCloud]
remotePollInterval={polling_interval}
'''
    args = {'polling_interval': 5000}
    polling_interval = polling_interval.format(**args)
    return polling_interval

def generate_account_config(users, space='Personal'):
    sync_paths = {}
    settings = QSettings(get_config('clientConfigFile'), QSettings.Format.IniFormat)
    users_uuids = {}
    server_url = get_config('localBackendUrl')
    capabilities = provisioning.get_capabilities()
    capabilities_variant = QJsonValue(capabilities).toVariant()

    for idx, username in enumerate(users):
        users_uuids[username] = QUuid.createUuid()
        settings.beginGroup("Accounts")
        settings.beginWriteArray(str(idx+1),len(users))

        settings.setValue("capabilities", capabilities_variant)
        settings.setValue("default_sync_root", create_user_sync_path(username))
        settings.setValue("uuid", users_uuids[username])
        settings.setValue("display-name", get_displayname_for_user(username))
        settings.setValue("url", server_url)
        settings.setValue("userExplicitlySignedOut", 'false')

        settings.endArray()
        settings.setValue("size", len(users))
        settings.endGroup()

    settings.beginGroup("Folders")
    for idx, username in enumerate(users):
        sync_path = create_space_path(username, space)
        settings.beginWriteArray(str(idx+1),len(users))

        if space == 'Personal':
            space_id = get_personal_space_id(username)
        else:
            space_id = get_space_id(space, username)
        dav_endpoint = QUrl(url_join(server_url, '/dav/spaces/', space_id))
        settings.setValue("spaceId", space_id)
        settings.setValue("accountUUID", users_uuids[username])
        settings.setValue("davUrl", dav_endpoint)
        settings.setValue("deployed", 'false')
        settings.setValue("displayString", get_config('syncConnectionName'))
        settings.setValue("ignoreHiddenFiles", 'true')
        settings.setValue("localPath", sync_path)
        settings.setValue("paused", 'false')
        settings.setValue("priority", '50')
        settings.setValue("virtualFilesMode", 'off')
        settings.setValue("journalPath",".sync_journal.db")
        settings.endArray()
        settings.setValue("size", len(users))
        sync_paths.update({username: sync_path})

    settings.endGroup()


    settings.sync()
    return sync_paths

def setup_client(username, space='Personal'):
    set_config('syncConnectionName', space)
    sync_paths = generate_account_config([username], space)
    start_client()
    for _, sync_path in sync_paths.items():
        listen_sync_status_for_item(sync_path)


def is_app_killed(pid):
    try:
        psutil.Process(pid)
        return False
    except psutil.NoSuchProcess:
        return True


def wait_until_app_killed(pid=0):
    timeout = 5 * 1000
    killed = squish.waitFor(
        lambda: is_app_killed(pid),
        timeout,
    )
    if not killed:
        test.log(f'Application was not terminated within {timeout} milliseconds')


def generate_uuidv4():
    return str(uuid.uuid4())


# sometimes the keyring is locked during the test execution, and we need to unlock it
def unlock_keyring():
    if is_windows():
        return

    stdout, stderr, _ = run_sys_command(
        [
            'busctl',
            '--user',
            'get-property',
            'org.freedesktop.secrets',
            '/org/freedesktop/secrets/collection/login',
            'org.freedesktop.Secret.Collection',
            'Locked',
        ]
    )
    output = ''
    if stdout:
        output = stdout.decode('utf-8')
    if stderr:
        output = stderr.decode('utf-8')

    if not output.strip().endswith('false'):
        test.log('Unlocking keyring...')
        password = os.getenv('VNC_PW')
        command = f'echo -n "{password}" | gnome-keyring-daemon -r -d --unlock'
        stdout, stderr, returncode = run_sys_command(command, True)
        if stdout:
            output = stdout.decode('utf-8')
        if stderr:
            output = stderr.decode('utf-8')
        if returncode:
            test.log(f'Failed to unlock keyring:\n{output}')


def run_sys_command(command=None, shell=False):
    cmd = subprocess.run(
        command,
        shell=shell,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    return cmd.stdout, cmd.stderr, cmd.returncode
