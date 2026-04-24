from types import SimpleNamespace
from appium.webdriver.common.appiumby import AppiumBy as By
from selenium.webdriver.common.keys import Keys

from helpers.ConfigHelper import get_config
from helpers.SetupClientHelper import app


class SyncConnection:
    ACCOUNT_CONNECTION_CONTAINER = SimpleNamespace(
        by=By.NAME, selector="Sync connections"
    )
    FOLDER_SYNC_CONNECTION_LIST = SimpleNamespace(by=None, selector=None)
    FOLDER_SYNC_CONNECTION = SimpleNamespace(by=None, selector=None)
    FOLDER_SYNC_CONNECTION_MENU_BUTTON = SimpleNamespace(
        by=By.XPATH,
        selector="//*[@name='Folder Sync']//*[contains(@name, '/{sync_folder}')]",
    )
    MENU = SimpleNamespace(by=None, selector=None)
    SELECTIVE_SYNC_APPLY_BUTTON = SimpleNamespace(by=None, selector=None)
    CANCEL_FOLDER_SYNC_CONNECTION_DIALOG = SimpleNamespace(by=None, selector=None)
    REMOVE_FOLDER_SYNC_CONNECTION_BUTTON = SimpleNamespace(by=None, selector=None)
    PERMISSION_ERROR_LABEL = SimpleNamespace(by=None, selector=None)

    @staticmethod
    def open_menu(sync_folder=None):
        if sync_folder is None:
            sync_folder = get_config('syncConnectionName')

        connections = app().find_elements(
            SyncConnection.ACCOUNT_CONNECTION_CONTAINER.by,
            SyncConnection.ACCOUNT_CONNECTION_CONTAINER.selector,
        )
        menu_button = None
        for connection in connections:
            # use the active connection
            if connection.get_attribute("showing") == "true":
                menu_button = connection.find_element(
                    SyncConnection.FOLDER_SYNC_CONNECTION_MENU_BUTTON.by,
                    SyncConnection.FOLDER_SYNC_CONNECTION_MENU_BUTTON.selector.format(
                        sync_folder=sync_folder
                    ),
                )
                print(menu_button)
                break
        # cannot click sync folder menu button
        # Workaround: use mouse right click to open the menu
        # menu_button.click()
        print(menu_button)
        menu_button.send_keys(Keys.SHIFT, Keys.F10)

    @staticmethod
    def perform_action(action):
        SyncConnection.open_menu()
        selector = SyncConnection.MENU.copy()
        selector["text"] = action
        squish.mouseClick(squish.waitForObject(selector))

    @staticmethod
    def force_sync():
        SyncConnection.perform_action("Force sync now")

    @staticmethod
    def pause_sync():
        SyncConnection.perform_action("Pause sync")

    @staticmethod
    def resume_sync():
        SyncConnection.perform_action("Resume sync")

    @staticmethod
    def has_menu_item(item):
        return squish.waitForObjectItem(SyncConnection.MENU, item)

    @staticmethod
    def menu_item_exists(menu_item):
        obj = SyncConnection.MENU.copy()
        obj.update({"type": "QAction", "text": menu_item})
        return object.exists(obj)

    @staticmethod
    def choose_what_to_sync():
        SyncConnection.open_menu()
        SyncConnection.perform_action("Choose what to sync")

    @staticmethod
    def get_folder_connection_count():
        return squish.waitForObject(SyncConnection.FOLDER_SYNC_CONNECTION_LIST).count

    @staticmethod
    def remove_folder_sync_connection():
        SyncConnection.perform_action("Remove Space")

    @staticmethod
    def cancel_folder_sync_connection_removal():
        squish.clickButton(
            squish.waitForObject(SyncConnection.CANCEL_FOLDER_SYNC_CONNECTION_DIALOG)
        )

    @staticmethod
    def confirm_folder_sync_connection_removal():
        squish.clickButton(
            squish.waitForObject(SyncConnection.REMOVE_FOLDER_SYNC_CONNECTION_BUTTON)
        )

    @staticmethod
    def wait_for_error_label(to_exist=True):
        """Wait for permission error label to appear or disappear"""
        status = squish.waitFor(
            lambda: object.exists(SyncConnection.PERMISSION_ERROR_LABEL) == to_exist,
            get_config("maxSyncTimeout") * 1000,
        )
        if not status:
            action = "appear" if to_exist else "disappear"
            raise AssertionError(f"Permission error label did not {action}")

    @staticmethod
    def get_permission_error_message():
        """Get the permission error message text"""
        SyncConnection.wait_for_error_label(True)  # Wait for label to appear
        return str(squish.waitForObject(SyncConnection.PERMISSION_ERROR_LABEL).text)
