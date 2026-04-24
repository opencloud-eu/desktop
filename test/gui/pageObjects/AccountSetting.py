from types import SimpleNamespace
from appium.webdriver.common.appiumby import AppiumBy as By

from pageObjects.Toolbar import Toolbar
from helpers.UserHelper import get_displayname_for_user
from helpers.SetupClientHelper import app, substitute_inline_codes
from helpers.SyncHelper import wait_for


class AccountSetting:
    ACCOUNT_CONNECTION_CONTAINER = SimpleNamespace(
        by=By.NAME, selector="Sync connections"
    )
    MANAGE_ACCOUNT_BUTTON = SimpleNamespace(by=By.NAME, selector="Manage Account")
    ACCOUNT_MENU = SimpleNamespace(by=By.NAME, selector="{menu_item}")
    CONFIRM_REMOVE_CONNECTION_BUTTON = SimpleNamespace(
        by=By.NAME, selector="Remove connection"
    )
    ACCOUNT_CONNECTION_LABEL = SimpleNamespace(by=None, selector=None)
    LOG_BROWSER_WINDOW = SimpleNamespace(by=None, selector=None)
    ACCOUNT_LOADING = SimpleNamespace(by=None, selector=None)
    DIALOG_STACK = SimpleNamespace(by=None, selector=None)
    CONFIRMATION_YES_BUTTON = SimpleNamespace(by=None, selector=None)

    @staticmethod
    def account_action(action):
        connections = app().find_elements(
            AccountSetting.ACCOUNT_CONNECTION_CONTAINER.by,
            AccountSetting.ACCOUNT_CONNECTION_CONTAINER.selector,
        )
        manage_button = None
        for connection in connections:
            # use the active connection
            if connection.get_attribute("showing") == "true":
                manage_button = connection.find_element(
                    AccountSetting.MANAGE_ACCOUNT_BUTTON.by,
                    AccountSetting.MANAGE_ACCOUNT_BUTTON.selector,
                )
                break
        manage_button.click()
        app().find_element(
            AccountSetting.ACCOUNT_MENU.by,
            AccountSetting.ACCOUNT_MENU.selector.format(menu_item=action),
        ).click()

    @staticmethod
    def remove_account_connection():
        AccountSetting.account_action("Remove")
        app().find_element(
            AccountSetting.CONFIRM_REMOVE_CONNECTION_BUTTON.by,
            AccountSetting.CONFIRM_REMOVE_CONNECTION_BUTTON.selector,
        ).click()

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
        Toolbar.open_account(username)
        AccountSetting.remove_account_connection()

    @staticmethod
    def wait_until_account_is_removed(username, timeout=10000):
        displayname = get_displayname_for_user(username)
        displayname = substitute_inline_codes(displayname)

        def account_removed():
            account = Toolbar.get_account(username)
            return account is None

        result = wait_for(account_removed, timeout)

        if not result:
            raise TimeoutError(
                "Timeout waiting for account to be removed for "
                + str(timeout)
                + " milliseconds"
            )
