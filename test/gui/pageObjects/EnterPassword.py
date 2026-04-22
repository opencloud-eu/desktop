from types import SimpleNamespace
from appium.webdriver.common.appiumby import AppiumBy as By

from pageObjects.AccountConnectionWizard import AccountConnectionWizard
from helpers.WebUIHelper import authorize_via_webui
from helpers.SetupClientHelper import app


class EnterPassword:
    LOGIN_CONTAINER = SimpleNamespace(by=None, selector=None)
    LOGIN_USER_LABEL = SimpleNamespace(by=By.XPATH, selector="//filler[@name='Login required']//label[contains(@name, 'Connecting')]")
    USERNAME_BOX = SimpleNamespace(by=None, selector=None)
    LOGOUT_BUTTON = SimpleNamespace(by=None, selector=None)

    def get_username(self):
        # Parse username from the login label:
        label = app().find_element(
            EnterPassword.LOGIN_USER_LABEL.by,
            EnterPassword.LOGIN_USER_LABEL.selector
        ).text
        username = label.split(" ", maxsplit=2)[1]
        return username.capitalize()

    def oidc_relogin(self, username, password):
        AccountConnectionWizard.copy_login_url()
        authorize_via_webui(username, password)

    def relogin(self, username, password, oauth=False):
        self.oidc_relogin(username, password)

    def login_after_setup(self, username, password):
        self.oidc_relogin(username, password)

    def accept_certificate(self):
        AccountConnectionWizard.accept_certificate()
