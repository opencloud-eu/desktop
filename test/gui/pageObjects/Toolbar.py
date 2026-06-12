from types import SimpleNamespace
from urllib.parse import urlparse
from appium.webdriver.common.appiumby import AppiumBy as By
from selenium.common.exceptions import NoSuchElementException

from helpers.AppHelper import app
from helpers.ConfigHelper import get_config
from helpers.UserHelper import get_displayname_for_user
from helpers.Utils import wait_for


class Toolbar:
    TOOLBAR_ROW = SimpleNamespace(by=None, selector=None)
    NAVIGATION_BAR = SimpleNamespace(
        by=By.XPATH, selector="//*[@name='Navigation bar']/.."
    )
    ACCOUNT_TAB = SimpleNamespace(by=By.CLASS_NAME, selector="[page tab | {text}]")
    ADD_ACCOUNT_BUTTON = SimpleNamespace(
        by=By.CLASS_NAME, selector="[push button | Add Account]"
    )
    ACTIVITY_TAB = SimpleNamespace(by=By.CLASS_NAME, selector="[page tab | Activity]")
    SETTINGS_TAB = SimpleNamespace(by=By.CLASS_NAME, selector="[page tab | Settings]")
    QUIT_BUTTON = SimpleNamespace(by=By.CLASS_NAME, selector="[push button | Quit]")
    CONFIRM_QUIT_BUTTON = SimpleNamespace(
        by=By.NAME,
        selector="Yes",
    )

    TOOLBAR_ITEMS = ["Add Account", "Activity", "Settings", "Quit"]

    @staticmethod
    def wait_toolbar_enabled():
        toolbar = app().find_element(
            Toolbar.NAVIGATION_BAR.by, Toolbar.NAVIGATION_BAR.selector
        )
        timeout = get_config('max_timeout')
        enabled = wait_for(
            toolbar.is_enabled,
            timeout,
        )
        if not enabled:
            raise AssertionError(f"Toolbar is not enabled within {timeout} ms")

    @staticmethod
    def get_item_selector(item_name):
        return {
            "container": names.dialogStack_quickWidget_QQuickWidget,
            "text": item_name,
            "type": "Label",
            "visible": True,
        }

    @staticmethod
    def has_tab(tab_name):
        if tab_name.lower() == "add account":
            tab = Toolbar.ADD_ACCOUNT_BUTTON
        elif tab_name.lower() == "activity":
            tab = Toolbar.ACTIVITY_TAB
        elif tab_name.lower() == "settings":
            tab = Toolbar.SETTINGS_TAB
        elif tab_name.lower() == "quit":
            tab = Toolbar.QUIT_BUTTON
        else:
            raise ValueError(f"Unknown tab: {tab_name}")
        return app().find_element(tab.by, tab.selector).is_displayed()

    @staticmethod
    def open_activity():
        tab = app().find_element(Toolbar.ACTIVITY_TAB.by, Toolbar.ACTIVITY_TAB.selector)
        # ISSUE: https://github.com/opencloud-eu/desktop/pull/879
        # Cannot select navigation tab by click event
        # Select the navigation tab using keyboard events as a workaround
        # TODO: Remove the workaround and uncomment 'click' action
        # tab.click()
        tab.native_click()
        if tab.get_attribute("checked") != "true":
            raise AssertionError("Activity tab is not active")

    @staticmethod
    def open_new_account_setup():
        app().find_element(
            Toolbar.ADD_ACCOUNT_BUTTON.by,
            Toolbar.ADD_ACCOUNT_BUTTON.selector,
        ).click()

    @staticmethod
    def open_account(username):
        account_tab = Toolbar.get_account(username)
        # ISSUE: https://github.com/opencloud-eu/desktop/pull/879
        # Cannot activate account tab by click event
        # Select the account tab using keyboard events as a workaround
        # TODO: Remove the workaround and uncomment 'click' action
        # account_tab.click()
        account_tab.native_click()
        # confirm account is active
        if account_tab.get_attribute("checked") != "true":
            raise AssertionError(f"Account is not active: {username}")

    @staticmethod
    def get_displayed_account_text(displayname, host):
        return str(
            squish.waitForObjectExists(
                Toolbar.get_item_selector(displayname + "\n" + host)
            ).text
        )

    @staticmethod
    def open_settings_tab():
        tab = app().find_element(Toolbar.SETTINGS_TAB.by, Toolbar.SETTINGS_TAB.selector)
        # ISSUE: https://github.com/opencloud-eu/desktop/pull/879
        # Cannot select navigation tab by click event
        # Select the navigation tab using keyboard events as a workaround
        # TODO: Remove the workaround and uncomment 'click' action
        # tab.click()
        tab.native_click()
        if tab.get_attribute("checked") != "true":
            raise AssertionError("Settings tab is not active")

    @staticmethod
    def quit_opencloud():
        app().find_element(Toolbar.QUIT_BUTTON.by, Toolbar.QUIT_BUTTON.selector).click()
        app().find_element(
            Toolbar.CONFIRM_QUIT_BUTTON.by, Toolbar.CONFIRM_QUIT_BUTTON.selector
        ).click()

    @staticmethod
    def get_accounts():
        accounts = {}
        selectors = {}
        children_obj = object.children(squish.waitForObjectExists(Toolbar.TOOLBAR_ROW))
        account_idx = 1
        for obj in children_obj:
            if hasattr(obj, "accountState"):
                account_info = {
                    "displayname": str(obj.accountState.account.davDisplayName),
                    "hostname": str(obj.accountState.account.hostName),
                    "initials": str(obj.accountState.account.initials),
                    "current": obj.checked,
                }
                account_locator = Toolbar.ACCOUNT_TAB.copy()
                if account_idx > 1:
                    account_locator.update({"occurrence": account_idx})
                account_locator.update({"text": account_info["hostname"]})

                accounts[account_info["displayname"]] = account_info
                selectors[account_info["displayname"]] = obj
                account_idx += 1
        return accounts, selectors

    @staticmethod
    def get_account(username):
        display_name = get_displayname_for_user(username)
        server_host = urlparse(get_config('localBackendUrl')).netloc
        account_label = f"{display_name}@{server_host}"
        account = None
        try:
            account = app().find_element(
                Toolbar.ACCOUNT_TAB.by,
                Toolbar.ACCOUNT_TAB.selector.format(text=account_label),
            )
        except NoSuchElementException:
            pass
        return account

    @staticmethod
    def get_active_account():
        accounts, selectors = Toolbar.get_accounts()
        for account, info in accounts.items():
            if info["current"]:
                return info, selectors[account]
        return None, None

    @staticmethod
    def account_has_focus(username):
        account = Toolbar.get_account(username)
        return account.get_attribute("checked") == "true"

    @staticmethod
    def account_exists(username):
        account = Toolbar.get_account(username)
        return account is not None
