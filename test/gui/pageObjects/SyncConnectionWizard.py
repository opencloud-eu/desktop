from types import SimpleNamespace
from appium.webdriver.common.appiumby import AppiumBy as By
from selenium.webdriver.common.keys import Keys
import time
import pyautogui

from helpers.SetupClientHelper import get_current_user_sync_path
from helpers.SetupClientHelper import app


class SyncConnectionWizard:
    CHOOSE_LOCAL_SYNC_FOLDER = SimpleNamespace(
        by=By.ACCESSIBILITY_ID, selector="localFolderLineEdit"
    )
    BACK_BUTTON = SimpleNamespace(by=By.NAME, selector="< Back")
    NEXT_BUTTON = SimpleNamespace(by=By.NAME, selector="Next >")
    SELECTIVE_SYNC_ROOT_FOLDER = SimpleNamespace(by=None, selector=None)
    ADD_SYNC_CONNECTION_BUTTON = SimpleNamespace(
        by=By.XPATH, selector="//dialog[@name='Add Space']//*[@name='Add Space']"
    )
    REMOTE_FOLDER_TREE = SimpleNamespace(by=None, selector=None)
    SELECTIVE_SYNC_TREE_HEADER = SimpleNamespace(by=None, selector=None)
    CANCEL_FOLDER_SYNC_CONNECTION_WIZARD = SimpleNamespace(by=By.NAME, selector="Cancel")
    SPACES_LIST = SimpleNamespace(by=By.NAME, selector="Spaces list")
    SPACE_NAME_SELECTOR = SimpleNamespace(by=By.NAME, selector="{space_name},")
    CREATE_REMOTE_FOLDER_BUTTON = SimpleNamespace(by=None, selector=None)
    CREATE_REMOTE_FOLDER_INPUT = SimpleNamespace(by=None, selector=None)
    CREATE_REMOTE_FOLDER_CONFIRM_BUTTON = SimpleNamespace(by=None, selector=None)
    REFRESH_BUTTON = SimpleNamespace(by=None, selector=None)
    REMOTE_FOLDER_SELECTION_INPUT = SimpleNamespace(by=None, selector=None)
    ADD_FOLDER_SYNC_BUTTON = SimpleNamespace(by=None, selector=None)
    WARN_LABEL = SimpleNamespace(by=None, selector=None)
    CHOOSE_WHAT_TO_SYNC_FOLDER_TREE = SimpleNamespace(by=None, selector=None)

    @staticmethod
    def set_sync_path_oc(sync_path):
        if not sync_path:
            sync_path = get_current_user_sync_path()
        sync_path_input = app().find_element(
            SyncConnectionWizard.CHOOSE_LOCAL_SYNC_FOLDER.by,
            SyncConnectionWizard.CHOOSE_LOCAL_SYNC_FOLDER.selector,
        )
        sync_path_input.clear()
        sync_path_input.send_keys(sync_path)
        SyncConnectionWizard.next_step()

    @staticmethod
    def set_sync_path(sync_path=""):
        SyncConnectionWizard.set_sync_path_oc(sync_path)

    @staticmethod
    def next_step():
        next_button = app().find_element(
            SyncConnectionWizard.NEXT_BUTTON.by,
            SyncConnectionWizard.NEXT_BUTTON.selector,
        )
        if not next_button.is_enabled():
            raise AssertionError("Next button is not enabled")
        next_button.click()

    @staticmethod
    def back():
        squish.clickButton(squish.waitForObject(SyncConnectionWizard.BACK_BUTTON))

    @staticmethod
    def select_remote_destination_folder(folder):
        squish.mouseClick(
            squish.waitForObjectItem(SyncConnectionWizard.REMOTE_FOLDER_TREE, folder)
        )
        SyncConnectionWizard.next_step()

    @staticmethod
    def deselect_all_remote_folders():
        element = app().find_element(
            By.NAME,
            "Add Space"
        )
        element.send_keys(Keys.ARROW_DOWN)
        element.send_keys(" ")

    @staticmethod
    def sort_by(header_text):
        squish.mouseClick(
            squish.waitForObject(
                {
                    "container": SyncConnectionWizard.SELECTIVE_SYNC_TREE_HEADER,
                    "text": header_text,
                    "type": "HeaderViewItem",
                    "visible": True,
                }
            )
        )

    @staticmethod
    def add_sync_connection():
        app().find_element(
            SyncConnectionWizard.ADD_SYNC_CONNECTION_BUTTON.by,
            SyncConnectionWizard.ADD_SYNC_CONNECTION_BUTTON.selector,
        ).click()

    @staticmethod
    def get_item_name_from_row(row_index):
        folder_row = {
            "row": row_index,
            "container": SyncConnectionWizard.SELECTIVE_SYNC_ROOT_FOLDER,
            "type": "QModelIndex",
        }
        return str(squish.waitForObjectExists(folder_row).displayText)

    @staticmethod
    def is_root_folder_checked():
        state = squish.waitForObject(SyncConnectionWizard.SELECTIVE_SYNC_ROOT_FOLDER)[
            "checkState"
        ]
        return state == "checked"

    @staticmethod
    def cancel_folder_sync_connection_wizard():
        app().find_element(
            SyncConnectionWizard.CANCEL_FOLDER_SYNC_CONNECTION_WIZARD.by,
            SyncConnectionWizard.CANCEL_FOLDER_SYNC_CONNECTION_WIZARD.selector,
        ).click()

    @staticmethod
    def select_space(space_name):
        spaces_list = app().find_element(
            SyncConnectionWizard.SPACES_LIST.by,
            SyncConnectionWizard.SPACES_LIST.selector,
        )
        space_item = spaces_list.find_element(
            SyncConnectionWizard.SPACE_NAME_SELECTOR.by,
            SyncConnectionWizard.SPACE_NAME_SELECTOR.selector.format(
                space_name=space_name
            ),
        )
        # ISSUE: https://github.com/opencloud-eu/desktop/pull/879
        # Cannot select space by click event
        # Select space using keyboard events as a workaround
        # TODO: Remove 'send_keys' and uncomment 'click' action
        space_item.send_keys(Keys.ARROW_DOWN)
        # space_item.click()
        if space_item.get_attribute("selected") != "true":
            raise AssertionError("Failed to select the space: " + space_name)

    @staticmethod
    def sync_space(space_name):
        SyncConnectionWizard.set_sync_path(get_current_user_sync_path())
        SyncConnectionWizard.select_space(space_name)
        SyncConnectionWizard.next_step()
        SyncConnectionWizard.add_sync_connection()

    @staticmethod
    def create_folder_in_remote_destination(folder_name):
        squish.clickButton(
            squish.waitForObject(SyncConnectionWizard.CREATE_REMOTE_FOLDER_BUTTON)
        )
        squish.type(
            squish.waitForObject(SyncConnectionWizard.CREATE_REMOTE_FOLDER_INPUT),
            folder_name,
        )
        squish.clickButton(
            squish.waitForObject(
                SyncConnectionWizard.CREATE_REMOTE_FOLDER_CONFIRM_BUTTON
            )
        )

    @staticmethod
    def refresh_remote():
        squish.clickButton(squish.waitForObject(SyncConnectionWizard.REFRESH_BUTTON))

    @staticmethod
    def is_remote_folder_selected(folder_selector):
        return squish.waitForObjectExists(folder_selector).selected

    @staticmethod
    def open_sync_connection_wizard():
        squish.mouseClick(
            squish.waitForObject(SyncConnectionWizard.ADD_FOLDER_SYNC_BUTTON)
        )

    @staticmethod
    def get_local_sync_path():
        return str(
            squish.waitForObjectExists(
                SyncConnectionWizard.CHOOSE_LOCAL_SYNC_FOLDER
            ).displayText
        )

    @staticmethod
    def get_warn_label():
        return str(squish.waitForObjectExists(SyncConnectionWizard.WARN_LABEL).text)

    @staticmethod
    def is_add_sync_folder_button_enabled():
        return squish.waitForObjectExists(
            SyncConnectionWizard.ADD_FOLDER_SYNC_BUTTON
        ).enabled

    @staticmethod
    def select_or_unselect_folders_to_sync(
            folders,
            should_select=True
    ):
        if should_select:
            SyncConnectionWizard.deselect_all_remote_folders()

        for folder in folders:
            path_parts = folder.strip("/").split("/")

            for i in range(len(path_parts) - 1):
                parent_folder_name = path_parts[i]
                parent_elements = app().find_elements(By.NAME, parent_folder_name)

                for element in parent_elements:
                    isChecked = element.get_attribute("checked")
                    if isChecked == "true":
                        parent_bounds = element.rect
                        center_x = int(parent_bounds['x'] + parent_bounds['width'] // 2)
                        center_y = int(parent_bounds['y'] + parent_bounds['height'] // 2)
                        pyautogui.doubleClick(center_x, center_y)

            target_folder_name = path_parts[-1]
            folder_element = app().find_element(By.NAME, target_folder_name)

            element_bounds = folder_element.rect
            checkbox_x_position = int(element_bounds['x'] + 10)
            checkbox_y_position = int(element_bounds['y'] + element_bounds['height'] // 2)

            pyautogui.moveTo(checkbox_x_position, checkbox_y_position, duration=0.1)
            pyautogui.click()

    @staticmethod
    def confirm_choose_what_to_sync_selection():
        app().find_element(By.NAME, "OK").click()

    @staticmethod
    def __handle_folder_selection(folders, should_select, new_sync_connection_wizard):
        SyncConnectionWizard.select_or_unselect_folders_to_sync(
            folders,
            should_select=should_select
        )

        if new_sync_connection_wizard:
            SyncConnectionWizard.add_sync_connection()
        else:
            SyncConnectionWizard.confirm_choose_what_to_sync_selection()

    @staticmethod
    def unselect_folders_to_sync(folders, new_sync_connection_wizard=False):
        SyncConnectionWizard.__handle_folder_selection(
            folders,
            should_select=False,
            new_sync_connection_wizard=new_sync_connection_wizard,
        )

    @staticmethod
    def select_folders_to_sync(folders, new_sync_connection_wizard=False):
        SyncConnectionWizard.__handle_folder_selection(
            folders,
            should_select=True,
            new_sync_connection_wizard=new_sync_connection_wizard,
        )
