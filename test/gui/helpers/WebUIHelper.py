import os
import subprocess
import pyperclip
from playwright.sync_api import sync_playwright

# import squish


# def get_clipboard_text():
#     try:
#         return squish.getClipboardText()
#     except:
#         # Retry after 2 seconds
#         squish.snooze(2)
#         return squish.getClipboardText()


def authorize_via_webui(username, password, login_type='oidc'):
    # script_path = os.path.dirname(os.path.realpath(__file__))

    # webui_path = os.path.join(script_path, '..', 'webUI')
    # os.chdir(webui_path)

    # envs = {
    #     'OC_USERNAME': username.strip('"'),
    #     'OC_PASSWORD': password.strip('"'),
    #     'OC_AUTH_URL': get_clipboard_text(),
    # }
    # proc = subprocess.run(
    #     f"pnpm run {login_type}-login",
    #     capture_output=True,
    #     shell=True,
    #     env={**os.environ, **envs},
    #     check=False,
    # )
    # if proc.returncode:
    #     if proc.stderr.decode('utf-8'):
    #         raise OSError(proc.stderr.decode('utf-8'))
    #     raise OSError(proc.stdout.decode('utf-8'))
    # os.chdir(script_path)

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
