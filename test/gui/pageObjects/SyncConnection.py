from types import SimpleNamespace
from appium.webdriver.common.appiumby import AppiumBy as By
from selenium.webdriver.common.keys import Keys

from helpers.ConfigHelper import get_config
from helpers.SetupClientHelper import app


class SyncConnection:
    ACCOUNT_CONNECTION_CONTAINER = SimpleNamespace(
        by=By.NAME, selector="Sync connections"
    )
    FOLDER_SYNC_CONNECTION_MENU_BUTTON = SimpleNamespace(
        by=By.NAME,
        selector="{sync_folder},Success,Local folder: {sync_path}{sync_folder}",
    )
    MENU_ITEM = SimpleNamespace(by=By.NAME, selector=None)
    SELECTIVE_SYNC_APPLY_BUTTON = SimpleNamespace(by=None, selector=None)
    CANCEL_FOLDER_SYNC_CONNECTION_DIALOG = SimpleNamespace(by=None, selector=None)
    CONFIRM_FOLDER_SYNC_CONNECTION_REMOVE = SimpleNamespace(
        by=By.NAME, selector="Remove Space"
    )
    PERMISSION_ERROR_LABEL = SimpleNamespace(by=None, selector=None)

    @staticmethod
    def get_current_account_connection():
        connections = app().find_elements(
            SyncConnection.ACCOUNT_CONNECTION_CONTAINER.by,
            SyncConnection.ACCOUNT_CONNECTION_CONTAINER.selector,
        )
        for connection in connections:
            # use the active connection
            if connection.get_attribute("showing") == "true":
                return connection
        return None

    @staticmethod
    def open_menu(sync_folder=None):
        if sync_folder is None:
            sync_folder = get_config('syncConnectionName')

        connection = SyncConnection.get_current_account_connection()
        menu_button = connection.find_element(
            SyncConnection.FOLDER_SYNC_CONNECTION_MENU_BUTTON.by,
            SyncConnection.FOLDER_SYNC_CONNECTION_MENU_BUTTON.selector.format(
                sync_folder=sync_folder,
                sync_path=get_config('currentUserSyncPath'),
            ),
        )
        # Cannot select sync folder menu button.
        # This is a messy workaround to open the context menu using keyboard navigation.
        # Ideally, we should be able to do: click() and send_keys(" ") to open the menu
        # but it doesn't work for some reason.
        # Also, send_keys(Keys.SPACE) doesn't work.
        menu_button.click()
        menu_button.send_keys(Keys.TAB)
        menu_button.send_keys(Keys.TAB)
        menu_button.send_keys(Keys.TAB)
        menu_button.send_keys(Keys.TAB)
        menu_button.send_keys(Keys.TAB)
        menu_button.send_keys(Keys.TAB)
        menu_button.send_keys(" ")

    @staticmethod
    def perform_action(action):
        SyncConnection.open_menu()
        app().find_element(SyncConnection.MENU_ITEM.by, action).click()

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
        return squish.waitForObjectItem(SyncConnection.MENU_ITEM, item)

    @staticmethod
    def menu_item_exists(menu_item):
        obj = SyncConnection.MENU_ITEM.copy()
        obj.update({"type": "QAction", "text": menu_item})
        return object.exists(obj)

    @staticmethod
    def choose_what_to_sync():
        SyncConnection.open_menu()
        SyncConnection.perform_action("Choose what to sync")

    @staticmethod
    def has_sync_connection(sync_folder):
        connection = SyncConnection.get_current_account_connection()
        try:
            connection.find_element(
                SyncConnection.FOLDER_SYNC_CONNECTION_MENU_BUTTON.by,
                SyncConnection.FOLDER_SYNC_CONNECTION_MENU_BUTTON.selector.format(
                    sync_folder=sync_folder,
                    sync_path=get_config('currentUserSyncPath'),
                ),
            )
            return True
        except:
            return False

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
        app().find_element(
            SyncConnection.CONFIRM_FOLDER_SYNC_CONNECTION_REMOVE.by,
            SyncConnection.CONFIRM_FOLDER_SYNC_CONNECTION_REMOVE.selector,
        ).click()

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
