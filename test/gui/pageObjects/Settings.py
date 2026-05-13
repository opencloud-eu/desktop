from types import SimpleNamespace
from appium.webdriver.common.appiumby import AppiumBy as By


class Settings:
    CHECKBOX_OPTION_ITEM = SimpleNamespace(by=None, selector=None)
    NETWORK_OPTION_ITEM = SimpleNamespace(by=None, selector=None)
    ABOUT_BUTTON = SimpleNamespace(by=None, selector=None)
    ABOUT_DIALOG = SimpleNamespace(by=None, selector=None)
    ABOUT_DIALOG_OK_BUTTON = SimpleNamespace(by=None, selector=None)
    GENERAL_OPTIONS_MAP = SimpleNamespace(by=None, selector=None)
    ADVANCED_OPTION_MAP = SimpleNamespace(by=None, selector=None)
    NETWORK_OPTION_MAP = SimpleNamespace(by=None, selector=None)

    @staticmethod
    def get_checkbox_option_selector(name):
        selector = Settings.CHECKBOX_OPTION_ITEM.copy()
        selector.update({"name": name})
        if name == "languageDropdown":
            selector.update({"type": "QComboBox"})
        elif name in ("ignoredFilesButton", "logSettingsButton"):
            selector.update({"type": "QPushButton"})
        return selector

    @staticmethod
    def get_network_option_selector(name):
        selector = Settings.NETWORK_OPTION_ITEM.copy()
        selector.update({"name": name})
        return selector

    @staticmethod
    def check_general_option(option):
        selector = Settings.GENERAL_OPTIONS_MAP[option]
        squish.waitForObjectExists(Settings.get_checkbox_option_selector(selector))

    @staticmethod
    def check_advanced_option(option):
        selector = Settings.ADVANCED_OPTION_MAP[option]
        squish.waitForObjectExists(Settings.get_checkbox_option_selector(selector))

    @staticmethod
    def check_network_option(option):
        selector = Settings.NETWORK_OPTION_MAP[option]
        squish.waitForObjectExists(Settings.get_network_option_selector(selector))

    @staticmethod
    def open_about_button():
        squish.clickButton(squish.waitForObject(Settings.ABOUT_BUTTON))

    @staticmethod
    def wait_for_about_dialog_to_be_visible():
        squish.waitForObjectExists(Settings.ABOUT_DIALOG)

    @staticmethod
    def close_about_dialog():
        squish.clickButton(squish.waitForObjectExists(Settings.ABOUT_DIALOG_OK_BUTTON))
