from os import path
import names
import squish

from helpers.SetupClientHelper import (
    get_current_user_sync_path,
    set_current_user_sync_path,
)
from helpers.ConfigHelper import get_config


class SyncConnectionWizard:
    CHOOSE_LOCAL_SYNC_FOLDER = {
        "buddy": names.add_Space_label_QLabel,
        "name": "localFolderLineEdit",
        "type": "QLineEdit",
        "visible": 1
    }
    BACK_BUTTON = {
        "window": names.stackedWidget_Add_Space_QGroupBox,
        "name": "__qt__passive_wizardbutton0",
        "type": "QPushButton",
        "visible": 1,
    }
    NEXT_BUTTON = {
        "window": names.stackedWidget_Add_Space_QGroupBox,
        "name": "__qt__passive_wizardbutton1",
        "type": "QPushButton",
        "visible": 1,
    }
    SELECTIVE_SYNC_ROOT_FOLDER = {
        "column": 0,
        "container": names.add_Space_Deselect_remote_folders_you_do_not_wish_to_synchronize_QTreeWidget,
        "text": "Personal",
        "type": "QModelIndex",
    }
    ADD_SPACE_FOLDER_TREE = {
        "column": 0,
        "container": names.deselect_remote_folders_you_do_not_wish_to_synchronize_OpenCloud_QModelIndex,
        "type": "QModelIndex",
    }
    ADD_SYNC_CONNECTION_BUTTON = {
        "name": "qt_wizard_finish",
        "type": "QPushButton",
        "visible": 1,
        "window": names.stackedWidget_Add_Space_QGroupBox,
    }
    REMOTE_FOLDER_TREE = {
        "container": names.add_Folder_Sync_Connection_groupBox_QGroupBox,
        "name": "folderTreeWidget",
        "type": "QTreeWidget",
        "visible": 1,
    }
    SELECTIVE_SYNC_TREE_HEADER = {
        "container": names.add_Space_Deselect_remote_folders_you_do_not_wish_to_synchronize_QTreeWidget,
        "orientation": 1,
        "type": "QHeaderView",
        "unnamed": 1,
        "visible": 1,
    }
    CANCEL_FOLDER_SYNC_CONNECTION_WIZARD = {
        "window": names.stackedWidget_Add_Space_QGroupBox,
        "name": "qt_wizard_cancel",
        "type": "QPushButton",
        "visible": 1,
    }
    SPACE_NAME_SELECTOR = {
        "container": names.quickWidget_scrollView_ScrollView,
        "type": "Label",
        "visible": True,
    }
    CREATE_REMOTE_FOLDER_BUTTON = {
        "container": names.add_Folder_Sync_Connection_groupBox_QGroupBox,
        "name": "addFolderButton",
        "type": "QPushButton",
        "visible": 1,
    }
    CREATE_REMOTE_FOLDER_INPUT = {
        "buddy": names.create_Remote_Folder_Enter_the_name_of_the_new_folder_to_be_created_below_QLabel,
        "type": "QLineEdit",
        "unnamed": 1,
        "visible": 1,
    }
    CREATE_REMOTE_FOLDER_CONFIRM_BUTTON = {
        "text": "OK",
        "type": "QPushButton",
        "unnamed": 1,
        "visible": 1,
        "window": names.create_Remote_Folder_QInputDialog,
    }
    REFRESH_BUTTON = {
        "container": names.add_Folder_Sync_Connection_groupBox_QGroupBox,
        "name": "refreshButton",
        "type": "QPushButton",
        "visible": 1,
    }
    REMOTE_FOLDER_SELECTION_INPUT = {
        "name": "folderEntry",
        "type": "QLineEdit",
        "visible": 1,
        "window": names.add_Folder_Sync_Connection_OCC_FolderWizard,
    }
    ADD_FOLDER_SYNC_BUTTON = {
        "checkable": False,
        "container": names.stackedWidget_quickWidget_OCC_QmlUtils_OCQuickWidget,
        "id": "addSyncButton",
        "type": "Button",
        "unnamed": 1,
        "visible": True,
    }
    WARN_LABEL = {
        "window": names.add_Folder_Sync_Connection_OCC_FolderWizard,
        "name": "warnLabel",
        "type": "QLabel",
        "visible": 1,
    }

    CHOOSE_WHAT_TO_SYNC_FOLDER_TREE = {
        "column": 0,
        "container": names.deselect_remote_folders_you_do_not_wish_to_synchronize_Personal_QModelIndex,
        "type": "QModelIndex",
    }

    @staticmethod
    def set_sync_path_oc(sync_path):
        if not sync_path:
            sync_path = get_current_user_sync_path()
        squish.type(
            squish.waitForObject(SyncConnectionWizard.CHOOSE_LOCAL_SYNC_FOLDER),
            "<Ctrl+A>",
        )
        squish.type(
            SyncConnectionWizard.CHOOSE_LOCAL_SYNC_FOLDER,
            sync_path,
        )
        SyncConnectionWizard.next_step()

    @staticmethod
    def set_sync_path(sync_path=""):
        SyncConnectionWizard.set_sync_path_oc(sync_path)

    @staticmethod
    def next_step():
        squish.clickButton(squish.waitForObject(SyncConnectionWizard.NEXT_BUTTON))

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
        # NOTE: checkbox does not have separate object
        # click on (11,11) which is a checkbox
        squish.mouseClick(
            squish.waitForObject(SyncConnectionWizard.SELECTIVE_SYNC_ROOT_FOLDER),
            11,
            11,
            squish.Qt.NoModifier,
            squish.Qt.LeftButton,
        )


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
        squish.clickButton(
            squish.waitForObject(SyncConnectionWizard.ADD_SYNC_CONNECTION_BUTTON)
        )

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
        squish.clickButton(
            squish.waitForObject(
                SyncConnectionWizard.CANCEL_FOLDER_SYNC_CONNECTION_WIZARD
            )
        )

    @staticmethod
    def select_space(space_name):
        selector = SyncConnectionWizard.SPACE_NAME_SELECTOR.copy()
        selector["text"] = space_name
        squish.mouseClick(squish.waitForObject(selector))

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
    def select_or_unselect_folders_to_sync(folders, should_select=True, in_choose_what_to_sync_dialog=False):
        if should_select:
            # First deselect all
            SyncConnectionWizard.deselect_all_remote_folders()
        folder_tree_locator = SyncConnectionWizard.get_folder_tree_locator(in_choose_what_to_sync_dialog)
        for folder in folders:
            folder_levels = folder.strip("/").split("/")
            parent_selector = None
            for sub_folder in folder_levels:
                if not parent_selector:
                    folder_tree_locator["text"] = sub_folder
                    parent_selector = folder_tree_locator
                    selector = parent_selector
                else:
                    selector = {
                        "column": "0",
                        "container": parent_selector,
                        "text": sub_folder,
                        "type": "QModelIndex",
                    }
                if (
                    len(folder_levels) == 1
                    or folder_levels.index(sub_folder) == len(folder_levels) - 1
                ):
                    # NOTE: checkbox does not have separate object
                    # click on (11,11) which is a checkbox
                    squish.mouseClick(
                        squish.waitForObject(selector),
                        11,
                        11,
                        squish.Qt.NoModifier,
                        squish.Qt.LeftButton,
                    )
                else:
                    squish.doubleClick(squish.waitForObject(selector))

    @staticmethod
    def confirm_choose_what_to_sync_selection():
        squish.clickButton(squish.waitForObject(names.stackedWidget_OK_QPushButton))

    @staticmethod
    def _handle_folder_selection(folders, should_select, in_choose_what_to_sync_dialog):
        SyncConnectionWizard.select_or_unselect_folders_to_sync(
            folders,
            should_select=should_select,
            in_choose_what_to_sync_dialog=in_choose_what_to_sync_dialog
        )

        if in_choose_what_to_sync_dialog:
            SyncConnectionWizard.confirm_choose_what_to_sync_selection()
        else:
            SyncConnectionWizard.add_sync_connection()

    @staticmethod
    def unselect_folders_to_sync(folders, in_choose_what_to_sync_dialog=False):
        SyncConnectionWizard._handle_folder_selection(
            folders, should_select=False, in_choose_what_to_sync_dialog=in_choose_what_to_sync_dialog
        )

    @staticmethod
    def select_folders_to_sync(folders, in_choose_what_to_sync_dialog=False):
        SyncConnectionWizard._handle_folder_selection(
            folders, should_select=True, in_choose_what_to_sync_dialog=in_choose_what_to_sync_dialog
        )

    @staticmethod
    def get_folder_tree_locator(in_choose_what_to_sync_dialog=False):
        return (
            SyncConnectionWizard.CHOOSE_WHAT_TO_SYNC_FOLDER_TREE.copy()
            if in_choose_what_to_sync_dialog
            else SyncConnectionWizard.ADD_SPACE_FOLDER_TREE.copy()
        )
