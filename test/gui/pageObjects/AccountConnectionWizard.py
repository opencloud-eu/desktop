# import test
# import names

# import squish
import os

from appium.webdriver.common.appiumby import AppiumBy as By

# from pageObjects.EnterPassword import EnterPassword
from helpers.WebUIHelper import authorize_via_webui
from helpers.ConfigHelper import get_config
from helpers.SetupClientHelper import (
    create_user_sync_path,
    get_temp_resource_path,
    set_current_user_sync_path,
)
from helpers.SyncHelper import listen_sync_status_for_item
from helpers.SetupClientHelper import get_app_driver


class AccountConnectionWizard:
    # SERVER_ADDRESS_BOX = {
    #     "container": names.setupWizardWindow_contentWidget_QStackedWidget,
    #     "name": "urlLineEdit",
    #     "type": "QLineEdit",
    #     "visible": 1,
    # }
    SERVER_ADDRESS_BOX = "QApplication.Settings.centralwidget.dialogStack.SetupWizardWidget.contentWidget.ServerUrlSetupWizardPage.urlLineEdit"
    # NEXT_BUTTON = {
    #     "container": names.settings_dialogStack_QStackedWidget,
    #     "name": "nextButton",
    #     "type": "QPushButton",
    #     "visible": 1,
    # }
    NEXT_BUTTON = (
        "QApplication.Settings.centralwidget.dialogStack.SetupWizardWidget.nextButton"
    )
    # CONFIRM_INSECURE_CONNECTION_BUTTON = {
    #     "text": "Confirm",
    #     "type": "QPushButton",
    #     "unnamed": 1,
    #     "visible": 1,
    #     "window": names.insecure_connection_QMessageBox,
    # }
    # USERNAME_BOX = {
    #     "container": names.contentWidget_OCC_QmlUtils_OCQuickWidget,
    #     "id": "userNameField",
    #     "type": "TextField",
    #     "visible": True,
    # }
    # SELECT_LOCAL_FOLDER = {
    #     "container": names.advancedConfigGroupBox_localDirectoryGroupBox_QGroupBox,
    #     "name": "localDirectoryLineEdit",
    #     "type": "QLineEdit",
    #     "visible": 1,
    # }
    # DIRECTORY_NAME_BOX = {
    #     "container": names.advancedConfigGroupBox_localDirectoryGroupBox_QGroupBox,
    #     "name": "chooseLocalDirectoryButton",
    #     "type": "QToolButton",
    #     "visible": 1,
    # }
    DIRECTORY_NAME_BOX = "QApplication.Settings.centralwidget.dialogStack.SetupWizardWidget.contentWidget.AccountConfiguredWizardPage.advancedConfigGroupBox.advancedConfigGroupBoxContentWidget.localDirectoryGroupBox.chooseLocalDirectoryButton"
    # CHOOSE_BUTTON = {
    #     "text": "Choose",
    #     "type": "QPushButton",
    #     "unnamed": 1,
    #     "visible": 1,
    #     "window": names.qFileDialog_QFileDialog,
    # }
    CHOOSE_BUTTON = "Choose"
    # OAUTH_CREDENTIAL_PAGE = {
    #     "container": names.contentWidget_contentWidget_QStackedWidget,
    #     "type": "OCC::Wizard::OAuthCredentialsSetupWizardPage",
    #     "visible": 1,
    # }
    # COPY_URL_TO_CLIPBOARD_BUTTON = {
    #     "container": names.contentWidget_OCC_QmlUtils_OCQuickWidget,
    #     "id": "copyToClipboardButton",
    #     "type": "Button",
    #     "visible": True,
    # }
    # CONF_SYNC_MANUALLY_RADIO_BUTTON = {
    #     "container": names.advancedConfigGroupBox_syncModeGroupBox_QGroupBox,
    #     "name": "configureSyncManuallyRadioButton",
    #     "type": "QRadioButton",
    #     "visible": 1,
    # }
    # ADVANCED_CONFIGURATION_CHECKBOX = {
    #     "container": names.setupWizardWindow_contentWidget_QStackedWidget,
    #     "name": "advancedConfigGroupBox",
    #     "type": "QGroupBox",
    #     "visible": 1,
    # }
    # DIRECTORY_NAME_EDIT_BOX = {
    #     "buddy": names.qFileDialog_fileNameLabel_QLabel,
    #     "name": "fileNameEdit",
    #     "type": "QLineEdit",
    #     "visible": 1,
    # }
    DIRECTORY_NAME_EDIT_BOX = "QApplication.QFileDialog.fileNameEdit"
    # SYNC_EVERYTHING_RADIO_BUTTON = {
    #     "container": names.advancedConfigGroupBox_syncModeGroupBox_QGroupBox,
    #     "name": "syncEverythingRadioButton",
    #     "type": "QRadioButton",
    #     "visible": 1,
    # }

    @staticmethod
    def add_server(server_url):
        # squish.mouseClick(
        #     squish.waitForObject(AccountConnectionWizard.SERVER_ADDRESS_BOX)
        # )
        # squish.type(
        #     squish.waitForObject(AccountConnectionWizard.SERVER_ADDRESS_BOX),
        #     server_url,
        # )
        url_input = get_app_driver().find_element(
            By.ACCESSIBILITY_ID, AccountConnectionWizard.SERVER_ADDRESS_BOX
        )
        url_input.clear()
        url_input.send_keys(get_config("localBackendUrl"))

        AccountConnectionWizard.next_step()

    @staticmethod
    def accept_certificate():
        # squish.clickButton(squish.waitForObject(EnterPassword.ACCEPT_CERTIFICATE_YES))
        get_app_driver().find_element(By.NAME, "Yes").click()

    @staticmethod
    def add_user_credentials(username, password):
        AccountConnectionWizard.oidc_login(username, password)

    @staticmethod
    def oidc_login(username, password):
        AccountConnectionWizard.browser_login(username, password, "oidc")

    @staticmethod
    def browser_login(username, password, login_type=None):
        # wait 500ms for copy button to fully load
        # squish.snooze(1 / 2)
        # squish.mouseClick(
        #     squish.waitForObject(AccountConnectionWizard.COPY_URL_TO_CLIPBOARD_BUTTON)
        # )
        get_app_driver().find_element(By.NAME, "Copy URL").click()
        authorize_via_webui(username, password, login_type)

    @staticmethod
    def next_step():
        # squish.clickButton(
        #     squish.waitForObjectExists(AccountConnectionWizard.NEXT_BUTTON)
        # )
        get_app_driver().find_element(
            By.ACCESSIBILITY_ID, AccountConnectionWizard.NEXT_BUTTON
        ).click()

    @staticmethod
    def select_sync_folder(user):
        # create sync folder for user
        sync_path = create_user_sync_path(user)

        AccountConnectionWizard.select_advanced_config()
        # squish.mouseClick(
        #     squish.waitForObject(AccountConnectionWizard.DIRECTORY_NAME_BOX)
        # )
        # squish.type(
        #     squish.waitForObject(AccountConnectionWizard.DIRECTORY_NAME_EDIT_BOX),
        #     sync_path,
        # )
        # squish.clickButton(squish.waitForObject(AccountConnectionWizard.CHOOSE_BUTTON))
        get_app_driver().find_element(
            By.ACCESSIBILITY_ID, AccountConnectionWizard.DIRECTORY_NAME_BOX
        ).click()
        dir_location_input = get_app_driver().find_element(
            By.ACCESSIBILITY_ID, AccountConnectionWizard.DIRECTORY_NAME_EDIT_BOX
        )
        dir_location_input.clear()
        dir_location_input.send_keys(sync_path)
        get_app_driver().find_element(
            By.NAME, AccountConnectionWizard.CHOOSE_BUTTON
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
        # squish.waitForObject(
        #     AccountConnectionWizard.ADVANCED_CONFIGURATION_CHECKBOX
        # ).setChecked(True)
        get_app_driver().find_element(By.NAME, "Advanced configuration").click()

    @staticmethod
    def can_change_local_sync_dir():
        can_change = False
        try:
            squish.waitForObjectExists(AccountConnectionWizard.SELECT_LOCAL_FOLDER)
            squish.clickButton(
                squish.waitForObject(AccountConnectionWizard.DIRECTORY_NAME_BOX)
            )
            squish.waitForObjectExists(AccountConnectionWizard.CHOOSE_BUTTON)
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
