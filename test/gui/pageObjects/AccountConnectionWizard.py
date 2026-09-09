import os
import time
import pyperclip
import pyautogui
from types import SimpleNamespace
from appium.webdriver.common.appiumby import AppiumBy as By
from selenium.common.exceptions import WebDriverException

from helpers.WebUIHelper import authorize_via_webui
from helpers.ConfigHelper import get_config, is_windows
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
        selector="QApplication.Settings.centralwidget.dialogStack.SetupWizardWidget.contentWidget.AccountConfiguredWizardPage.advancedConfigGroupBox.advancedConfigGroupBoxContentWidget.localDirectoryGroupBox.chooseLocalDirectoryButton",
    )
    LOCAL_DOWNLOAD_DIRECTORY_INPUT = SimpleNamespace(
        by=By.ACCESSIBILITY_ID,
        selector="QApplication.Settings.centralwidget.dialogStack.SetupWizardWidget.contentWidget.AccountConfiguredWizardPage.advancedConfigGroupBox.advancedConfigGroupBoxContentWidget.localDirectoryGroupBox.localDirectoryLineEdit",
    )
    DIRECTORY_NAME_BOX = SimpleNamespace(
        by=By.ACCESSIBILITY_ID,
        selector="QApplication.Settings.centralwidget.dialogStack.SetupWizardWidget.contentWidget.AccountConfiguredWizardPage.advancedConfigGroupBox.advancedConfigGroupBoxContentWidget.localDirectoryGroupBox.chooseLocalDirectoryButton",
    )
    CHOOSE_FOLDER_BUTTON = SimpleNamespace(by=By.NAME, selector="Choose")
    SELECT_FOLDER_BUTTON = SimpleNamespace(by=By.NAME, selector="Select Folder")
    WINDOWS_LOGIN_DIALOG = SimpleNamespace(
        by=By.XPATH, selector="//*[contains(@Name, 'Log in with your web browser')]"
    )
    LOGIN_DIALOG = SimpleNamespace(by=By.NAME, selector="Log in with your web browser")
    COPY_URL_TO_CLIPBOARD_BUTTON_WINDOW = SimpleNamespace(
        by=By.XPATH,
        selector="//*[contains(@Name, 'Copy URL')]",
    )
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
    WINDOWS_DIRECTORY_NAME_EDIT_BOX = SimpleNamespace(
        by=By.CLASS_NAME,
        selector="Edit",
    )
    SYNC_EVERYTHING_RADIO_BUTTON = SimpleNamespace(
        by=By.NAME, selector="Synchronize all existing spaces"
    )

    @staticmethod
    def add_server(server_url):
        url_input = app().find_element(
            AccountConnectionWizard.SERVER_ADDRESS_BOX.by,
            AccountConnectionWizard.SERVER_ADDRESS_BOX.selector,
        )
        url_input.clear()
        url_input.send_keys(server_url)

        AccountConnectionWizard.next_step()

    @staticmethod
    def accept_certificate():
        buttons = app().find_elements(
            AccountConnectionWizard.ACCEPT_CERTIFICATE_YES.by,
            AccountConnectionWizard.ACCEPT_CERTIFICATE_YES.selector,
        )
        if is_windows():
            pyautogui.hotkey("alt", "y")
        else:
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
        if is_windows():
            app().find_element(
                AccountConnectionWizard.COPY_URL_TO_CLIPBOARD_BUTTON_WINDOW.by,
                AccountConnectionWizard.COPY_URL_TO_CLIPBOARD_BUTTON_WINDOW.selector,
            ).click()
        else:
            app().find_element(
                AccountConnectionWizard.COPY_URL_TO_CLIPBOARD_BUTTON.by,
                AccountConnectionWizard.COPY_URL_TO_CLIPBOARD_BUTTON.selector,
            ).click()

    @staticmethod
    def get_login_url():
        login_url = ""
        for attempt in range(2):
            try:
                AccountConnectionWizard.copy_login_url()
                login_url = pyperclip.paste() if is_windows() else app().get_clipboard_text()

                if login_url.startswith("https://"):
                    return login_url
            except WebDriverException:
                pass

            if attempt == 0:
                time.sleep(0.5)

        raise WebDriverException(f"Failed to get a valid login URL from clipboard: {login_url!r}")

    @staticmethod
    def browser_login(username, password):
        login_url = AccountConnectionWizard.get_login_url()
        authorize_via_webui(username, password, login_url)

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

        app().find_element(
            AccountConnectionWizard.DIRECTORY_NAME_BOX.by,
            AccountConnectionWizard.DIRECTORY_NAME_BOX.selector,
        ).click()
        if is_windows():
            dir_location_input = app().find_element(
                AccountConnectionWizard.WINDOWS_DIRECTORY_NAME_EDIT_BOX.by,
                AccountConnectionWizard.WINDOWS_DIRECTORY_NAME_EDIT_BOX.selector,
            )
        else:
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
            AccountConnectionWizard.select_advanced_config()
            sync_path = AccountConnectionWizard.select_sync_folder(account_details["user"])
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
            AccountConnectionWizard.SYNC_EVERYTHING_RADIO_BUTTON.selector,
        ).click()

    @staticmethod
    def is_credential_window_visible():
        locator = (
            AccountConnectionWizard.WINDOWS_LOGIN_DIALOG
            if is_windows()
            else AccountConnectionWizard.LOGIN_DIALOG
        )

        return (
            app()
            .find_element(
                locator.by,
                locator.selector,
            )
            .is_displayed()
        )

    @staticmethod
    def select_advanced_config():
        element = app().find_element(
            AccountConnectionWizard.ADVANCED_CONFIGURATION_CHECKBOX.by,
            AccountConnectionWizard.ADVANCED_CONFIGURATION_CHECKBOX.selector,
        )
        if is_windows():
            element.native_toggle()
        else:
            element.click()

    @staticmethod
    def can_change_local_sync_dir():
        can_change = False
        try:
            select_local_folder = app().find_element(
                AccountConnectionWizard.SELECT_LOCAL_FOLDER_BUTTON.by,
                AccountConnectionWizard.SELECT_LOCAL_FOLDER_BUTTON.selector,
            )
            if is_windows():
                app().execute_script("windows: invoke", select_local_folder)
                time.sleep(5)
            else:
                select_local_folder.click()
            app().find_element(
                AccountConnectionWizard.DIRECTORY_NAME_BOX.by,
                AccountConnectionWizard.DIRECTORY_NAME_BOX.selector,
            )
            if is_windows():
                app().find_element(
                    AccountConnectionWizard.SELECT_FOLDER_BUTTON.by,
                    AccountConnectionWizard.SELECT_FOLDER_BUTTON.selector,
                )
            else:
                app().find_element(
                    AccountConnectionWizard.CHOOSE_FOLDER_BUTTON.by,
                    AccountConnectionWizard.CHOOSE_FOLDER_BUTTON.selector,
                )
            can_change = True
        except Exception:
            pass
        return can_change

    @staticmethod
    def is_sync_everything_option_checked():
        element = app().find_element(
            AccountConnectionWizard.SYNC_EVERYTHING_RADIO_BUTTON.by,
            AccountConnectionWizard.SYNC_EVERYTHING_RADIO_BUTTON.selector,
        )
        if is_windows():
            return element.is_selected()
        return element.get_attribute("checked") == "true"

    @staticmethod
    def get_local_sync_path():
        element = app().find_element(
            AccountConnectionWizard.LOCAL_DOWNLOAD_DIRECTORY_INPUT.by,
            AccountConnectionWizard.LOCAL_DOWNLOAD_DIRECTORY_INPUT.selector,
        )
        return str(element.text)
