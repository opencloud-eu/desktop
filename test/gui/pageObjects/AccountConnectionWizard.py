import os
from types import SimpleNamespace
from appium.webdriver.common.appiumby import AppiumBy as By

from helpers.WebUIHelper import authorize_via_webui
from helpers.ConfigHelper import get_config
from helpers.SetupClientHelper import (
    create_user_sync_path,
    get_temp_resource_path,
    set_current_user_sync_path,
)
from helpers.SyncHelper import listen_sync_status_for_item
from helpers.SetupClientHelper import app


class AccountConnectionWizard:
    SERVER_ADDRESS_BOX = SimpleNamespace(
        by=By.ACCESSIBILITY_ID,
        selector="QApplication.Settings.centralwidget.dialogStack.SetupWizardWidget.contentWidget.ServerUrlSetupWizardPage.urlLineEdit",
    )
    NEXT_BUTTON = SimpleNamespace(
        by=By.ACCESSIBILITY_ID,
        selector="QApplication.Settings.centralwidget.dialogStack.SetupWizardWidget.nextButton",
    )
    ACCEPT_CERTIFICATE_YES = SimpleNamespace(
        by=By.NAME,
        selector="Yes",
    )
    SELECT_LOCAL_FOLDER = SimpleNamespace(by=None, selector=None)
    DIRECTORY_NAME_BOX = SimpleNamespace(
        by=By.ACCESSIBILITY_ID,
        selector="QApplication.Settings.centralwidget.dialogStack.SetupWizardWidget.contentWidget.AccountConfiguredWizardPage.advancedConfigGroupBox.advancedConfigGroupBoxContentWidget.localDirectoryGroupBox.chooseLocalDirectoryButton",
    )
    CHOOSE_FOLDER_BUTTON = SimpleNamespace(by=By.NAME, selector="Choose")
    OAUTH_CREDENTIAL_PAGE = SimpleNamespace(by=None, selector=None)
    COPY_URL_TO_CLIPBOARD_BUTTON = SimpleNamespace(
        by=By.NAME,
        selector="Copy URL",
    )
    CONF_SYNC_MANUALLY_RADIO_BUTTON = SimpleNamespace(by=None, selector=None)
    ADVANCED_CONFIGURATION_CHECKBOX = SimpleNamespace(
        by=By.NAME,
        selector="Advanced configuration",
    )
    DIRECTORY_NAME_EDIT_BOX = SimpleNamespace(
        by=By.ACCESSIBILITY_ID,
        selector="QApplication.QFileDialog.fileNameEdit",
    )
    SYNC_EVERYTHING_RADIO_BUTTON = SimpleNamespace(by=None, selector=None)

    @staticmethod
    def add_server(server_url):
        url_input = app().find_element(
            AccountConnectionWizard.SERVER_ADDRESS_BOX.by,
            AccountConnectionWizard.SERVER_ADDRESS_BOX.selector,
        )
        url_input.clear()
        url_input.send_keys(get_config("localBackendUrl"))

        AccountConnectionWizard.next_step()

    @staticmethod
    def accept_certificate():
        app().find_element(
            AccountConnectionWizard.ACCEPT_CERTIFICATE_YES.by,
            AccountConnectionWizard.ACCEPT_CERTIFICATE_YES.selector,
        ).click()

    @staticmethod
    def add_user_credentials(username, password):
        AccountConnectionWizard.oidc_login(username, password)

    @staticmethod
    def oidc_login(username, password):
        AccountConnectionWizard.browser_login(username, password)

    @staticmethod
    def browser_login(username, password):
        app().find_element(
            AccountConnectionWizard.COPY_URL_TO_CLIPBOARD_BUTTON.by,
            AccountConnectionWizard.COPY_URL_TO_CLIPBOARD_BUTTON.selector,
        ).click()
        authorize_via_webui(username, password)

    @staticmethod
    def next_step():
        app().find_element(
            AccountConnectionWizard.NEXT_BUTTON.by,
            AccountConnectionWizard.NEXT_BUTTON.selector,
        ).click()

    @staticmethod
    def select_sync_folder(user):
        # create sync folder for user
        sync_path = create_user_sync_path(user)

        AccountConnectionWizard.select_advanced_config()
        app().find_element(
            AccountConnectionWizard.DIRECTORY_NAME_BOX.by,
            AccountConnectionWizard.DIRECTORY_NAME_BOX.selector,
        ).click()
        dir_location_input = app().find_element(
            AccountConnectionWizard.DIRECTORY_NAME_EDIT_BOX.by,
            AccountConnectionWizard.DIRECTORY_NAME_EDIT_BOX.selector,
        )
        dir_location_input.clear()
        dir_location_input.send_keys(sync_path)
        app().find_element(
            AccountConnectionWizard.CHOOSE_FOLDER_BUTTON.by,
            AccountConnectionWizard.CHOOSE_FOLDER_BUTTON.selector,
        ).click()
        return os.path.join(sync_path, get_config('syncConnectionName'))

    @staticmethod
    def set_temp_folder_as_sync_folder(folder_name):
        sync_path = get_temp_resource_path(folder_name)

        # clear the current path
        squish.mouseClick(
            squish.waitForObject(AccountConnectionWizard.SELECT_LOCAL_FOLDER)
        )

        squish.waitForObject(AccountConnectionWizard.SELECT_LOCAL_FOLDER).setText("")

        squish.type(
            squish.waitForObject(AccountConnectionWizard.SELECT_LOCAL_FOLDER),
            sync_path,
        )
        set_current_user_sync_path(sync_path)
        return sync_path

    @staticmethod
    def add_account(account_details):
        AccountConnectionWizard.add_account_information(account_details)
        AccountConnectionWizard.next_step()

    @staticmethod
    def add_account_information(account_details):
        if account_details["server"]:
            AccountConnectionWizard.add_server(account_details["server"])
            AccountConnectionWizard.accept_certificate()
        if account_details["user"]:
            AccountConnectionWizard.add_user_credentials(
                account_details["user"],
                account_details["password"],
            )
        sync_path = ""
        if account_details["sync_folder"]:
            AccountConnectionWizard.select_advanced_config()
            sync_path = AccountConnectionWizard.set_temp_folder_as_sync_folder(
                account_details["sync_folder"]
            )
        elif account_details["user"]:
            sync_path = AccountConnectionWizard.select_sync_folder(
                account_details["user"]
            )
        if sync_path:
            # listen for sync status
            listen_sync_status_for_item(sync_path)

    @staticmethod
    def select_manual_sync_folder_option():
        squish.clickButton(
            squish.waitForObject(
                AccountConnectionWizard.CONF_SYNC_MANUALLY_RADIO_BUTTON
            )
        )

    @staticmethod
    def select_download_everything_option():
        squish.clickButton(
            squish.waitForObject(AccountConnectionWizard.SYNC_EVERYTHING_RADIO_BUTTON)
        )

    @staticmethod
    def is_new_connection_window_visible():
        visible = False
        try:
            squish.waitForObject(AccountConnectionWizard.SERVER_ADDRESS_BOX)
            visible = True
        except:
            pass
        return visible

    @staticmethod
    def is_credential_window_visible():
        visible = False
        try:
            squish.waitForObject(AccountConnectionWizard.OAUTH_CREDENTIAL_PAGE)
            visible = True
        except:
            pass
        return visible

    @staticmethod
    def select_advanced_config():
        app().find_element(
            AccountConnectionWizard.ADVANCED_CONFIGURATION_CHECKBOX.by,
            AccountConnectionWizard.ADVANCED_CONFIGURATION_CHECKBOX.selector,
        ).click()

    @staticmethod
    def can_change_local_sync_dir():
        can_change = False
        try:
            squish.waitForObjectExists(AccountConnectionWizard.SELECT_LOCAL_FOLDER)
            squish.clickButton(
                squish.waitForObject(AccountConnectionWizard.DIRECTORY_NAME_BOX)
            )
            squish.waitForObjectExists(AccountConnectionWizard.CHOOSE_FOLDER_BUTTON)
            can_change = True
        except:
            pass
        return can_change

    @staticmethod
    def is_sync_everything_option_checked():
        return squish.waitForObjectExists(
            AccountConnectionWizard.SYNC_EVERYTHING_RADIO_BUTTON
        ).checked

    @staticmethod
    def get_local_sync_path():
        return str(
            squish.waitForObjectExists(
                AccountConnectionWizard.SELECT_LOCAL_FOLDER
            ).displayText
        )
