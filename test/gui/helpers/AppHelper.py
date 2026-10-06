import re
import pyautogui
import psutil
import threading
import json
import base64
from appium.webdriver import Remote, WebElement
from appium.options.common.base import AppiumOptions
from appium.webdriver.common.appiumby import AppiumBy as By
from selenium.common.exceptions import WebDriverException, NoSuchElementException
from selenium.webdriver.support import expected_conditions as EC
from selenium.webdriver.support.ui import WebDriverWait

import helpers.api.http_helper as request
from helpers.ConfigHelper import get_config, get_app_env, is_linux, is_windows
from helpers.ElementHelper import get_element_center_xy
from helpers.keys.keys_map import get_key
from helpers.Utils import wait_for
from helpers.FilesHelper import normalize_path


def native_click(self, **kwargs):
    x, y = get_element_center_xy(self)
    if isinstance(self, WebElement):
        win_x, win_y = get_window_location(self.parent)
    else:
        win_x, win_y = get_window_location(self)
    if x < win_x:
        x = x + win_x
    if y < win_y:
        y = y + win_y
    pyautogui.click(x, y, **kwargs)


def native_double_click(self, **kwargs):
    x, y = get_element_center_xy(self)
    if isinstance(self, WebElement):
        win_x, win_y = get_window_location(self.parent)
    else:
        win_x, win_y = get_window_location(self)
    if x < win_x:
        x = x + win_x
    if y < win_y:
        y = y + win_y
    pyautogui.doubleClick(x, y, **kwargs)


def native_checkbox_toggle(self):
    if is_windows():
        app().execute_script("windows: toggle", self)
    elif is_linux():
        self.click()
    else:
        raise NotImplementedError("unsupported platform.")


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
        # reset implicit wait to default value
        if timeout is not None:
            set_implicit_wait(get_config('min_timeout'))


def find_elements_with_wait(self, by, selector, timeout=get_config('min_timeout')):
    wait = WebDriverWait(self, timeout)
    try:
        wait.until(EC.element_to_be_clickable((by, selector)))
    except WebDriverException as e:
        if not re.search(r"Found \d+ elements using", str(e)):
            raise
    return self.find_elements(by, selector)


def is_checked(self):
    if is_windows():
        return self.is_selected()
    if is_linux():
        return self.get_attribute("checked") == "true"
    raise NotImplementedError("unsupported platform.")


def get_native_clipboard_text(self):
    if is_windows():
        clipboard = self.execute_script("windows: getClipboard", {"contentType": "plaintext"})
        return base64.b64decode(clipboard).decode("utf-8")
    if is_linux():
        return self.get_clipboard_text()
    raise NotImplementedError("unsupported platform.")


def pause(self):
    threading.Event().wait()


# bind custom element methods
Remote.find_element = find_element
Remote.pause = pause
Remote.find_elements_with_wait = find_elements_with_wait
Remote.get_native_clipboard_text = get_native_clipboard_text
WebElement.native_click = native_click
WebElement.native_double_click = native_double_click
WebElement.native_checkbox_toggle = native_checkbox_toggle
WebElement.native_send_keys = native_send_keys
WebElement.find_element = find_element
WebElement.find_elements_with_wait = find_elements_with_wait
WebElement.is_checked = is_checked

app_driver = None


def app():
    return app_driver


def create_app_session():
    global app_driver

    options = AppiumOptions()

    logfile = get_config('currentAppLogFile')

    app_args = '-s --logdebug'
    if logfile:
        app_args += f' --logfile {logfile}'

    if is_windows():
        options.set_capability('automationName', 'NovaWindows')
        options.set_capability('platformName', 'Windows')
        options.set_capability('app', get_config('app_path'))
        options.set_capability('appArguments', app_args)
        options.set_capability("shouldCloseApp", True)

    elif is_linux():
        options.set_capability('app', f'{get_config("app_path")} {app_args}')

    options.set_capability('appium:environ', get_app_env())
    options.set_capability('timeouts', {'implicit': get_config('min_timeout') * 1000})
    app_driver = Remote(command_executor=get_config('webdriver_url'), options=options)
    # NOTE: these methods to set implicit wait are not working:
    # app_driver.implicitly_wait(5)
    # app_driver.implicitly_wait = 5


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
    app_path = normalize_path(app_path)

    for process in psutil.process_iter(['exe']):
        process_path = process.info["exe"] or ''
        process_path = normalize_path(process_path)
        if process_path == app_path:
            print("Closing desktop client...")
            process.kill()
            process.wait(timeout=get_config('min_timeout'))
            break

    # Reset driver for reuse
    app_driver = None


def wait_until_app_terminated():
    app_path = get_config("app_path")
    app_path = normalize_path(app_path)

    def check_app():
        for process in psutil.process_iter(['exe']):
            process_path = process.info["exe"] or ''
            process_path = normalize_path(process_path)
            if process_path == app_path:
                return False
        return True

    terminated = wait_for(
        lambda: check_app(),
        get_config('max_timeout'),
    )
    if not terminated:
        raise ValueError("Desktop client did not terminate within the timeout period.")


def get_window_location(driver):
    window = driver.find_element(By.XPATH, "//*[contains(@name,'OpenCloud Desktop')]").location
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
