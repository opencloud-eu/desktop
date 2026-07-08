import time
from behave import when as When, then as Then, given as Given
from sure import ensure

from pageObjects.SyncConnectionWizard import SyncConnectionWizard
from pageObjects.Toolbar import Toolbar
from pageObjects.Activity import Activity
from pageObjects.SyncConnection import SyncConnection
from pageObjects.Settings import Settings
from helpers.ConfigHelper import set_config
from helpers.SyncHelper import (
    wait_for_resource_to_sync,
    wait_for_resource_to_have_sync_error,
)
from helpers.SetupClientHelper import (
    get_temp_resource_path,
    set_current_user_sync_path,
    substitute_inline_codes,
    get_resource_path,
)
from helpers.FilesHelper import convert_path_separators_for_os
from helpers.TableParser import table_hashes, table_raw


def _check_activities(context, not_synced=False, should_exist=True):
    field = "status" if not_synced else "action"
    activities = table_hashes(context.table)
    for activity in activities:
        activity["account"] = substitute_inline_codes(activity["account"])
        has_activity = Activity.has_activity(
            activity["resource"], activity[field], activity["account"]
        )
        with ensure(
            'Activity should exist: {0} | {1} | {2}',
            activity["resource"],
            activity[field],
            activity["account"],
        ):
            if should_exist:
                has_activity.should.be.true
            else:
                has_activity.should.be.false


@Given('the user has paused the file sync')
def step(context):
    SyncConnection.pause_sync()


@When('the user resumes the file sync on the client')
def step(context):
    SyncConnection.resume_sync()


@When('the user force syncs the files')
def step(context):
    SyncConnection.force_sync()


@When('the user waits for the files to sync')
def step(context):
    wait_for_resource_to_sync(get_resource_path('/'), force_sync=True)


@When('the user waits for {resource_type:ResourceType} "{resource}" to be synced')
def step(context, resource_type, resource):
    resource = get_resource_path(resource)
    wait_for_resource_to_sync(convert_path_separators_for_os(resource), resource_type)
    Toolbar.wait_toolbar_enabled()


@When(r'the user waits for (file|folder) "([^"]*)" to have sync error', regexp=True)
def step(context, resource_type, resource):
    resource = get_resource_path(resource)
    wait_for_resource_to_have_sync_error(resource, resource_type)


@When(r'user "([^"]*)" waits for (file|folder) "([^"]*)" to have sync error', regexp=True)
def step(context, username, resource_type, resource):
    resource = get_resource_path(resource, username)
    wait_for_resource_to_have_sync_error(resource, resource_type)


@When('the user opens the activity tab')
def step(context):
    Toolbar.open_activity()


@When('the user opens the settings tab')
def step(context):
    Toolbar.open_settings_tab()


@Then('the table of conflict warnings should include file "{filename}"')
def step(context, filename):
    Activity.has_conflict_file(filename)


@Then('the {resource_type:ResourceType} "{resource_name}" should be blacklisted')
def step(context, resource_type, resource_name):
    with ensure(f'{resource_type.capitalize()} is blacklisted'):
        Activity.is_resource_blacklisted(resource_name).should.be.true


@Then('the file "|any|" should be ignored')
def step(context, filename):
    with ensure("File is not ignored"):
        Activity.is_resource_ignored(filename).should.be.true


@Then('the file "{filename}" should be excluded')
def step(context, filename):
    with ensure('File is Excluded'):
        Activity.is_resource_excluded(filename).should.be.true


@When('the user selects "{tab_name}" tab in the activity')
def step(context, tab_name):
    Activity.open_tab(tab_name)


@Then('the toolbar should have the following tabs:')
def step(context):
    tabs = table_raw(context.table)
    for row in tabs:
        tab_name = row[0]
        with ensure('Tab not found: {0}', tab_name):
            Toolbar.has_tab(tab_name).should.be.true


@When('the user selects only the following folders to sync:')
def step(context):
    folders = []
    for row in context.table:
        folders.append(row[0])
    SyncConnectionWizard.deselect_all_remote_folders()
    SyncConnectionWizard.select_folders_to_sync(folders, new_sync_connection_wizard=True)


@When('the user sorts the folder list by "{header_text}"')
def step(context, header_text):
    if (header_text := header_text.capitalize()) in ['Size', 'Name']:
        SyncConnectionWizard.sort_by(header_text)
    else:
        raise ValueError("Sorting by '" + header_text + "' is not supported.")


@Then('the sync all checkbox should be checked')
def step(context):
    with ensure(
        'Sync all checkbox is checked',
    ):
        SyncConnectionWizard.is_root_folder_checked().should.be.true


@Then('the folders should be in the following order:')
def step(context):
    row_index = 0
    for row in context.table:
        expected_folder = row[0]
        actual_folder = SyncConnectionWizard.get_item_name_from_row(row_index)
        with ensure(f"Expected '{expected_folder}', got '{actual_folder}'"):
            actual_folder.should.be.equal(expected_folder)

        row_index += 1


@When('the user selects "{space_name}" space in sync connection wizard')
def step(context, space_name):
    SyncConnectionWizard.select_space(space_name)
    SyncConnectionWizard.next_step()
    set_config('syncConnectionName', space_name)


@When('the user sets the sync path in sync connection wizard')
def step(context):
    SyncConnectionWizard.set_sync_path()


@When('the user sets the temp folder "{folder_name}" as local sync path in sync connection wizard')
def step(context, folder_name):
    sync_path = get_temp_resource_path(folder_name)
    SyncConnectionWizard.set_sync_path(sync_path)
    set_current_user_sync_path(sync_path)


@When('the user syncs the "{space_name}" space')
def step(context, space_name):
    SyncConnectionWizard.sync_space(space_name)


@Then('the settings tab should have the following options in the general section:')
def step(context):
    settings = table_raw(context.table)
    for row in settings:
        setting = row[0]
        with ensure('General setting not found: {0}', setting):
            Settings.has_general_setting(setting).should.be.true


@Then('the settings tab should have the following options in the advanced section:')
def step(context):
    settings = table_raw(context.table)
    for row in settings:
        setting = row[0]
        with ensure('Advanced setting not found: {0}', setting):
            Settings.has_advanced_setting(setting).should.be.true


@Then('the settings tab should have the following options in the network section:')
def step(context):
    settings = table_raw(context.table)
    for row in settings:
        setting = row[0]
        with ensure('Network setting not found: {0}', setting):
            Settings.has_network_setting(setting).should.be.true


@When('the user opens the about dialog')
def step(context):
    Settings.open_about_dialog()


@Then('the about dialog should be opened')
def step(context):
    with ensure('About dialog is not opened.'):
        Settings.has_about_dialog().should.be.true


@When('the user closes the about dialog')
def step(context):
    Settings.close_about_dialog()


@When('the user adds the folder sync connection')
def step(context):
    SyncConnectionWizard.add_sync_connection()


@When('user unselects all the remote folders')
def step(context):
    SyncConnectionWizard.deselect_all_remote_folders()


@Then('for user "{user}" sync folder "{sync_folder}" should not be displayed')
def step(context, user, sync_folder):
    Toolbar.open_account(user)
    has_sync_connection = SyncConnection.has_sync_connection(sync_folder)
    with ensure('There should not be "{0}" folder sync connection, but found.', sync_folder):
        has_sync_connection.should.be.false


@When('the user navigates back in the sync connection wizard')
def step(context):
    SyncConnectionWizard.back()


@When('the user removes the folder sync connection')
def step(context):
    SyncConnection.remove_folder_sync_connection()
    SyncConnection.confirm_folder_sync_connection_removal()


@Then('the file "{file_name}" should have status "{status}" in the activity tab')
def step(context, file_name, status):
    Activity.has_sync_status(file_name, status)


@Then('the add space button should be disabled')
def step(context):
    with ensure('Add space Button to open sync connection wizard should be disabled'):
        SyncConnectionWizard.is_add_space_button_enabled().should.be.false


@When('the user checks the activities of account "{account}"')
def step(context, account):
    account = substitute_inline_codes(account)
    Activity.select_synced_filter(account)


@Then('the following activities should be displayed in synced table')
def step(context):
    _check_activities(context)


@Then('the following activities should be displayed in not synced table')
def step(context):
    _check_activities(context, not_synced=True)


@Then('the following activities should not be displayed in synced table')
def step(context):
    _check_activities(context, should_exist=False)


@Then('the following activities should not be displayed in not synced table')
def step(context):
    _check_activities(context, not_synced=True, should_exist=False)


@When('the user unchecks the "{filter_option}" filter')
def step(context, filter_option):
    Activity.select_not_synced_filter(filter_option)


@Then('the following error message should appear in the client')
def step(context):
    expected_error_message = context.text

    actual_error_message = SyncConnection.get_permission_error_message()

    # wait for error message to disappear
    SyncConnection.wait_for_error_label(False)

    with ensure(
        f'Expected error message: "{expected_error_message}" but got: "{actual_error_message}"'
    ):
        expected_error_message.should.equal(actual_error_message)


@Given('the user has waited for "{wait_for}" seconds')
def step(context, wait_for):
    time.sleep(float(wait_for))


@When('the user unselects the following folders to sync in "Choose what to sync" window:')
def step(context):
    SyncConnection.choose_what_to_sync()
    folders = []
    for row in context.table:
        folders.append(row[0])
    SyncConnectionWizard.unselect_folders_to_sync(folders, new_sync_connection_wizard=False)
