from types import SimpleNamespace
from urllib.parse import urlparse
from appium.webdriver.common.appiumby import AppiumBy as By
from selenium.webdriver.common.keys import Keys

from helpers.SetupClientHelper import app, close_and_kill_app
from helpers.ConfigHelper import get_config
from helpers.UserHelper import get_displayname_for_user


class Toolbar:
    TOOLBAR_ROW = SimpleNamespace(by=None, selector=None)
    ACCOUNT_BUTTON = SimpleNamespace(by=None, selector=None)
    ADD_ACCOUNT_BUTTON = SimpleNamespace(by=By.NAME, selector="Add Account")
    ACTIVITY_BUTTON = SimpleNamespace(by=By.NAME, selector="Activity")
    SETTINGS_BUTTON = SimpleNamespace(by=None, selector=None)
    QUIT_BUTTON = SimpleNamespace(
        by=By.NAME,
        selector="Quit"
    )
    CONFIRM_QUIT_BUTTON = SimpleNamespace(
        by=By.ACCESSIBILITY_ID,
        selector="QApplication.QMessageBox.qt_msgbox_buttonbox.QPushButton"
    )

    TOOLBAR_ITEMS = ["Add Account", "Activity", "Settings", "Quit"]

    @staticmethod
    def get_item_selector(item_name):
        return {
            "container": names.dialogStack_quickWidget_QQuickWidget,
            "text": item_name,
            "type": "Label",
            "visible": True,
        }

    @staticmethod
    def has_item(item_name, timeout=get_config("minSyncTimeout") * 1000):
        try:
            squish.waitForObject(Toolbar.get_item_selector(item_name), timeout)
            return True
        except:
            return False

    @staticmethod
    def open_activity():
        app().find_element(
            Toolbar.ACTIVITY_BUTTON.by, Toolbar.ACTIVITY_BUTTON.selector
        ).click()

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
        account_tab.send_keys(Keys.TAB)
        account_tab.send_keys(Keys.ENTER)

    @staticmethod
    def get_displayed_account_text(displayname, host):
        return str(
            squish.waitForObjectExists(
                Toolbar.get_item_selector(displayname + "\n" + host)
            ).text
        )

    @staticmethod
    def open_settings_tab():
        squish.mouseClick(squish.waitForObject(Toolbar.SETTINGS_BUTTON))

    @staticmethod
    def quit_opencloud():
        app().find_element(
            Toolbar.QUIT_BUTTON.by,
            Toolbar.QUIT_BUTTON.selector
        ).click()
        app().find_element(
            Toolbar.CONFIRM_QUIT_BUTTON.by,
            Toolbar.CONFIRM_QUIT_BUTTON.selector
        ).click()
        close_and_kill_app()


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
                account_locator = Toolbar.ACCOUNT_BUTTON.copy()
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
            account = app().find_element(By.NAME, account_label)
        except:
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
