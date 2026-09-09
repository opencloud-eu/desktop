import pyautogui
import psutil
import threading
import json
from appium.webdriver import Remote, WebElement
from appium.options.common.base import AppiumOptions
from appium.webdriver.common.appiumby import AppiumBy as By
from selenium.common.exceptions import WebDriverException, NoSuchElementException

import helpers.api.http_helper as request
from helpers.ConfigHelper import get_config, get_app_env, is_windows
from helpers.ElementHelper import get_element_center_xy
from helpers.keys.keys_map import get_key
from helpers.Utils import wait_for


def native_click(self, **kwargs):
    x, y = get_element_center_xy(self)
    win_x, win_y = get_window_location()
    if x < win_x:
        x = x + win_x
    if y < win_y:
        y = y + win_y
    pyautogui.click(x, y, **kwargs)


def native_double_click(self, **kwargs):
    x, y = get_element_center_xy(self)
    win_x, win_y = get_window_location()
    if x < win_x:
        x = x + win_x
    if y < win_y:
        y = y + win_y
    pyautogui.doubleClick(x, y, **kwargs)


def native_send_keys(self, key):
    pyautogui.press(get_key(key))


def find_element(self, by, selector, timeout=None):
    """
    Returns a visible element.
    Throws if no elements are found or if multiple visible elements are found.
    """
    if timeout is not None:
        set_implicit_wait(timeout)

    try:
        elements = self.find_elements(by, selector)
        elements_count = len(elements)
        if elements_count > 1:
            visible_elements = [el for el in elements if el.is_displayed()]
            if len(visible_elements) == 1:
                return visible_elements.pop()
            raise WebDriverException(f'Found {elements_count} elements using "{by}={selector}"')
        if elements_count == 0:
            raise NoSuchElementException(f'No element found for "{by}={selector}"')
        return elements[0]
    finally:
        if timeout is not None:
            set_implicit_wait(get_config('min_timeout'))


def pause(self):
    threading.Event().wait()


Remote.find_element = find_element
Remote.pause = pause
WebElement.native_click = native_click
WebElement.native_double_click = native_double_click
WebElement.native_send_keys = native_send_keys
WebElement.find_element = find_element

app_driver = None


def app():
    return app_driver


def create_app_session():
    global app_driver

    options = AppiumOptions()

    if is_windows():
        options.set_capability('automationName', 'NovaWindows')
        options.set_capability('platformName', 'Windows')
        options.set_capability('app', get_config('app_path'))

        try:
            logfile = get_config('currentAppLogFile')
        except (KeyError, Exception):
            logfile = None

        if logfile:
            options.set_capability('appArguments', f'--logfile {logfile} --logdebug')

        options.set_capability('shouldCloseApp', True)
    else:
        try:
            logfile = get_config('currentAppLogFile')
        except (KeyError, Exception):
            logfile = None

        app_args = '-s --logdebug'
        if logfile:
            app_args += f' --logfile {logfile}'
        options.set_capability('app', f'{get_config("app_path")} {app_args}',)

    options.set_capability('appium:environ', get_app_env())
    options.set_capability('timeouts', {'implicit': get_config('min_timeout') * 1000})
    app_driver = Remote(command_executor=get_config('webdriver_url'), options=options)

def close_and_kill_app():
    """
    Close Appium session and kill the desktop client process.
    Use this for both mid-scenario and end-of-scenario cleanup.
    """
    global app_driver
    # Quit Appium session
    if app_driver is not None:
        app_driver.quit()

    if is_windows():
        for process in psutil.process_iter(['pid', 'name']):
            if 'opencloud' in process.info['name'].lower():
                print("Closing desktop client...")
                psutil.Process(process.info['pid']).kill()
                break
    else:
        app_path = get_config("app_path")
        for process in psutil.process_iter(['pid', 'exe']):
            if process.info['exe'] == app_path:
                print("Closing desktop client...")
                psutil.Process(process.info['pid']).kill()
                break
    app_driver = None

def wait_until_app_terminated():
    def check_app():
        if is_windows():
            for process in psutil.process_iter(['name']):
                if 'opencloud' in process.info['name'].lower():
                    return False
        else:
            for process in psutil.process_iter(['exe']):
                if process.info['exe'] == get_config("app_path"):
                    return False
        return True

    terminated = wait_for(
        lambda: check_app(),
        get_config('max_timeout'),
    )
    if not terminated:
        raise ValueError("Desktop client did not terminate within the timeout period.")


def get_window_location():
    window = app().find_element(
        By.XPATH,
        "//*[contains(@name,'OpenCloud')]"
    ).location
    return window['x'], window['y']


def set_implicit_wait(timeout):
    """
    Set the implicit wait time for the current session.
    """
    session_id = app().session_id
    body = {'ms': timeout * 1000}
    response = request.post(
        f'{get_config("webdriver_url")}/session/{session_id}/timeouts/implicit_wait',
        json.dumps(body),
    )
    request.assert_http_status(response, 200, 'Failed to set implicit timeout')