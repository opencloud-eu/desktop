import pyautogui
import psutil
from appium.webdriver import Remote, WebElement
from appium.options.common.base import AppiumOptions

from helpers.ConfigHelper import get_config, get_app_env
from helpers.ElementHelper import get_element_center_xy
from helpers.keys.keys_map import get_key


def native_click(self, **kwargs):
    x, y = get_element_center_xy(self)
    pyautogui.click(x, y, **kwargs)


def native_double_click(self, **kwargs):
    x, y = get_element_center_xy(self)
    pyautogui.doubleClick(x, y, **kwargs)


def native_send_keys(self, key):
    pyautogui.press(get_key(key))


# bind custom element methods
WebElement.native_click = native_click
WebElement.native_double_click = native_double_click
WebElement.native_send_keys = native_send_keys

app_driver = None


def app():
    return app_driver


def create_app_session():
    global app_driver
    logfile = get_config("currentAppLogFile")
    command_args = f' --logfile {logfile}'

    options = AppiumOptions()
    options.set_capability(
        'app',
        f'{get_config("app_path")} -s {command_args} --logdebug',
    )
    options.set_capability('appium:environ', get_app_env())
    app_driver = Remote(command_executor='http://localhost:4723', options=options)
    app_driver.implicitly_wait = 10


def close_and_kill_app():
    """
    Close Appium session and kill the desktop client process.
    Use this for both mid-scenario and end-of-scenario cleanup.
    """
    global app_driver
    # Quit Appium session
    if app_driver is not None:
        app_driver.quit()

    # Kill remaining process by exe path
    app_path = get_config("app_path")
    for process in psutil.process_iter(['pid', 'exe']):
        if process.info['exe'] == app_path:
            print("Closing desktop client...")
            psutil.Process(process.info['pid']).kill()
            break

    # Reset driver for reuse
    app_driver = None
