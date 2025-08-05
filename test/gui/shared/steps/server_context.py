from helpers.api import provisioning, webdav_helper as webdav

from pageObjects.Toolbar import Toolbar


@Then(
    r'^as "([^"].*)" (?:file|folder) "([^"].*)" should not exist in the server',
    regexp=True,
)
def step(context, user_name, resource_name):
    test.compare(
        webdav.resource_exists(user_name, resource_name),
        False,
        f"Resource '{resource_name}' should not exist, but does",
    )


@Then(
    r'^as "([^"].*)" (?:file|folder) "([^"].*)" should exist in the server', regexp=True
)
def step(context, user_name, resource_name):
    test.compare(
        webdav.resource_exists(user_name, resource_name),
        True,
        f"Resource '{resource_name}' should exist, but does not",
    )


@Then('as "|any|" the file "|any|" should have the content "|any|" in the server')
def step(context, user_name, file_name, content):
    text_content = webdav.get_file_content(user_name, file_name)
    test.compare(
        text_content,
        content,
        f"File '{file_name}' should have content '{content}' but found '{text_content}'",
    )


@Then(
    r'as user "([^"].*)" folder "([^"].*)" should contain "([^"].*)" items in the server',
    regexp=True,
)
def step(context, user_name, folder_name, items_number):
    total_items = webdav.get_folder_items_count(user_name, folder_name)
    test.compare(
        total_items, items_number, f'Folder should contain {items_number} items'
    )


@Given('user "|any|" has created folder "|any|" in the server')
def step(context, user, folder_name):
    webdav.create_folder(user, folder_name)


@Given('user "|any|" has uploaded file with content "|any|" to "|any|" in the server')
def step(context, user, file_content, file_name):
    webdav.create_file(user, file_name, file_content)


@When('the user clicks on the settings tab')
def step(context):
    Toolbar.open_settings_tab()


@When('user "|any|" uploads file with content "|any|" to "|any|" in the server')
def step(context, user, file_content, file_name):
    webdav.create_file(user, file_name, file_content)


@When('user "|any|" deletes the folder "|any|" in the server')
def step(context, user, folder_name):
    webdav.delete_resource(user, folder_name)


@Given('user "|any|" has been created in the server with default attributes')
def step(context, user):
    provisioning.create_user(user)


@Given('user "|any|" has uploaded file "|any|" to "|any|" in the server')
def step(context, user, file_name, destination):
    webdav.upload_file(user, file_name, destination)


@Then('as "|any|" the content of file "|any|" in the server should match the content of local file "|any|"')
def step(context, user_name, server_file_name, local_file_name):
    server_content = webdav.get_file_content(user_name, server_file_name)
    local_content  = open(get_file_for_upload(local_file_name), "rb").read()

    test.compare(
        server_content,
        local_content,
        f"Server file '{server_file_name}' differs from local file '{local_file_name}'"
    )
