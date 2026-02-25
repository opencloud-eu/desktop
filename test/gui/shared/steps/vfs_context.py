from helpers.FilesHelper import get_file_size_on_disk, get_file_size
from helpers.SetupClientHelper import get_resource_path
from helpers.SyncHelper import perform_file_explorer_vfs_action


@Then('the placeholder file "|any|" should exist on the file system')
def step(context, file_name):
    resource_path = get_resource_path(file_name)
    size_on_disk = get_file_size_on_disk(resource_path)
    test.compare(
        size_on_disk, 0, f"Size of the placeholder on the disk is: '{size_on_disk}'"
    )


@Then('the file "|any|" should be downloaded')
def step(context, file_name):
    resource_path = get_resource_path(file_name)
    size_on_disk = get_file_size_on_disk(resource_path)
    file_size = get_file_size(resource_path)
    test.compare(
        size_on_disk,
        file_size,
        f"File size is equal to its size on disk",
    )


@When(r'user "([^"]*)" marks (?:file|folder) "([^"]*)" as (online-only|available-locally) from the file explorer', regexp=True)
def step(context, user, resource, action):
    resource_path = get_resource_path(resource, user)
    perform_file_explorer_vfs_action(resource_path, action)
