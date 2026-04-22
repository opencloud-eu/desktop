from types import SimpleNamespace
from urllib.parse import urlparse
from appium.webdriver.common.appiumby import AppiumBy as By

from helpers.SetupClientHelper import wait_until_app_killed
from helpers.ConfigHelper import get_config
from helpers.SetupClientHelper import app

import time


class Toolbar:
    TOOLBAR_ROW = SimpleNamespace(by=None, selector=None)
    ACCOUNT_BUTTON = SimpleNamespace(by=By.NAME, selector=None)
    # ACCOUNT_BUTTON_LABEL = SimpleNamespace(by=By.XPATH, selector="//label[@name='BM']")
    ADD_ACCOUNT_BUTTON = SimpleNamespace(by=By.NAME, selector="Add Account")
    ACTIVITY_BUTTON = SimpleNamespace(by=By.NAME, selector="Activity")
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
    def open_account(displayname):
        account_tab = Toolbar.get_account(displayname)
        # print(f"DEBUG: Before click - checked='{account_tab.get_attribute('checked')}'")
        # print(f"DEBUG: Before click - focused='{account_tab.get_attribute('focused')}'")
        account_tab.click()
        # print(f"DEBUG: After click - checked='{account_tab.get_attribute('checked')}'")
        # print(f"DEBUG: After click - focused='{account_tab.get_attribute('focused')}'")
        # breakpoint()

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

# account = app().find_element(
#     By.XPATH,
#     f"//*[contains(@name, '{display_name}')]"
# )

    # @staticmethod
    # def get_account(display_name):
    #     server_host = urlparse(get_config('localBackendUrl')).netloc
    #     account_label = f"{display_name}@{server_host}"
    #     # account = None
    #     try:
    #         # nav_bar = app().find_element(By.NAME, "Navigation bar")
    #         # account = nav_bar.find_element(By.NAME, account_label)
    #         # account = app().find_element(
    #         #     By.XPATH,
    #         #     f'//panel[@name="Navigation bar"]//pagetab[@name="{account_label}"]'
    #         # )
    #         all_matches = app().find_elements(By.NAME, account_label)
    #         print(f"DEBUG: Looking for '{account_label}'")
    #         print(f"DEBUG: Found {len(all_matches)} elements")
    #         for i, el in enumerate(all_matches):
    #             print(f"DEBUG: [{i}] name='{el.get_attribute('name')}' "
    #                   f"role='{el.get_attribute('role')}' "
    #                   f"displayed='{el.is_displayed()}'")
    #         return all_matches[0] if all_matches else None
    #         # return account
    #     except:
    #         return None
    #     # return account

    @staticmethod
    def get_account(display_name):
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
    def account_has_focus(display_name):
        account = Toolbar.get_account(display_name)
        return account.get_attribute("checked") == "true"

    @staticmethod
    def account_exists(display_name):
        time.sleep(5)
        account = Toolbar.get_account(display_name)
        return account is not None
