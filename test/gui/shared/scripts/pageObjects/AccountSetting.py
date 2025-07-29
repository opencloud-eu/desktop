import names
import squish

from helpers.UserHelper import get_displayname_for_user
from helpers.SetupClientHelper import substitute_inline_codes

from pageObjects.Toolbar import Toolbar


class AccountSetting:
    MANAGE_ACCOUNT_BUTTON = {
        "container": names.stackedWidget_quickWidget_OCC_QmlUtils_OCQuickWidget,
        "id": "manageAccountButton",
        "text": "Manage Account",
        "type": "Button",
        "visible": 1,
    }
    ACCOUNT_MENU = {
        "checkable": False,
        "container": names.quickWidget_Overlay,
        "text": "",
        "enabled": True,
        "type": "MenuItem",
        "unnamed": 1,
        "visible": True
    }
    CONFIRM_REMOVE_CONNECTION_BUTTON = {
        "container": names.settings_dialogStack_QStackedWidget,
        "text": "Remove connection",
        "type": "QPushButton",
        "unnamed": 1,
        "visible": 1,
    }
    ACCOUNT_CONNECTION_LABEL = {
        "container": names.stackedWidget_quickWidget_OCC_QmlUtils_OCQuickWidget,
        "type": "Label",
        "visible": 1
    }
    LOG_BROWSER_WINDOW = {
        "name": "OCC__LogBrowser",
        "type": "OCC::LogBrowser",
        "visible": 1,
    }
    ACCOUNT_LOADING = {
        "window": names.settings_OCC_SettingsDialog,
        "name": "loadingPage",
        "type": "QWidget",
        "visible": 0,
    }
    DIALOG_STACK = {
        "name": "dialogStack",
        "type": "QStackedWidget",
        "visible": 1,
        "window": names.settings_OCC_SettingsDialog,
    }
    CONFIRMATION_YES_BUTTON = {"text": "Yes", "type": "QPushButton", "visible": 1}

    @staticmethod
    def account_action(action):
        squish.mouseClick(squish.waitForObject(AccountSetting.MANAGE_ACCOUNT_BUTTON))
        selector = AccountSetting.ACCOUNT_MENU.copy()
        selector['text'] = action
        squish.mouseClick(
            squish.waitForObject(selector)
        )

    @staticmethod
    def remove_account_connection():
        AccountSetting.account_action("Remove")
        squish.clickButton(
            squish.waitForObject(AccountSetting.CONFIRM_REMOVE_CONNECTION_BUTTON)
        )

    @staticmethod
    def logout():
        AccountSetting.account_action("Log out")

    @staticmethod
    def login():
        AccountSetting.account_action("Log in")

    @staticmethod
    def get_account_connection_label():
        return str(
            squish.waitForObjectExists(AccountSetting.ACCOUNT_CONNECTION_LABEL).text
        )

    @staticmethod
    def is_connecting():
        return "Connecting to" in AccountSetting.get_account_connection_label()

    @staticmethod
    def is_user_signed_out():
        return "Signed out" in AccountSetting.get_account_connection_label()

    @staticmethod
    def is_user_signed_in():
        return "Connected" in AccountSetting.get_account_connection_label()

    @staticmethod
    def wait_until_connection_is_configured(timeout=5000):
        result = squish.waitFor(
            AccountSetting.is_connecting,
            timeout,
        )

        if not result:
            raise TimeoutError(
                "Timeout waiting for connection to be configured for "
                + str(timeout)
                + " milliseconds"
            )

    @staticmethod
    def wait_until_account_is_connected(timeout=5000):
        result = squish.waitFor(
            AccountSetting.is_user_signed_in,
            timeout,
        )

        if not result:
            raise TimeoutError(
                "Timeout waiting for the account to be connected for "
                + str(timeout)
                + " milliseconds"
            )
        return result

    @staticmethod
    def wait_until_sync_folder_is_configured(timeout=5000):
        result = squish.waitFor(
            lambda: not squish.waitForObjectExists(
                AccountSetting.ACCOUNT_LOADING
            ).visible,
            timeout,
        )

        if not result:
            raise TimeoutError(
                "Timeout waiting for sync folder to be connected for "
                + str(timeout)
                + " milliseconds"
            )
        return result

    @staticmethod
    def press_key(key):
        key = key.replace('"', "")
        key = f"<{key}>"
        squish.nativeType(key)

    @staticmethod
    def is_log_dialog_visible():
        visible = False
        try:
            visible = squish.waitForObjectExists(
                AccountSetting.LOG_BROWSER_WINDOW
            ).visible
        except:
            pass
        return visible

    @staticmethod
    def remove_connection_for_user(username):
        displayname = get_displayname_for_user(username)
        displayname = substitute_inline_codes(displayname)
        Toolbar.open_account(displayname)
        AccountSetting.remove_account_connection()
