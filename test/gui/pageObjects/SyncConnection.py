from types import SimpleNamespace
from appium.webdriver.common.appiumby import AppiumBy as By
from selenium.common.exceptions import NoSuchElementException, WebDriverException

from helpers.ConfigHelper import get_config
from helpers.AppHelper import app
from helpers.Utils import wait_for


class SyncConnection:
    ACCOUNT_CONNECTION_CONTAINER = SimpleNamespace(by=By.NAME, selector="Sync connections")
    FOLDER_SYNC_CONNECTION_MENU_BUTTON = SimpleNamespace(
        by=By.NAME,
        selector="{sync_folder},{status},Local folder: {sync_path}{sync_folder}",
    )
    MENU_ITEM = SimpleNamespace(by=By.NAME, selector=None)
    CONFIRM_FOLDER_SYNC_CONNECTION_REMOVE = SimpleNamespace(by=By.NAME, selector="Remove Space")
    PERMISSION_ERROR_LABEL = SimpleNamespace(
        by=By.XPATH, selector="//label[contains(@name, 'permission')]"
    )

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
    def open_menu(sync_folder=None, sync_state="success"):
        if sync_folder is None:
            sync_folder = get_config('syncConnectionName')

        if sync_state == "success":
            status = "Success"
        elif sync_state == "paused":
            status = "Sync paused"
        elif sync_state == "queued":
            status = "Queued"
        else:
            raise ValueError(f"Unknown sync_state: {sync_state}")

        connection = SyncConnection.get_current_account_connection()
        menu_button = connection.find_element(
            SyncConnection.FOLDER_SYNC_CONNECTION_MENU_BUTTON.by,
            SyncConnection.FOLDER_SYNC_CONNECTION_MENU_BUTTON.selector.format(
                sync_folder=sync_folder,
                sync_path=get_config("currentUserSyncPath").rstrip("/") + "/",
                status=status,
            ),
        )
        menu_button.native_click(button='right')

    @staticmethod
    def perform_action(action, sync_state="success"):
        SyncConnection.open_menu(sync_state=sync_state)
        app().find_element(SyncConnection.MENU_ITEM.by, action).click()

    @staticmethod
    def force_sync():
        SyncConnection.perform_action("Force sync now")

    @staticmethod
    def pause_sync():
        SyncConnection.perform_action("Pause sync")

    @staticmethod
    def resume_sync():
        SyncConnection.perform_action("Resume sync", "paused")

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
                    status="success",
                ),
                timeout=get_config("lowest_timeout"),
            )
            return True
        except NoSuchElementException:
            return False

    @staticmethod
    def is_sync_in_progress(sync_folder):
        connection = SyncConnection.get_current_account_connection()
        try:
            connection.find_element(
                By.NAME,
                "{sync_folder},Queued,Local folder: {sync_path}{sync_folder}".format(
                    sync_folder=sync_folder,
                    sync_path=get_config('currentUserSyncPath'),
                ),
                timeout=get_config("lowest_timeout"),
            )
            return True
        except NoSuchElementException:
            return False
        except WebDriverException as e:
            if "NoneType" in str(e):
                return False

    @staticmethod
    def remove_folder_sync_connection():
        SyncConnection.perform_action("Remove Space")

    @staticmethod
    def confirm_folder_sync_connection_removal():
        app().find_element(
            SyncConnection.CONFIRM_FOLDER_SYNC_CONNECTION_REMOVE.by,
            SyncConnection.CONFIRM_FOLDER_SYNC_CONNECTION_REMOVE.selector,
        ).click()

    @staticmethod
    def wait_for_error_label(to_exist=True):
        """Wait for permission error label to appear or disappear"""

        def check_label():
            try:
                app().find_element(
                    SyncConnection.PERMISSION_ERROR_LABEL.by,
                    SyncConnection.PERMISSION_ERROR_LABEL.selector,
                    timeout=get_config("lowest_timeout"),
                )
                return True
            except (NoSuchElementException, WebDriverException):
                return False

        status = wait_for(
            lambda: check_label() == to_exist,
            get_config("max_timeout"),
        )
        if not status:
            action = "appear" if to_exist else "disappear"
            raise AssertionError(f"Permission error label did not {action}")

    @staticmethod
    def get_permission_error_message():
        """Get the permission error message text"""
        SyncConnection.wait_for_error_label(True)  # Wait for label to appear
        element = app().find_element(
            SyncConnection.PERMISSION_ERROR_LABEL.by,
            SyncConnection.PERMISSION_ERROR_LABEL.selector,
        )
        return str(element.text)
