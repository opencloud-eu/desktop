# from objectmaphelper import RegularExpression
from types import SimpleNamespace
from appium.webdriver.common.appiumby import AppiumBy as By
from selenium.common.exceptions import NoSuchElementException

from helpers.FilesHelper import build_conflicted_regex
from helpers.ConfigHelper import get_config
from helpers.AppHelper import app


class Activity:
    TAB_CONTAINER = SimpleNamespace(by=None, selector=None)
    SUBTAB_CONTAINER = SimpleNamespace(
        by=By.CLASS_NAME, selector="[page tab | {tab_name}]"
    )
    NOT_SYNCED_TABLE = SimpleNamespace(by=None, selector=None)
    LOCAL_ACTIVITY_FILTER_BUTTON = SimpleNamespace(by=By.NAME, selector="Filter")
    LOCAL_ACTIVITY_FILTER_OPTION_SELECTOR = SimpleNamespace(by=By.NAME, selector=None)
    LOCAL_ACTIVITY_TABLE = SimpleNamespace(by=By.NAME, selector="Local activity table")
    FILTER_BUTTON_SELECTED_STATE = SimpleNamespace(by=By.NAME, selector="1 Filter")
    NOT_SYNCED_FILTER_BUTTON = SimpleNamespace(by=None, selector=None)
    NOT_SYNCED_FILTER_OPTION_SELECTOR = SimpleNamespace(by=None, selector=None)
    SYNCED_ACTIVITY_TABLE_HEADER_SELECTOR = SimpleNamespace(by=None, selector=None)
    NOT_SYNCED_ACTIVITY_TABLE_HEADER_SELECTOR = SimpleNamespace(by=None, selector=None)
    SYNCED_ACTIVITY_STATUS = SimpleNamespace(by=By.NAME, selector=None)

    @staticmethod
    def get_not_synced_file_selector(resource):
        return {
            "column": 1,
            "container": Activity.NOT_SYNCED_TABLE,
            "text": resource,
            "type": "QModelIndex",
        }

    @staticmethod
    def get_not_synced_status(row):
        return squish.waitForObjectExists(
            {
                "column": 6,
                "row": row,
                "container": Activity.NOT_SYNCED_TABLE,
                "type": "QModelIndex",
            }
        ).text

    @staticmethod
    def click_tab(tab_name):
        selector = Activity.SUBTAB_CONTAINER.selector.format(tab_name=tab_name)
        app().find_element(Activity.SUBTAB_CONTAINER.by, selector).click()

    @staticmethod
    def check_file_exist(filename):
        squish.waitForObjectExists(
            Activity.get_not_synced_file_selector(
                RegularExpression(build_conflicted_regex(filename))
            )
        )

    @staticmethod
    def is_resource_blacklisted(filename):
        result = squish.waitFor(
            lambda: Activity.has_sync_status(filename, "Blacklisted"),
            get_config("maxSyncTimeout") * 1000,
        )
        return result

    @staticmethod
    def is_resource_ignored(filename):
        result = squish.waitFor(
            lambda: Activity.has_sync_status(filename, "File Ignored"),
            get_config("maxSyncTimeout") * 1000,
        )
        return result

    @staticmethod
    def is_resource_excluded(filename):
        result = squish.waitFor(
            lambda: Activity.has_sync_status(filename, "Excluded"),
            get_config("maxSyncTimeout") * 1000,
        )
        return result

    @staticmethod
    def has_sync_status(filename, status):
        try:
            app().find_element(Activity.SYNCED_ACTIVITY_STATUS.by, status)
            return True
        except:
            return False

    @staticmethod
    def select_synced_filter(sync_filter):
        app().find_element(
            Activity.LOCAL_ACTIVITY_FILTER_BUTTON.by,
            Activity.LOCAL_ACTIVITY_FILTER_BUTTON.selector,
        ).click()
        container = app().find_element(
            Activity.LOCAL_ACTIVITY_TABLE.by, Activity.LOCAL_ACTIVITY_TABLE.selector
        )
        # NOTE: clicking filter options does not work
        container.find_element(
            Activity.LOCAL_ACTIVITY_FILTER_OPTION_SELECTOR.by, sync_filter
        ).click()
        # FIXME: enable the check below once the filter options are clickable
        # confirm filter is applied
        # app().find_element(
        #     Activity.FILTER_BUTTON_SELECTED_STATE.by,
        #     Activity.FILTER_BUTTON_SELECTED_STATE.selector,
        # )

    @staticmethod
    def get_synced_file_selector(resource):
        return {
            "column": Activity.get_synced_table_column_number_by_name("File"),
            "container": Activity.LOCAL_ACTIVITY_TABLE,
            "text": resource,
            "type": "QModelIndex",
        }

    @staticmethod
    def get_synced_table_column_number_by_name(column_name):
        return squish.waitForObject(
            {
                "container": Activity.SYNCED_ACTIVITY_TABLE_HEADER_SELECTOR,
                "text": column_name,
                "type": "HeaderViewItem",
                "visible": True,
            }
        )["section"]

    @staticmethod
    def has_activity(resource, action, account):
        try:
            row = app().find_element(By.NAME, resource)
            row_y = row.rect['y']
            # check other properties using current row position
            action_cells = app().find_elements(By.NAME, action)
            found_action_cell = False
            for action_el in action_cells:
                if action_el.rect['y'] == row_y:
                    found_action_cell = True
                    break
            if not found_action_cell:
                raise NoSuchElementException(
                    f'Activity for "{resource}" does not have "{action}" action'
                )
            account_cells = app().find_elements(By.NAME, account)
            found_account_cell = False
            for account_el in account_cells:
                if account_el.rect['y'] == row_y:
                    found_account_cell = True
                    break
            if not found_account_cell:
                raise NoSuchElementException(
                    f'Activity for "{resource}" does not have "{account}" account label'
                )
            return True
        except:
            return False

    @staticmethod
    def select_not_synced_filter(filter_option):
        squish.clickButton(squish.waitForObject(Activity.NOT_SYNCED_FILTER_BUTTON))
        squish.activateItem(
            squish.waitForObjectItem(
                Activity.NOT_SYNCED_FILTER_OPTION_SELECTOR, filter_option
            )
        )

    @staticmethod
    def get_not_synced_table_column_number_by_name(column_name):
        return squish.waitForObject(
            {
                "container": Activity.NOT_SYNCED_ACTIVITY_TABLE_HEADER_SELECTOR,
                "text": column_name,
                "type": "HeaderViewItem",
                "visible": True,
            }
        )["section"]

    @staticmethod
    def check_not_synced_table(resource, status, account):
        try:
            file_row = squish.waitForObject(
                Activity.get_not_synced_file_selector(resource),
                get_config("lowestSyncTimeout") * 1000,
            )["row"]
            squish.waitForObjectExists(
                {
                    "column": Activity.get_not_synced_table_column_number_by_name(
                        "Status"
                    ),
                    "row": file_row,
                    "container": Activity.NOT_SYNCED_TABLE,
                    "text": status,
                    "type": "QModelIndex",
                }
            )
            squish.waitForObjectExists(
                {
                    "column": Activity.get_not_synced_table_column_number_by_name(
                        "Account"
                    ),
                    "row": file_row,
                    "container": Activity.NOT_SYNCED_TABLE,
                    "text": account,
                    "type": "QModelIndex",
                }
            )
            return True
        except:
            return False
