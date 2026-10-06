import os
import re
import builtins
import shutil
import zipfile
from os.path import isfile, join, isdir, exists
from behave import when as When, then as Then, given as Given
from sure import ensure

import helpers.FileExplorerHelper as FileExplorer
from helpers.SetupClientHelper import get_resource_path, get_temp_resource_path
from helpers.SyncHelper import (
    listen_sync_status_for_item,
)
from helpers.Utils import wait_for
from helpers.ConfigHelper import get_config
from helpers.FilesHelper import (
    build_conflicted_regex,
    normalize_path,
    can_read,
    can_write,
    read_file_content,
    get_size_in_bytes,
    prefix_path_namespace,
    remember_path,
    convert_path_separators_for_os,
    get_file_for_upload,
)
from helpers.TableParser import table_hashes, validate_table_headers


def folder_exists(folder_path, timeout=get_config('min_timeout')):
    return wait_for(
        lambda: isdir(normalize_path(folder_path)),
        timeout,
    )


def file_exists(file_path, timeout=get_config('min_timeout')):
    return wait_for(
        lambda: isfile(normalize_path(file_path)),
        timeout,
    )


# To create folders in a temporary directory, set is_temp_folder True
def create_folder(foldername, username=None, is_temp_folder=False):
    if is_temp_folder:
        folder_path = join(get_config('test_temp_dir'), foldername)
    else:
        folder_path = get_resource_path(foldername, username)
    os.makedirs(prefix_path_namespace(convert_path_separators_for_os(folder_path)))


def rename_file_folder(source, destination):
    source = get_resource_path(source)
    destination = get_resource_path(destination)
    os.rename(source, destination)


def create_file_with_size(filename, filesize, is_temp_folder=False):
    if is_temp_folder:
        file = join(get_config('test_temp_dir'), filename)
    else:
        file = get_resource_path(filename)
    with open(prefix_path_namespace(file), 'wb') as f:
        f.seek(get_size_in_bytes(filesize) - 1)
        f.write(b'\0')


def write_file(resource, content):
    with open(prefix_path_namespace(resource), 'w', encoding='utf-8') as f:
        f.write(content)


def write_file_to_sync_path(path, content):
    listen_sync_status_for_item(get_resource_path(path), 'FILE')
    write_file(path, content)


def try_to_write_file(resource, content):
    listen_sync_status_for_item(get_resource_path(resource), 'FILE')
    try:
        write_file(resource, content)
    except Exception:
        pass


def create_zip(resources, zip_file_name, cwd=''):
    original_cwd = os.getcwd()
    os.chdir(cwd)
    try:
        with zipfile.ZipFile(zip_file_name, 'w') as zipped_file:
            for resource in resources:
                zipped_file.write(resource)
    finally:
        os.chdir(original_cwd)


def extract_zip(zip_file_path, destination_dir):
    with zipfile.ZipFile(zip_file_path, 'r') as zip_file:
        zip_file.extractall(destination_dir)


def add_copy_suffix(resource_path, resource_type):
    suffix = ' (Copy)'
    if resource_type == 'file':
        source_dir = resource_path.rsplit('.', 1)
        return source_dir[0] + suffix + '.' + source_dir[-1]
    return resource_path + suffix


def copy_resource(resource_type, source, destination, from_files_for_upload=False):
    source = get_file_for_upload(source) if from_files_for_upload else get_resource_path(source)
    destination = get_resource_path(destination)
    if source == destination and destination != '/':
        destination = add_copy_suffix(source, resource_type)

    listen_sync_status_for_item(destination, resource_type)
    if resource_type == 'folder':
        return shutil.copytree(source, destination)
    return shutil.copy2(source, destination)


def move_resource(username, resource_type, source, destination, is_temp_folder=False):
    if not is_temp_folder:
        source = get_resource_path(source, username)
    if destination == '/':
        destination = ''
    destination = get_resource_path(destination, username)

    listen_sync_status_for_item(destination, resource_type)
    shutil.move(source, destination)


def delete_resource(resource, resource_type):
    listen_sync_status_for_item(resource, resource_type)
    resource_path = normalize_path(get_resource_path(resource))
    if resource_type == 'file':
        os.remove(resource_path)
    else:
        shutil.rmtree(resource_path)


@Given('user "{username}" has created the following folders inside the sync folder:')
def step(context, username):
    validate_table_headers(context.table, ['foldername'])
    folders = table_hashes(context.table)
    for folder in folders:
        create_folder(folder['foldername'], username)


@Given('user "{username}" has created the following files inside the sync folder:')
@When('user "{username}" creates the following files inside the sync folder:')
@When('user "{username}" updates the content of the following files inside the sync folder:')
def step(context, username):
    validate_table_headers(context.table, ['filename', 'content'])
    files = table_hashes(context.table)
    for file in files:
        file_path = get_resource_path(file["filename"], username)
        file_path = convert_path_separators_for_os(file_path)
        content = file.get("content", "")
        write_file_to_sync_path(file_path, content)


@Given(
    'user "{username}" has created a file "{filename}" with the following content inside the sync folder'
)
@When(
    'user "{username}" creates a file "{filename}" with the following content inside the sync folder'
)
@When(
    'user "{username}" updates file "{filename}" with the following content inside the sync folder'
)
def step(context, username, filename):
    file = get_resource_path(filename, username)
    write_file_to_sync_path(convert_path_separators_for_os(file), context.text)


@Given('user "{username}" has created a folder "{foldername}" inside the sync folder')
@When('user "{username}" creates a folder "{foldername}" inside the sync folder')
def step(context, username, foldername):
    create_folder(foldername, username)


@When('user "{_user}" creates a file "{filename}" with size "{filesize}" inside the sync folder')
def step(context, _user, filename, filesize):
    create_file_with_size(filename, filesize)


@When(r'the user copies (file|folder) "([^"]*)" into folder "([^"]*)"', regexp=True)
def step(context, resource_type, resource_name, destination_dir):
    copy_resource(resource_type, resource_name, destination_dir, False)


@When('the user copies {resource_type:ResourceType} "{resource_name}" into the same directory')
def step(context, resource_type, resource_name):
    copy_resource(resource_type, resource_name, resource_name, False)


@When('the user renames a file "{source}" to "{destination}"')
@When('the user renames a folder "{source}" to "{destination}"')
def step(context, source, destination):
    rename_file_folder(source, destination)


@Then('the following files should exist on the file system with the content:')
def step(context):
    validate_table_headers(context.table, ['filename', 'content'])
    files = table_hashes(context.table)
    for file in files:
        file_path = get_resource_path(file['filename'])
        with open(file_path, encoding='utf-8') as f:
            content = f.read()
        expected_content = file['content']
        with ensure(
            'Content mismatch for file "{file_path}"\n'
            + 'Expected: {expected}\n'
            + 'Actual: {actual}',
            file_path=file_path,
            expected=expected_content,
            actual=content,
        ):
            content.should.equal(expected_content)


@Then('the file "{file_path}" should exist on the file system with the following content')
def step(context, file_path):
    expected = context.text
    file_path = get_resource_path(file_path)
    with open(file_path, encoding='utf-8') as f:
        contents = f.read()
    with ensure(
        '{0} expected to exist with content "{1}" but has content "{2}"',
        file_path,
        expected,
        contents,
    ):
        contents.should.equal(expected)


@Then('the {resource_type:ResourceType} "{resource}" should exist on the file system')
def step(context, resource_type, resource):
    resource_path = get_resource_path(resource)
    resource_exists = False
    timeout = get_config('max_timeout')
    if resource_type == 'file':
        resource_exists = file_exists(resource_path, timeout)
    else:
        resource_exists = folder_exists(resource_path, timeout)

    with ensure(
        '{0} "{1}" should exist, but it does not',
        resource_type.capitalize(),
        resource,
    ):
        resource_exists.should.be.true


@Then('the {resource_type:ResourceType} "{resource}" should not exist on the file system')
def step(context, resource_type, resource):
    resource_path = get_resource_path(resource)
    with ensure(
        '{0} "{1}" should not exist, but it does',
        resource_type.capitalize(),
        resource,
    ):
        exists(resource_path).should.be.false


@Given('the user has changed the content of local file "{filename}" to:')
def step(context, filename):
    file_content = context.text
    write_file_to_sync_path(get_resource_path(filename), file_content)


@Then('a conflict file for "{filename}" should exist on the file system with the following content')
def step(context, filename):
    expected = context.text

    onlyfiles = [f for f in os.listdir(get_resource_path()) if isfile(get_resource_path(f))]
    found = False
    pattern = re.compile(build_conflicted_regex(filename))
    for file in onlyfiles:
        if pattern.match(file):
            with open(get_resource_path(file), encoding='utf-8') as f:
                if f.read() == expected:
                    found = True
                    break

    if not found:
        raise AssertionError('Conflict file not found with given name')


@When('the user overwrites the file "{resource}" with content "{content}"')
def step(context, resource, content):
    resource = get_resource_path(resource)
    write_file_to_sync_path(resource, content)


@When('the user tries to overwrite the file "|any|" with content "|any|"')
def step(context, resource, content):
    resource = get_resource_path(resource)
    try_to_write_file(resource, content)


@When('user "|any|" tries to overwrite the file "|any|" with content "|any|"')
def step(context, user, resource, content):
    resource = get_resource_path(resource, user)
    try_to_write_file(resource, content)


@When('the user deletes the {resource_type:ResourceType} "{resource_name}"')
def step(context, resource_type, resource_name):
    delete_resource(resource_name, resource_type)


@Given('the user has created a folder "{folder_name}" in temp folder')
def step(context, folder_name):
    create_folder(folder_name, is_temp_folder=True)


@Given(
    'the user has created "{file_number}" files each of size "{file_size}" bytes inside folder "{folder_name}" in temp folder'
)
def step(context, file_number, file_size, folder_name):
    current_sync_path = get_temp_resource_path(folder_name)
    if folder_exists(current_sync_path):
        file_size = builtins.int(file_size)
        for i in range(builtins.int(file_number)):
            file_name = f'file{i}.txt'
            create_file_with_size(join(current_sync_path, file_name), file_size, True)
    else:
        raise FileNotFoundError(f"Folder '{folder_name}' does not exist in the temp folder")


@When(
    r'user "([^"]*)" reads the content of file "([^"]*)"',
    regexp=True,
)
def step(context, username, file):
    file_path = get_resource_path(file, username)
    with open(file_path) as f:
        f.read()


@When(
    r'user "{username}" moves {resource_type} "{resource_name}" from the temp folder into the sync folder'
)
def step(context, username, resource_type, resource_name):
    source_dir = join(get_config('test_temp_dir'), resource_name)
    move_resource(username, resource_type, source_dir, '/', True)


@When(
    r'user "([^"]*)" moves (folder|file) "([^"]*)" to the temp folder',
    regexp=True,
)
def step(context, username, resource_type, resource_name):
    destination = join(get_config('test_temp_dir'), resource_name)
    move_resource(username, resource_type, resource_name, destination)


@When(
    'user "{username}" moves {resource_type:ResourceType} "{source}" to "{destination}" in the sync folder'
)
def step(context, username, resource_type, source, destination):
    move_resource(username, resource_type, source, destination)


@Then('user "{user}" should be able to open the file "{file_name}" on the file system')
def step(context, user, file_name):
    file_path = get_resource_path(file_name, user)
    with ensure(
        'File should be readable but user "{0}" cannot read file "{1}"',
        user,
        file_name,
    ):
        can_read(file_path).should.be.true


@Then('as "{user}" the file "{file_name}" should have content "{content}" on the file system')
def step(context, user, file_name, content):
    file_path = get_resource_path(file_name, user)
    file_content = read_file_content(file_path)
    with ensure(
        'File "{0}" should have content "{1}" but got "{2}"',
        file_name,
        content,
        file_content,
    ):
        content.should.equal(file_content)


@Then('user "{user}" should not be able to edit the file "{file_name}" on the file system')
def step(context, user, file_name):
    file_path = get_resource_path(file_name, user)
    with ensure('File should not be writable, but it is'):
        can_write(file_path).should.be.false


@Given(
    'the user has created a zip file "{zip_file_name}" with the following resources in the temp folder'
)
def step(context, zip_file_name):
    resource_list = []

    for row in context.table:
        resource_list.append(row[0])
        resource = join(get_config('test_temp_dir'), row[0])
        if row[1] == 'folder':
            os.makedirs(resource)
        elif row[1] == 'file':
            content = ''
            if len(row) > 2 and row[2]:
                content = row[2]
            write_file(resource, content)
    create_zip(resource_list, zip_file_name, get_config('test_temp_dir'))


@When('user "{username}" unzips the zip file "{zip_file_name}" inside the sync root')
def step(context, username, zip_file_name):
    destination_dir = get_resource_path('/', username)
    zip_file_path = join(destination_dir, zip_file_name)
    extract_zip(zip_file_path, destination_dir)


@When('user "|any|" copies file "|any|" to temp folder')
def step(context, username, source):
    source_dir = get_resource_path(source, username)
    destination_dir = get_temp_resource_path(source)
    shutil.copy2(source_dir, destination_dir)


@Given('the user has created folder "{folder_name}" in the default home path')
def step(context, folder_name):
    folder_path = join(get_config('home_dir'), folder_name)
    os.makedirs(prefix_path_namespace(folder_path))
    remember_path(folder_path)
    # when account is added, folder with suffix will be created
    remember_path(f'{folder_path} (2)')


@Given(
    'the user has copied file "{resource_name}" from outside the sync folder to "{destination}" in the sync folder',
    regexp=True,
)
def step(context, resource_name, destination):
    copy_resource('file', resource_name, destination, True)


@When(
    'the user copies file "{resource_name}" from outside the sync folder to "{destination}" in the sync folder',
    regexp=True,
)
def step(context, resource_name, destination):
    copy_resource('file', resource_name, destination, True)


@When('the user deletes the following files')
def step(context):
    for row in context.table:
        filename = row[0]
        delete_resource(filename, 'file')


@Given('the user has created a file "{filename}" with size "{filesize}" in the sync folder')
def step(context, filename, filesize):
    create_file_with_size(filename, filesize)


@When(
    'user "{username}" copies the private link of file "{resource}" from the file explorer context menu'
)
@When(
    'user "{username}" copies the private link of folder "{resource}" from the file explorer context menu'
)
def step(context, username, resource):
    resource_path = get_resource_path(resource, username)
    FileExplorer.copy_private_link(resource_path)


@Then(
    'the following file explorer context menu items should be available for file "{resource}" of user "{username}"'
)
@Then(
    'the following file explorer context menu items should be available for folder "{resource}" of user "{username}"'
)
def step(context, resource, username):
    expected_menus = [row['menu'] for row in table_hashes(context.table)]
    FileExplorer.check_file_context_menu_items(resource, expected_menus)

    for item in expected_menus:
        with ensure(f'Menu item "{item}" not found in the actual list: {actual_menus}'):
            (item in actual_menus).should.be.true


@Then('the private link should be copied to the clipboard')
def step(context):
    clipboard_content = FileExplorer.get_clipboard_content()
    base_url = get_config('localBackendUrl').rstrip("/")
    link_pattern = rf'^{re.escape(base_url)}/f/[0-9A-Fa-f-%\$]+$'

    with ensure(
        f'Clipboard content "{clipboard_content}" does not match the private link pattern "{link_pattern}"'
    ):
        (re.fullmatch(link_pattern, clipboard_content)).should_not.be.none
