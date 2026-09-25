import os
import time
from types import SimpleNamespace
from appium.webdriver.common.appiumby import AppiumBy as By
from selenium.common.exceptions import WebDriverException

from helpers.WebUIHelper import authorize_via_webui
from helpers.ConfigHelper import get_config, is_linux, is_windows
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
    LOCAL_SYNC_FOLDER_INPUT = SimpleNamespace(
        by=By.ACCESSIBILITY_ID,
        selector="QApplication.Settings.centralwidget.dialogStack.SetupWizardWidget.contentWidget.AccountConfiguredWizardPage.advancedConfigGroupBox.advancedConfigGroupBoxContentWidget.localDirectoryGroupBox.localDirectoryLineEdit",
    )
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
    SYNC_EVERYTHING_RADIO_BUTTON = SimpleNamespace(
        by=By.NAME, selector="Synchronize all existing spaces"
    )

    @staticmethod
    def get_native_choose_folder_button():
        if is_windows():
            return SimpleNamespace(by=By.NAME, selector="Select Folder")
        if is_linux():
            return SimpleNamespace(by=By.NAME, selector="Choose")
        raise NotImplementedError("unsupported platform.")

    @staticmethod
    def get_native_directory_path_edit():
        if is_windows():
            return SimpleNamespace(by=By.CLASS_NAME, selector="Edit")
        if is_linux():
            return SimpleNamespace(
                by=By.ACCESSIBILITY_ID,
                selector="QApplication.QFileDialog.fileNameEdit",
            )
        raise NotImplementedError("unsupported platform.")

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
        buttons = app().find_elements_with_wait(
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
        # wait for copy button to be visible and enable
        app().find_elements_with_wait(
            AccountConnectionWizard.COPY_URL_TO_CLIPBOARD_BUTTON.by,
            AccountConnectionWizard.COPY_URL_TO_CLIPBOARD_BUTTON.selector,
        )
        app().find_element(
            AccountConnectionWizard.COPY_URL_TO_CLIPBOARD_BUTTON.by,
            AccountConnectionWizard.COPY_URL_TO_CLIPBOARD_BUTTON.selector,
        ).click()

    @staticmethod
    def get_login_url():
        login_url = ""
        try:
            AccountConnectionWizard.copy_login_url()
            login_url = app().get_native_clipboard_text()
            if not login_url.startswith("https://"):
                raise WebDriverException(f"Invalid clipboard text: {login_url}")
        except WebDriverException:
            # retry once upon failure
            time.sleep(0.5)
            AccountConnectionWizard.copy_login_url()
            login_url = app().get_native_clipboard_text()
        except Exception as e:
            print(f"[Error] Failed to get login URL. Clipboard value: {login_url}")
            raise e
        return login_url

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
            AccountConnectionWizard.SELECT_LOCAL_FOLDER_BUTTON.by,
            AccountConnectionWizard.SELECT_LOCAL_FOLDER_BUTTON.selector,
        ).click()

        dir_input_selector = AccountConnectionWizard.get_native_directory_path_edit()
        dir_location_input = app().find_element(
            dir_input_selector.by,
            dir_input_selector.selector,
        )
        dir_location_input.clear()
        dir_location_input.send_keys(sync_path)

        choose_button_selector = AccountConnectionWizard.get_native_choose_folder_button()
        app().find_element(
            choose_button_selector.by,
            choose_button_selector.selector,
        ).click()
        return os.path.join(sync_path, get_config('syncConnectionName'))

    @staticmethod
    def set_temp_folder_as_sync_folder(folder_name):
        sync_path = get_temp_resource_path(folder_name)

        dir_location_input = app().find_element(
            AccountConnectionWizard.LOCAL_SYNC_FOLDER_INPUT.by,
            AccountConnectionWizard.LOCAL_SYNC_FOLDER_INPUT.selector,
        )
        dir_location_input.click()
        dir_location_input.clear()
        dir_location_input.send_keys(sync_path)

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
        return (
            app()
            .find_element(
                AccountConnectionWizard.LOGIN_DIALOG.by,
                AccountConnectionWizard.LOGIN_DIALOG.selector,
            )
            .is_displayed()
        )

    @staticmethod
    def select_advanced_config():
        element = app().find_element(
            AccountConnectionWizard.ADVANCED_CONFIGURATION_CHECKBOX.by,
            AccountConnectionWizard.ADVANCED_CONFIGURATION_CHECKBOX.selector,
        )
        element.native_checkbox_toggle()

    @staticmethod
    def can_change_local_sync_dir():
        can_change = False
        try:
            select_local_folder = app().find_element(
                AccountConnectionWizard.SELECT_LOCAL_FOLDER_BUTTON.by,
                AccountConnectionWizard.SELECT_LOCAL_FOLDER_BUTTON.selector,
            )
            select_local_folder.click()

            # wait for local directory dialog to be visible
            dir_input_selector = AccountConnectionWizard.get_native_directory_path_edit()
            dir_input = app().find_elements_with_wait(
                dir_input_selector.by,
                dir_input_selector.selector,
            )[0]
            dir_input.clear()

            choose_button_selector = AccountConnectionWizard.get_native_choose_folder_button()
            can_change = (
                app()
                .find_element(choose_button_selector.by, choose_button_selector.selector)
                .is_enabled()
            )
        except Exception as e:
            raise e
        return can_change

    @staticmethod
    def is_sync_all_spaces_option_checked():
        element = app().find_element(
            AccountConnectionWizard.SYNC_EVERYTHING_RADIO_BUTTON.by,
            AccountConnectionWizard.SYNC_EVERYTHING_RADIO_BUTTON.selector,
        )
        return element.is_checked()

    @staticmethod
    def get_local_sync_path():
        element = app().find_element(
            AccountConnectionWizard.LOCAL_SYNC_FOLDER_INPUT.by,
            AccountConnectionWizard.LOCAL_SYNC_FOLDER_INPUT.selector,
        )
        return str(element.text)
