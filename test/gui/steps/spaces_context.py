from sure import ensure

from pageObjects.EnterPassword import EnterPassword
from helpers.UserHelper import get_password_for_user
from helpers.SetupClientHelper import setup_client, get_resource_path
from helpers.SyncHelper import wait_for_initial_sync_to_complete
from helpers.SpaceHelper import (
    create_space,
    create_space_folder,
    create_space_file,
    add_user_to_space,
    get_file_content,
    resource_exists,
)
from helpers.ConfigHelper import get_config, set_config


@Given('the administrator has created a space "{space_name}"')
def step(context, space_name):
    create_space(space_name)


@Given('the administrator has created a folder "{folder_name}" in space "{space_name}"')
def step(context, folder_name, space_name):
    create_space_folder(space_name, folder_name)


@Given(
    'the administrator has uploaded a file "{file_name}" with content "{content}" inside space "{space_name}"'
)
def step(context, file_name, content, space_name):
    create_space_file(space_name, file_name, content)


@Given(
    'the administrator has added user "{user}" to space "{space_name}" with role "{role}"'
)
def step(context, user, space_name, role):
    add_user_to_space(user, space_name, role)


@Given('user "{user}" has set up a client with space "{space_name}"')
def step(context, user, space_name):
    set_config('syncConnectionName', space_name)
    password = get_password_for_user(user)
    setup_client(user, space_name)
    enter_password = EnterPassword()
    enter_password.accept_certificate()
    enter_password.login_after_setup(user, password)
    # wait for files to sync
    wait_for_initial_sync_to_complete(get_resource_path('/', user, space_name))


@Then(
    'as "{user}" the file "{file_name}" in the space "{space_name}" should have content "{content}" in the server'
)
def step(context, user, file_name, space_name, content):
    downloaded_content = get_file_content(space_name, file_name, user)
    with ensure(
        'File "{0}" in space "{1}" should have content "{2}" but got "{3}"',
        file_name,
        space_name,
        content,
        downloaded_content,
    ):
        content.should.equal(downloaded_content)


@Then(
    'as "{user}" the space "{space_name}" should have file "{resource_name}" in the server'
)
@Then(
    'as "{user}" the space "{space_name}" should have folder "{resource_name}" in the server'
)
def step(context, user, space_name, resource_name):
    exists = resource_exists(space_name, resource_name, user)
    with ensure('Resource "{0}" should exist but it does not', resource_name):
        exists.should.be.true
