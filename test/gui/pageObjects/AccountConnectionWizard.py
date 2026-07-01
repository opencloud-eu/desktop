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
from helpers.AppHelper import app


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
    SELECT_LOCAL_FOLDER_BUTTON = SimpleNamespace(
        by=By.ACCESSIBILITY_ID,
        selector="QApplication.Settings.centralwidget.dialogStack.SetupWizardWidget.contentWidget.AccountConfiguredWizardPage.advancedConfigGroupBox.advancedConfigGroupBoxContentWidget.localDirectoryGroupBox.chooseLocalDirectoryButton"
    )
    LOCAL_DOWNLOAD_DIRECTORY_INPUT = SimpleNamespace(
        by=By.ACCESSIBILITY_ID,
        selector="QApplication.Settings.centralwidget.dialogStack.SetupWizardWidget.contentWidget.AccountConfiguredWizardPage.advancedConfigGroupBox.advancedConfigGroupBoxContentWidget.localDirectoryGroupBox.localDirectoryLineEdit"
    )
    DIRECTORY_NAME_BOX = SimpleNamespace(
        by=By.ACCESSIBILITY_ID,
        selector="QApplication.Settings.centralwidget.dialogStack.SetupWizardWidget.contentWidget.AccountConfiguredWizardPage.advancedConfigGroupBox.advancedConfigGroupBoxContentWidget.localDirectoryGroupBox.chooseLocalDirectoryButton",
    )
    CHOOSE_FOLDER_BUTTON = SimpleNamespace(by=By.NAME, selector="Choose")
    LOGIN_DIALOG = SimpleNamespace(by=By.NAME, selector="Log in with your web browser")
    COPY_URL_TO_CLIPBOARD_BUTTON = SimpleNamespace(
        by=By.NAME,
        selector="Copy URL",
    )
    CONF_SYNC_MANUALLY_RADIO_BUTTON = SimpleNamespace(
        by=By.NAME, selector="Configure synchronization manually"
    )
    ADVANCED_CONFIGURATION_CHECKBOX = SimpleNamespace(
        by=By.NAME,
        selector="Advanced configuration",
    )
    DIRECTORY_NAME_EDIT_BOX = SimpleNamespace(
        by=By.ACCESSIBILITY_ID,
        selector="QApplication.QFileDialog.fileNameEdit",
    )
    SYNC_EVERYTHING_RADIO_BUTTON = SimpleNamespace(by=By.NAME, selector="Synchronize all existing spaces")

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
        buttons = app().find_elements(
            AccountConnectionWizard.ACCEPT_CERTIFICATE_YES.by,
            AccountConnectionWizard.ACCEPT_CERTIFICATE_YES.selector,
        )
        # click the last button
        last_button = buttons.pop()
        last_button.click()

    @staticmethod
    def add_user_credentials(username, password):
        AccountConnectionWizard.oidc_login(username, password)

    @staticmethod
    def oidc_login(username, password):
        AccountConnectionWizard.browser_login(username, password)

    @staticmethod
    def copy_login_url():
        app().find_element(
            AccountConnectionWizard.COPY_URL_TO_CLIPBOARD_BUTTON.by,
            AccountConnectionWizard.COPY_URL_TO_CLIPBOARD_BUTTON.selector,
        ).click()

    @staticmethod
    def browser_login(username, password):
        AccountConnectionWizard.copy_login_url()
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
        app().find_element(
            AccountConnectionWizard.CONF_SYNC_MANUALLY_RADIO_BUTTON.by,
            AccountConnectionWizard.CONF_SYNC_MANUALLY_RADIO_BUTTON.selector,
        ).click()

    @staticmethod
    def select_download_everything_option():
        app().find_element(
            AccountConnectionWizard.SYNC_EVERYTHING_RADIO_BUTTON.by,
            AccountConnectionWizard.SYNC_EVERYTHING_RADIO_BUTTON.selector
        ).click()

    @staticmethod
    def is_credential_window_visible():
        visible = app().find_element(
            AccountConnectionWizard.LOGIN_DIALOG.by,
            AccountConnectionWizard.LOGIN_DIALOG.selector
        ).is_displayed()
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
            app().find_element(
            AccountConnectionWizard.SELECT_LOCAL_FOLDER_BUTTON.by,
            AccountConnectionWizard.SELECT_LOCAL_FOLDER_BUTTON.selector
            ).click()
            app().find_element(
                AccountConnectionWizard.DIRECTORY_NAME_BOX.by,
                AccountConnectionWizard.DIRECTORY_NAME_BOX.selector,
            )
            app().find_element(
                AccountConnectionWizard.CHOOSE_FOLDER_BUTTON.by,
                AccountConnectionWizard.CHOOSE_FOLDER_BUTTON.selector
            )
            can_change = True
        except:
            pass
        return can_change

    @staticmethod
    def is_sync_everything_option_checked():
        element = app().find_element(
            AccountConnectionWizard.SYNC_EVERYTHING_RADIO_BUTTON.by,
            AccountConnectionWizard.SYNC_EVERYTHING_RADIO_BUTTON.selector
        )
        return element.get_attribute("checked") == "true"

    @staticmethod
    def get_local_sync_path():
        element = app().find_element(
            AccountConnectionWizard.LOCAL_DOWNLOAD_DIRECTORY_INPUT.by,
            AccountConnectionWizard.LOCAL_DOWNLOAD_DIRECTORY_INPUT.selector
        )
        return str(element.text)
