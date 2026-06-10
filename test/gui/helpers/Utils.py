from selenium.webdriver.support.ui import WebDriverWait
from selenium.common.exceptions import TimeoutException


def wait_for(condition, timeout=10, interval=0.5):
    wait = WebDriverWait(None, timeout, poll_frequency=interval)
    try:
        wait.until(lambda _: condition())
        return True
    except TimeoutException:
        return False
