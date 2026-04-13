import pyperclip
from playwright.sync_api import sync_playwright


def authorize_via_webui(username, password):
    url = pyperclip.paste()
    with sync_playwright() as pw:
        browser = pw.chromium.launch(headless=True)
        context = browser.new_context(ignore_https_errors=True)
        page = context.new_page()

        page.goto(url)
        page.fill('#oc-login-username', username)
        page.fill('#oc-login-password', password)
        page.click('button :text("Log in")')
        page.click('button :text("Allow")')
        page.wait_for_selector(':text("Login successful")')

        context.close()
        browser.close()
