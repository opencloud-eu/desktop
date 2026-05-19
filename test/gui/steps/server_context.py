from behave import given as Given, then as Then
from sure import ensure

from helpers.api import provisioning, webdav_helper as webdav
from helpers.TableParser import table_rows_hash


@Given('user "{user}" has been created in the server with default attributes')
def step(context, user):
    provisioning.create_user(user)


@Then(
    'as "{user_name}" {resource_type:ResourceType} "{resource_name}" should not exist in the server'
)
def step(context, user_name, resource_type, resource_name):
    resource_exists = webdav.resource_exists(user_name, resource_name)

    with ensure(
        '{0} "{1}" should not exist, but it does',
        resource_type.capitalize(),
        resource_name,
    ):
        resource_exists.should.be.false


@Then(
    'as "{user_name}" {resource_type:ResourceType} "{resource_name}" should exist in the server'
)
def step(context, user_name, resource_type, resource_name):
    resource_exists = webdav.resource_exists(user_name, resource_name)

    with ensure(
        '{0} "{1}" should exist, but it does not',
        resource_type.capitalize(),
        resource_name,
    ):
        resource_exists.should.be.true


@Then(
    'as "{user_name}" the file "{file_name}" should have the content "{content}" in the server'
)
def step(context, user_name, file_name, content):
    text_content = webdav.get_file_content(user_name, file_name)
    with ensure(
        '{0}  should have content "{1}" but found "{2}"',
        file_name,
        content,
        text_content,
    ):
        text_content.should.equal(content)


@Then(
    r'as user "([^"].*)" folder "([^"].*)" should contain "([^"].*)" items in the server',
    regexp=True,
)
def step(context, user_name, folder_name, items_number):
    total_items = webdav.get_folder_items_count(user_name, folder_name)
    test.compare(
        total_items, items_number, f'Folder should contain {items_number} items'
    )


@Given('user "{user}" has created folder "{folder_name}" in the server')
def step(context, user, folder_name):
    webdav.create_folder(user, folder_name)


@Given(
    'user "{user}" has uploaded file with content "{file_content}" to "{file_name}" in the server'
)
def step(context, user, file_content, file_name):
    webdav.create_file(user, file_name, file_content)


@When('the user clicks on the settings tab')
def step(context):
    Toolbar.open_settings_tab()


@When('user "{user}" uploads file with content "{file_content}" to "{file_name}" in the server')
def step(context, user, file_content, file_name):
    webdav.create_file(user, file_name, file_content)


@When('user "{user}" deletes the folder "{folder_name}" in the server')
def step(context, user, folder_name):
    webdav.delete_resource(user, folder_name)


@Given('user "{user}" has uploaded file "{file_name}" to "{destination}" in the server')
def step(context, user, file_name, destination):
    webdav.upload_file(user, file_name, destination)


@Then(
    'as "|any|" the content of file "|any|" in the server should match the content of local file "|any|"'
)
def step(context, user_name, server_file_name, local_file_name):
    raw_server_content = webdav.get_file_content(user_name, server_file_name)
    with tempfile.NamedTemporaryFile(suffix=Path(server_file_name).suffix) as tmp_file:
        if isinstance(raw_server_content, str):
            tmp_file.write(raw_server_content.encode('utf-8'))
        else:
            tmp_file.write(raw_server_content)
        server_content = get_document_content(tmp_file.name)
    local_content = get_document_content(get_file_for_upload(local_file_name))

    test.compare(
        server_content,
        local_content,
        f"Server file '{server_file_name}' differs from local file '{local_file_name}'",
    )


@Then(
    r'as "([^"].*)" following files should not exist in the server',
    regexp=True,
)
def step(context, user_name):
    for row in context.table[1:]:
        resource_name = row[0]
        test.compare(
            webdav.resource_exists(user_name, resource_name),
            False,
            f"Resource '{resource_name}' should not exist, but does",
        )


@Given('user "|any|" has uploaded the following files to the server')
def step(context, user):
    for row in context.table[1:]:
        file_name = row[0]
        file_content = row[1]
        webdav.create_file(user, file_name, file_content)


@Given('user "{user}" has sent the following resource share invitation:')
def step(context, user):
    resource_details = table_rows_hash(context.table)
    webdav.send_resource_share_invitation(
        user,
        resource_details['resource'],
        resource_details['sharee'],
        resource_details['permissionsRole'],
    )
