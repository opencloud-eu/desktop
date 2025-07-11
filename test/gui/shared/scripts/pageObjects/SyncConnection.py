import names
import squish
import object  # pylint: disable=redefined-builtin


class SyncConnection:
    FOLDER_SYNC_CONNECTION_LIST = {
        "container": names.quickWidget_scrollView_ScrollView,
        "type": "ListView",
        "visible": True,
    }
    FOLDER_SYNC_CONNECTION = {
        "container": names.settings_stack_QStackedWidget,
        "name": "_folderList",
        "type": "QListView",
        "visible": 1,
    }
    FOLDER_SYNC_CONNECTION_MENU_BUTTON = {
        "container": names.quickWidget_scrollView_ScrollView,
        "id": "moreButton",
        "type": "Image",
        "visible": True
    }
    MENU = {
        "checkable": False,
        "container": names.quickWidget_Overlay,
        "enabled": True,
        "text": "",
        "type": "MenuItem",
        "unnamed": 1,
        "visible": True
    }
    SELECTIVE_SYNC_APPLY_BUTTON = {
        "container": names.settings_stack_QStackedWidget,
        "name": "selectiveSyncApply",
        "type": "QPushButton",
        "visible": 1,
    }
    CANCEL_FOLDER_SYNC_CONNECTION_DIALOG = {
        "text": "Cancel",
        "type": "QPushButton",
        "unnamed": 1,
        "visible": 1,
        "window": names.confirm_Folder_Sync_Connection_Removal_QMessageBox,
    }
    REMOVE_FOLDER_SYNC_CONNECTION_BUTTON = {
        "text": "Remove Space",
        "type": "QPushButton",
        "unnamed": 1,
        "visible": 1,
        "window": names.confirm_removal_of_Space_QMessageBox,
    }

    @staticmethod
    def open_menu():
        menu_button = squish.waitForObject(
            SyncConnection.FOLDER_SYNC_CONNECTION_MENU_BUTTON
        )
        squish.mouseClick(menu_button)

    @staticmethod
    def perform_action(action):
        SyncConnection.open_menu()
        selector = SyncConnection.MENU.copy()
        selector['text'] = action
        squish.mouseClick(
            squish.waitForObject(selector)
        )

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
    def unselect_folder_in_selective_sync(folder_name):
        sync_folders = object.children(
            squish.waitForObject(SyncConnection.FOLDER_SYNC_CONNECTION)
        )
        for sync_folder in sync_folders:
            # TODO: allow selective sync in other sync folders as well
            if hasattr(sync_folder, "text") and sync_folder.text == "Personal":
                items = object.children(sync_folder)
                for item in items:
                    if hasattr(item, "text") and item.text:
                        # remove item size suffix
                        # example: folder1 (13 B) => folder1
                        item_name = item.text.rsplit(" ", 1)
                        if item_name[0] == folder_name:
                            # NOTE: checkbox does not have separate object
                            # click on (11,11) which is a checkbox to unselect the folder
                            squish.mouseClick(
                                item,
                                11,
                                11,
                                squish.Qt.NoModifier,
                                squish.Qt.LeftButton,
                            )
                            break
        squish.clickButton(
            squish.waitForObject(SyncConnection.SELECTIVE_SYNC_APPLY_BUTTON)
        )

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
