from types import SimpleNamespace
from appium.webdriver.common.appiumby import AppiumBy as By
from selenium.webdriver.common.keys import Keys

from helpers.SetupClientHelper import get_current_user_sync_path
from helpers.AppHelper import app
from helpers.ConfigHelper import get_config


class SyncConnectionWizard:
    CHOOSE_LOCAL_SYNC_FOLDER = SimpleNamespace(
        by=By.ACCESSIBILITY_ID, selector="localFolderLineEdit"
    )
    BACK_BUTTON = SimpleNamespace(by=By.NAME, selector="< Back")
    NEXT_BUTTON = SimpleNamespace(by=By.NAME, selector="Next >")
    SELECTIVE_SYNC_ROOT_FOLDER = SimpleNamespace(
        by=By.NAME,
        selector=None
    )
    SELECTIVE_SYNC_TREE_FOLDER = SimpleNamespace(
        by=By.XPATH,
        selector="//table_cell[@name and contains(@states, 'checkable') and @name!='{space}']"
    )
    ADD_SYNC_CONNECTION_BUTTON = SimpleNamespace(
        by=By.XPATH, selector="//dialog[@name='Add Space']//*[@name='Add Space']"
    )
    SELECTIVE_SYNC_TREE_HEADER = SimpleNamespace(by=By.NAME, selector='{header}')
    CANCEL_FOLDER_SYNC_CONNECTION_WIZARD = SimpleNamespace(
        by=By.NAME, selector="Cancel"
    )
    SPACES_LIST = SimpleNamespace(by=By.NAME, selector="Spaces list")
    SPACE_NAME_SELECTOR = SimpleNamespace(by=By.NAME, selector="{space_name},")
    ADD_SPACE_BUTTON = SimpleNamespace(by=By.NAME, selector='Add Space')

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
        app().find_element(
            SyncConnectionWizard.BACK_BUTTON.by,
            SyncConnectionWizard.BACK_BUTTON.selector
        ).click()

    @staticmethod
    def deselect_all_remote_folders():
        root = app().find_element(
            SyncConnectionWizard.SELECTIVE_SYNC_ROOT_FOLDER.by,
            get_config('syncConnectionName'),
        )
        root.native_click()
        root.native_send_keys(Keys.SPACE)  # uncheck the root folder

    @staticmethod
    def sort_by(header_text):
        element = app().find_element(
            SyncConnectionWizard.SELECTIVE_SYNC_TREE_HEADER.by,
            SyncConnectionWizard.SELECTIVE_SYNC_TREE_HEADER.selector.format(header=header_text)
        )
        # ISSUE: https://github.com/opencloud-eu/desktop/pull/879
        # Cannot select table header element by click event
        # Select the table header element using keyboard events as a workaround
        # TODO: Remove the workaround and uncomment 'click' action
        # element.click()
        element.native_click()

    @staticmethod
    def add_sync_connection():
        app().find_element(
            SyncConnectionWizard.ADD_SYNC_CONNECTION_BUTTON.by,
            SyncConnectionWizard.ADD_SYNC_CONNECTION_BUTTON.selector,
        ).click()

    @staticmethod
    def get_item_name_from_row(row_index):
        elements = app().find_elements(
            SyncConnectionWizard.SELECTIVE_SYNC_TREE_FOLDER.by,
            SyncConnectionWizard.SELECTIVE_SYNC_TREE_FOLDER.selector.format(space=get_config("syncConnectionName"))
        )
        return str(elements[row_index].text)


    @staticmethod
    def is_root_folder_checked():
        element = app().find_element(
            SyncConnectionWizard.SELECTIVE_SYNC_ROOT_FOLDER.by,
            get_config("syncConnectionName")
        )
        return element.get_attribute("checked") == "true"
        
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
    def get_local_sync_path():
        element = app().find_element(
            SyncConnectionWizard.CHOOSE_LOCAL_SYNC_FOLDER.by,
            SyncConnectionWizard.CHOOSE_LOCAL_SYNC_FOLDER.selector
        )
        return str(element.text)

    @staticmethod
    def is_add_space_button_enabled():
        element = app().find_element(
            SyncConnectionWizard.ADD_SPACE_BUTTON.by,
            SyncConnectionWizard.ADD_SPACE_BUTTON.selector
        )
        return element.is_enabled()

    @staticmethod
    def get_relative_folder_element(target_folder, parent_row):
        possible_els = app().find_elements(By.NAME, target_folder)
        for folder in possible_els:
            if folder.rect["x"] > parent_row:
                return folder

    @staticmethod
    def toggle_folder_selection(folders, select=True):
        expected_state = "true" if select else "false"

        for folder_path in folders:
            parents = folder_path.strip("/").split("/")
            target_folder = parents.pop()

            parent_element = None
            parent_position = 0
            target_element = None
            for idx, parent in enumerate(parents):
                p_elements = app().find_elements(By.NAME, parent)
                next_item = idx + 1 < len(parents) and parents[idx + 1] or target_folder

                # select nested folders based on the position of the parent folder
                for p_element in p_elements:
                    if p_element.rect["x"] >= parent_position and (
                        select or p_element.get_attribute("checked") == 'true'
                    ):
                        parent_element = p_element
                        parent_position = p_element.rect["x"]
                        break

                parent_element.native_double_click()  # expand the folder
                target_element = SyncConnectionWizard.get_relative_folder_element(
                    next_item, parent_position
                )

                # retry once if the folder is not expanded
                if not target_element or not target_element.is_displayed():
                    print('[WARN] Folder was not expanded, retrying with arrow key')
                    # expand using arrow key
                    parent_element.native_click()
                    parent_element.native_send_keys(Keys.ARROW_RIGHT)
                    # try to get the next target again
                    target_element = SyncConnectionWizard.get_relative_folder_element(
                        next_item, parent_position
                    )
                if not target_element or not target_element.is_displayed():
                    raise AssertionError(f'Failed to expand folder: {parent}')

            if not target_element:
                target_element = SyncConnectionWizard.get_relative_folder_element(
                    target_folder, parent_position
                )
            is_checked = target_element.get_attribute("checked")
            # return early if the folder is already in the expected state.
            if is_checked == expected_state:
                return

            target_element.native_click()
            if not target_element.is_selected():
                raise AssertionError(f"Failed to focus folder: {target_folder}")
            target_element.native_send_keys(Keys.SPACE)  # toggle the folder selection

            is_checked = target_element.get_attribute("checked")
            if is_checked != expected_state:
                raise AssertionError(
                    f"Failed to {'select' if select else 'unselect'} folder: {folder_path}"
                )

    @staticmethod
    def confirm_choose_what_to_sync_selection():
        app().find_element(By.NAME, "OK").click()

    @staticmethod
    def __handle_folder_selection(folders, should_select, new_sync_connection_wizard):
        SyncConnectionWizard.toggle_folder_selection(folders, should_select)

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
