import re
from urllib.parse import quote
import xml.etree.ElementTree as ET
import json

import helpers.api.http_helper as request
from helpers.api.utils import url_join
from helpers.ConfigHelper import get_config, PERMISSION_ROLES
from helpers.FilesHelper import get_file_for_upload
from helpers.SpaceHelper import get_personal_space_id, get_space_id, get_spaces_dav_endpoint
from helpers.api import provisioning


def get_webdav_url():
    return url_join(get_config('localBackendUrl'), 'remote.php/dav/files')


def get_beta_graph_url():
    return url_join(get_config("localBackendUrl"), "graph", "v1beta1")


def sanitize_url_resource_path(resource):
    resource = resource.strip('/').replace('\\', '/')
    encoded_resource_path = [quote(path, safe='') for path in resource.split('/')]
    return '/'.join(encoded_resource_path)


def get_resource_path(user, resource):
    encoded_resource_path = sanitize_url_resource_path(resource)
    return url_join(get_webdav_url(), user, encoded_resource_path)


def resource_exists(user, resource):
    response = request.propfind(get_resource_path(user, resource), user=user)
    if response.status_code == 207:
        return True
    if response.status_code == 404:
        return False
    raise AssertionError(f'Server returned status code: {response.status_code}')


def get_file_content(user, resource):
    response = request.get(get_resource_path(user, resource), user=user)

    request.assert_http_status(
        response, 200, f'Failed to get file content. Status: {response.status_code}'
    )
    if resource.lower().endswith('.txt'):
        return response.text
    return response.content


def get_folder_items(user, resource):
    """Get the root XML element from a PROPFIND request"""
    path = get_resource_path(user, resource)
    xml_response = request.propfind(path, user=user)

    request.assert_http_status(
        xml_response, 207, f'Failed to get resource properties: {xml_response.status_code}'
    )
    return ET.fromstring(xml_response.content)


def get_folder_items_count(user, folder_name):
    folder_name = folder_name.strip('/')
    if folder_name != '':
        folder_name += '/'
    root_element = get_folder_items(user, folder_name)
    total_items = 0

    for response_element in root_element:
        for href_element in response_element:
            if not href_element.text:
                continue
            is_folder_item = re.search(f'/{user}/{folder_name}.+', href_element.text)
            shares_folder = href_element.text.endswith(f'/{user}/Shares/')
            if href_element.tag == '{DAV:}href' and is_folder_item and not shares_folder:
                total_items += 1
    return str(total_items)


def create_folder(user, folder_name):
    url = get_resource_path(user, folder_name)
    response = request.mkcol(url, user=user)
    request.assert_http_status(
        response,
        201,
        f'Could not create the folder: {folder_name} for user {user}.'
        + f' Status: {response.status_code}',
    )


def create_file(user, file_name, contents):
    url = get_resource_path(user, file_name)
    response = request.put(url, body=contents, user=user)
    request.assert_http_status(
        response,
        [201, 204],
        f'Could not create file "{file_name}" for user {user}. Status: {response.status_code}',
    )


def upload_file(user, file_name, destination):
    file_path = get_file_for_upload(file_name)
    with open(file_path, 'rb') as file:
        contents = file.read()
    create_file(user, destination, contents)


def delete_resource(user, resource):
    url = get_resource_path(user, resource)
    response = request.delete(url, user=user)
    request.assert_http_status(
        response, 204, f'Could not delete folder "{resource}". Status: {response.status_code}'
    )


def get_resource_id(user, resource):
    root_element = get_folder_items(user, resource)

    # Finding the fileid elements using XPath with namespace notation
    fileid_elements = root_element.findall(".//{http://owncloud.org/ns}fileid")

    # The First element is the desired resource's file id
    if fileid_elements:
        return fileid_elements[0].text

    raise AssertionError(f'Could not find resource ID for {resource}')


def get_permission_role_id(role_name):
    """Get the permission role ID for a given role name"""
    if role_name not in PERMISSION_ROLES:
        raise ValueError(f"Unknown permission role: {role_name}")
    return PERMISSION_ROLES[role_name]


def get_user_id(username):
    """Get the user ID for a given username from created users"""
    if username in provisioning.created_users:
        return provisioning.created_users[username]['id']

    raise AssertionError(
        f'User {username} not found in created users. Make sure the user is created first.'
    )


def send_resource_share_invitation(user, resource, sharee, permission_role):
    space_id = get_personal_space_id(user)
    resource_id = get_resource_id(user, resource)
    recipient_user_id = get_user_id(sharee)
    role_id = get_permission_role_id(permission_role)

    url = url_join(get_beta_graph_url(), "drives", space_id, "items", resource_id, "invite")

    body = {
        "roles": [role_id],
        "recipients": [{"objectId": recipient_user_id, "@libre.graph.recipient.type": "user"}],
    }

    response = request.post(url, body=json.dumps(body), user=user)
    request.assert_http_status(
        response, [200, 201], f'Failed to send share invitation. Status: {response.status_code}'
    )


def patch_resource_properties(properties, resource_path=None, space_name=None, user=None):
    """
    Patch the properties of a folder or space.

    Args:
        user (str): The username.
        properties (dict): A dictionary of properties to patch.
                           Where the key is the property name and the value is the property value.
                           Example: {"ocrclone:integrity-id": "some-value"}
        resource_path (str): The path of the resource to patch.
        space_name (str): The name of the space.
    Raises:
        AssertionError: If the server response indicates failure.
    """
    if resource_path is None:
        resource_path = "/"

    if space_name is None:
        space_name = "Personal"

    encoded_resource_path = sanitize_url_resource_path(resource_path)
    space_id = get_space_id(space_name)

    url = url_join(get_spaces_dav_endpoint(), space_id, encoded_resource_path)

    props = []
    for key, value in properties.items():
        props.append(f'<{key}>{value}</{key}>')
    props = '\n'.join(props)

    body = f"""
    <d:propertyupdate xmlns:d="DAV:" xmlns:oc="http://owncloud.org/ns" xmlns:ocrclone="ocrclone">
        <d:set>
            <d:prop>
            {props}
            </d:prop>
        </d:set>
    </d:propertyupdate>
    """

    response = request.proppatch(url, body=body, user=user)
    request.assert_http_status(
        response,
        207,
        f'Could not patch properties. Resource: "{resource_path}", Space: "{space_name}"'
        + f'\n  Properties: {body}'
        + f'\n  Status: {response.status_code}',
    )

    # check if the property patch was successful
    xml = ET.fromstring(response.content)
    status = xml.find('.//{DAV:}propstat/{DAV:}status')
    if not status.text.endswith("200 OK"):
        raise AssertionError(
            f'Failed to patch properties. Resource: "{resource_path}", Space: "{space_name}"'
            + f'\n  Properties: {body}'
            + f'\n  Status: {status.text}'
        )
