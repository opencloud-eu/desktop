# from objectmaphelper import RegularExpression
from types import SimpleNamespace
from appium.webdriver.common.appiumby import AppiumBy as By
from selenium.webdriver.common.keys import Keys
from selenium.common.exceptions import NoSuchElementException, WebDriverException

from helpers.ConfigHelper import get_config
from helpers.AppHelper import app
from helpers.Utils import wait_for


class Activity:
    SUBTAB_CONTAINER = SimpleNamespace(
        by=By.XPATH, selector="//page_tab[starts-with(@name, '{tab_name}')]"
    )
    LOCAL_ACTIVITY_FILTER_BUTTON = SimpleNamespace(by=By.NAME, selector="Filter")
    LOCAL_ACTIVITY_FILTER_OPTION_SELECTOR = SimpleNamespace(by=By.NAME, selector=None)
    LOCAL_ACTIVITY_TABLE = SimpleNamespace(by=By.NAME, selector="Local activity table")
    FILTER_BUTTON_SELECTED_STATE = SimpleNamespace(
        by=By.XPATH, selector="//*[contains(@name, '1 Filter')]"
    )
    NOT_SYNCED_FILTER_BUTTON = SimpleNamespace(
        by=By.ACCESSIBILITY_ID,
        selector="QApplication.Settings.centralwidget.dialogStack.page.stack.OCC::ActivitySettings.QTabWidget.qt_tabwidget_stackedwidget.OCC__IssuesWidget._filterButton",
    )
    NOT_SYNCED_ACTIVITY_CONFLICT_FILE = SimpleNamespace(
        by=By.XPATH, selector="//*[starts-with(@name, '{filename} (conflicted copy')]"
    )
    SYNCED_ACTIVITY_STATUS = SimpleNamespace(by=By.NAME, selector=None)

    @staticmethod
    def open_tab(tab_name):
        selector = Activity.SUBTAB_CONTAINER.selector.format(tab_name=tab_name)
        app().find_element(Activity.SUBTAB_CONTAINER.by, selector).click()

    @staticmethod
    def has_conflict_file(filename):
        filename = filename.rsplit(".", 1)[0]
        has_activity = wait_for(
            lambda: (
                app()
                .find_element(
                    Activity.NOT_SYNCED_ACTIVITY_CONFLICT_FILE.by,
                    Activity.NOT_SYNCED_ACTIVITY_CONFLICT_FILE.selector.format(filename=filename),
                )
                .is_displayed()
            ),
            get_config('max_timeout'),
        )
        if not has_activity:
            raise AssertionError("File conflict activity not found")

    @staticmethod
    def is_resource_blacklisted(filename):
        return wait_for(
            lambda: Activity.has_sync_status(filename, "Blacklisted"),
            get_config("sync_timeout"),
        )

    @staticmethod
    def is_resource_ignored(filename):
        return wait_for(
            lambda: Activity.has_sync_status(filename, "File Ignored"),
            get_config("sync_timeout"),
        )

    @staticmethod
    def is_resource_excluded(filename):
        return wait_for(
            lambda: Activity.has_sync_status(filename, "Excluded"),
            get_config("sync_timeout"),
        )

    @staticmethod
    def has_sync_status(filename, status):
        try:
            row = app().find_element(By.NAME, filename)
            row_y = row.rect['y']
            status_cells = app().find_elements(Activity.SYNCED_ACTIVITY_STATUS.by, status)
            for status_el in status_cells:
                if status_el.rect['y'] == row_y:
                    return True
            return False
        except NoSuchElementException:
            return False
        except WebDriverException as e:
            if "NoneType" in str(e):
                return False

    @staticmethod
    def select_synced_filter(sync_filter):
        menu = app().find_element(
            Activity.LOCAL_ACTIVITY_FILTER_BUTTON.by,
            Activity.LOCAL_ACTIVITY_FILTER_BUTTON.selector,
        )
        menu.click()

        # NOTE: Filter options are not visible in the accessibility tree.
        # As a workaround, select the second filter option (which is an account filter).
        # This means we cannot select a specific account filter for now.
        menu.send_keys(Keys.ARROW_DOWN)
        menu.send_keys(Keys.ARROW_DOWN)
        menu.send_keys(Keys.ENTER)
        # confirm filter is applied
        app().find_element(
            Activity.FILTER_BUTTON_SELECTED_STATE.by,
            Activity.FILTER_BUTTON_SELECTED_STATE.selector,
        )

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
        menu = app().find_element(
            Activity.NOT_SYNCED_FILTER_BUTTON.by,
            Activity.NOT_SYNCED_FILTER_BUTTON.selector,
        )
        menu.click()
        # NOTE: Filter options are not visible in the accessibility tree.
        # As a workaround, select the 6th filter option (which is an Excluded filter).
        # This means we cannot select a specific Excluded filter for now.
        for _ in range(6):
            menu.send_keys(Keys.ARROW_DOWN)
        menu.send_keys(Keys.ENTER)
