from types import SimpleNamespace
from appium.webdriver.common.appiumby import AppiumBy as By

from pageObjects.Toolbar import Toolbar
from helpers.UserHelper import get_displayname_for_user
from helpers.SetupClientHelper import substitute_inline_codes
from helpers.AppHelper import app
from helpers.Utils import wait_for
from helpers.ConfigHelper import get_config


class AccountSetting:
    ACCOUNT_CONNECTION_CONTAINER = SimpleNamespace(
        by=By.NAME, selector="Sync connections"
    )
    MANAGE_ACCOUNT_BUTTON = SimpleNamespace(by=By.NAME, selector="Manage Account")
    ACCOUNT_MENU = SimpleNamespace(by=By.NAME, selector="{menu_item}")
    CONFIRM_REMOVE_CONNECTION_BUTTON = SimpleNamespace(
        by=By.NAME, selector="Remove connection"
    )
    ACCOUNT_CONNECTION_LABEL = SimpleNamespace(
        by=By.XPATH,
        selector="//list[@name='Folder Sync']//label",
    )

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
        labels = app().find_elements(
            AccountSetting.ACCOUNT_CONNECTION_LABEL.by,
            AccountSetting.ACCOUNT_CONNECTION_LABEL.selector,
        )
        # first label is the sync status label
        return labels[0].text

    @staticmethod
    def is_user_signed_out():
        return "Signed out" in AccountSetting.get_account_connection_label()

    @staticmethod
    def is_user_signed_in():
        return "Connected" in AccountSetting.get_account_connection_label()

    @staticmethod
    def wait_until_account_is_connected(timeout=get_config('min_timeout')):
        result = wait_for(
            AccountSetting.is_user_signed_in,
            timeout,
        )

        if not result:
            raise TimeoutError(
                "Timeout waiting for the account to be connected for "
                + str(timeout)
                + " seconds"
            )
        return result

    @staticmethod
    def remove_connection_for_user(username):
        Toolbar.open_account(username)
        AccountSetting.remove_account_connection()

    @staticmethod
    def wait_until_account_is_removed(username, timeout=get_config('min_timeout')):
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
                + " seconds"
            )
