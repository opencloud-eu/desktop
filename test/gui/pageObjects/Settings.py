from types import SimpleNamespace
from appium.webdriver.common.appiumby import AppiumBy as By

from helpers.AppHelper import app


class Settings:
    ABOUT_BUTTON = SimpleNamespace(by=By.NAME, selector="About")
    ABOUT_DIALOG = SimpleNamespace(by=By.CLASS_NAME, selector="[page tab | About]")
    ABOUT_DIALOG_OK_BUTTON = SimpleNamespace(by=By.NAME, selector="OK")
    GENERAL_SETTING_START_ON_LOGIN = SimpleNamespace(
        by=By.XPATH, selector="//panel/*[@name='Start on Login']"
    )
    GENERAL_SETTING_LANGUAGE = SimpleNamespace(
        by=By.XPATH, selector="//panel/label[@name='Language']"
    )
    ADVANCED_SETTING_SYNC_HIDDEN_FILES = SimpleNamespace(
        by=By.XPATH, selector="//panel/*[@name='Sync hidden files']"
    )
    ADVANCED_SETTING_EDIT_IGNORED_FILES = SimpleNamespace(
        by=By.XPATH, selector="//panel/*[@name='Edit Ignored Files']"
    )
    ADVANCED_SETTING_LOG_SETTINGS = SimpleNamespace(
        by=By.XPATH, selector="//panel/*[@name='Log Settings']"
    )
    NETWORK_SETTING_DOWNLOAD_BANDWIDTH = SimpleNamespace(
        by=By.XPATH, selector="//panel[@name='Download Bandwidth']"
    )
    NETWORK_SETTING_UPLOAD_BANDWIDTH = SimpleNamespace(
        by=By.XPATH, selector="//panel[@name='Upload Bandwidth']"
    )

    @staticmethod
    def has_general_setting(setting):
        if setting.lower() == "start on login":
            locator = Settings.GENERAL_SETTING_START_ON_LOGIN
        elif setting.lower() == "language":
            locator = Settings.GENERAL_SETTING_LANGUAGE
        else:
            raise ValueError(f"Unknown general setting: {setting}")
        return app().find_element(locator.by, locator.selector).is_displayed()

    @staticmethod
    def has_advanced_setting(setting):
        if setting.lower() == "sync hidden files":
            locator = Settings.ADVANCED_SETTING_SYNC_HIDDEN_FILES
        elif setting.lower() == "edit ignored files":
            locator = Settings.ADVANCED_SETTING_EDIT_IGNORED_FILES
        elif setting.lower() == "log settings":
            locator = Settings.ADVANCED_SETTING_LOG_SETTINGS
        else:
            raise ValueError(f"Unknown advanced setting: {setting}")
        return app().find_element(locator.by, locator.selector).is_displayed()

    @staticmethod
    def has_network_setting(setting):
        if setting.lower() == "download bandwidth":
            locator = Settings.NETWORK_SETTING_DOWNLOAD_BANDWIDTH
        elif setting.lower() == "upload bandwidth":
            locator = Settings.NETWORK_SETTING_UPLOAD_BANDWIDTH
        else:
            raise ValueError(f"Unknown network setting: {setting}")
        return app().find_element(locator.by, locator.selector).is_displayed()

    @staticmethod
    def open_about_dialog():
        app().find_element(Settings.ABOUT_BUTTON.by, Settings.ABOUT_BUTTON.selector).click()

    @staticmethod
    def has_about_dialog():
        return (
            app()
            .find_element(Settings.ABOUT_DIALOG.by, Settings.ABOUT_DIALOG.selector)
            .is_displayed()
        )

    @staticmethod
    def close_about_dialog():
        app().find_element(
            Settings.ABOUT_DIALOG_OK_BUTTON.by, Settings.ABOUT_DIALOG_OK_BUTTON.selector
        ).click()
