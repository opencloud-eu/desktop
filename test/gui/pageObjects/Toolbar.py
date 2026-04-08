from types import SimpleNamespace
from urllib.parse import urlparse
from appium.webdriver.common.appiumby import AppiumBy as By

from helpers.SetupClientHelper import wait_until_app_killed
from helpers.ConfigHelper import get_config
from helpers.SetupClientHelper import app


class Toolbar:
    TOOLBAR_ROW = SimpleNamespace(by=None, selector=None)
    ACCOUNT_BUTTON = SimpleNamespace(by=None, selector=None)
    ADD_ACCOUNT_BUTTON = SimpleNamespace(by=None, selector=None)
    ACTIVITY_BUTTON = SimpleNamespace(by=None, selector=None)
    SETTINGS_BUTTON = SimpleNamespace(by=None, selector=None)
    QUIT_BUTTON = SimpleNamespace(by=None, selector=None)
    CONFIRM_QUIT_BUTTON = SimpleNamespace(by=None, selector=None)

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
        squish.mouseClick(squish.waitForObject(Toolbar.ACTIVITY_BUTTON))

    @staticmethod
    def open_new_account_setup():
        squish.mouseClick(squish.waitForObject(Toolbar.ADD_ACCOUNT_BUTTON))

    @staticmethod
    def open_account(displayname):
        _, selector = Toolbar.get_account(displayname)
        squish.mouseClick(squish.waitForObject(selector))

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
        squish.mouseClick(squish.waitForObject(Toolbar.QUIT_BUTTON))
        squish.clickButton(squish.waitForObject(Toolbar.CONFIRM_QUIT_BUTTON))
        for ctx in squish.applicationContextList():
            pid = ctx.pid
            ctx.detach()
            wait_until_app_killed(pid)

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
    def get_account(display_name):
        accounts, selectors = Toolbar.get_accounts()
        return accounts.get(display_name), selectors.get(display_name)

    @staticmethod
    def get_active_account():
        accounts, selectors = Toolbar.get_accounts()
        for account, info in accounts.items():
            if info["current"]:
                return info, selectors[account]
        return None, None

    @staticmethod
    def account_has_focus(display_name):
        account, selector = Toolbar.get_account(display_name)
        return account["current"] and squish.waitForObject(selector).checked

    @staticmethod
    def account_exists(display_name):
        server_host = urlparse(get_config('localBackendUrl')).netloc
        account_label = f"{display_name}@{server_host}"
        app().find_element(By.NAME, account_label)
