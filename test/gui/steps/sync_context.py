from behave import when as When
from helpers.SyncHelper import wait_for_resource_to_sync
from helpers.SetupClientHelper import get_resource_path

@When('the user waits for the files to sync')
def step(context):
    wait_for_resource_to_sync(get_resource_path('/'))