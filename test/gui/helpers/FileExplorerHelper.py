from appium.webdriver import Remote
from appium.options.common.base import AppiumOptions
from selenium.webdriver.common.by import By
from dogtail.tree import root

from helpers.ConfigHelper import get_config, is_linux


file_explorer_session = None
LINUX_FILE_EXPLORERS = ["org.gnome.Nautilus", "caja", "nemo"]

def get_file_explorer_name():
    if is_linux():
        return "caja"


def get_file_explorer():
    return file_explorer_session


def close_file_explorer():
    global file_explorer_session
    if file_explorer_session:
        file_explorer_session.quit()
        file_explorer_session = None


def open_file_explorer(resource_path):
    global file_explorer_session
    options = AppiumOptions()
    options.set_capability('app', f'{get_file_explorer_name()} {resource_path}')
    file_explorer_session = Remote(command_executor=get_config('webdriver_url'), options=options)
    return file_explorer_session


def get_running_explorer_pid(window_name):
    apps = root.applications()
    for app in apps:
        # print("App: ", app.name)
        if app.name not in LINUX_FILE_EXPLORERS:
            continue
        for window in app.children:
            if window.name == window_name:
                return app.get_process_id()
    return None


def create_session_from_running_explorer(window_name="Personal"):
    global file_explorer_session
    explorer_pid = get_running_explorer_pid(window_name)
    if explorer_pid is None:
        raise RuntimeError(f"File explorer not found with window name '{window_name}'")

    options = AppiumOptions()
    options.set_capability('app', f'{explorer_pid}')
    file_explorer_session = Remote(command_executor=get_config('webdriver_url'), options=options)
    return file_explorer_session


def navigate_to(parent_paths = ()):
    explorer = get_file_explorer()
    for parent_path in parent_paths:
        parent = explorer.find_element(By.NAME, parent_path)
        parent.native_double_click()


def check_file_context_menu_items(resource):
    explorer = get_file_explorer()

    resource = explorer.find_element(By.NAME, resource)
    resource.native_click(button='right')
    menu = explorer.find_element(By.NAME, 'OpenCloud Desktop')
    menu.native_click()
    items = menu.find_elements(By.NAME, "Share...")
    print(len(items))
    for item in items:
        item.native_click()

def copy_private_link(resource):
    explorer = get_file_explorer()
    resource = explorer.find_element(By.NAME, resource)
    resource.native_click(button='right')
    menu = explorer.find_element(By.NAME, 'OpenCloud Desktop')
    menu.native_click()
    copy_private_link_item = menu.find_element(By.NAME, "Copy private link to clipboard")
    copy_private_link_item.native_click()


def get_clipboard_content():
    return file_explorer_session.get_clipboard_text()
