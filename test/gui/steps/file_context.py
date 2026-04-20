from behave import when as When, register_type
import parse

import os
import shutil

from helpers.SyncHelper import wait_for_client_to_be_ready
from helpers.FilesHelper import sanitize_path
from helpers.SetupClientHelper import get_resource_path

@parse.with_pattern(r"file|folder")
def parse_resource_type(text):
    return text

register_type(ResourceType=parse_resource_type)

def deleteResource(resource, resource_type):
    resource_path = sanitize_path(get_resource_path(resource))
    if resource_type == 'file':
        os.remove(resource_path)
    else:
        shutil.rmtree(resource_path)


@When('the user deletes the {resource_type:ResourceType} "{resource_name}"')
def step(context, resource_type, resource_name):
    wait_for_client_to_be_ready()
    print(f"Deleting {resource_type} '{resource_name}'")
    deleteResource(resource_name, resource_type)