from helpers.SetupClientHelper import get_resource_path
from helpers.SyncHelper import perform_file_explorer_vfs_action
from helpers.VFSFileHelper import is_placeholder_resource, is_file_downloaded


@Then('the placeholder file "|any|" should exist on the file system')
def step(context, file_name):
    resource_path = get_resource_path(file_name)
    test.compare(is_placeholder_resource(resource_path), True, f"File is a placeholder")


@Then('the file "|any|" should be downloaded')
def step(context, file_name):
    resource_path = get_resource_path(file_name)
    test.compare(is_file_downloaded(resource_path), True, f"File is downloaded")


@When(r'user "([^"]*)" marks (?:file|folder) "([^"]*)" as "(Free up space|Always keep on this device)" from the file explorer', regexp=True)
def step(context, user, resource, action):
    resource_path = get_resource_path(resource, user)
    perform_file_explorer_vfs_action(resource_path, action)
