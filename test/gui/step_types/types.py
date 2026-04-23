from behave import register_type
from parse import with_pattern


@with_pattern(r"file|folder")
def resource_type(text):
    return text


register_type(ResourceType=resource_type)
