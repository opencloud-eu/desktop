from helpers.ConfigHelper import get_config
from helpers import UserHelper
import helpers.api.http_helper as request
from helpers.api.utils import url_join
import json
from PySide6.QtCore import QJsonDocument

created_groups = {}
created_users = {}


def get_graph_url():
    return url_join(get_config("localBackendUrl"), "graph", "v1.0")


def create_user(username):
    if username in UserHelper.test_users:
        user = UserHelper.test_users[username]
    else:
        user = UserHelper.User(
            username=username,
            displayname=username,
            email=f'{username}@mail.com',
            password=UserHelper.get_default_password(),
        )

    url = url_join(get_graph_url(), "users")
    body = json.dumps(
        {
            "onPremisesSamAccountName": user.username,
            "passwordProfile": {"password": user.password},
            "displayName": user.displayname,
            "mail": user.email,
        }
    )
    response = request.post(url, body)
    request.assert_http_status(response, 201, f"Failed to create user '{username}'")
    resp_object = response.json()

    # Check if the user already exists
    user_info = {
        "id": resp_object["id"],
        "username": user.username,
        "password": user.password,
        "displayname": resp_object["displayName"],
        "email": resp_object["mail"],
    }
    created_users[username] = user_info


def delete_created_users():
    for username, user_info in list(created_users.items()):
        user_id = user_info['id']
        url = url_join(get_graph_url(), "users", user_id)
        response = request.delete(url)
        request.assert_http_status(response, 204, "Failed to delete user")
        del created_users[username]


def get_capabilities():
    server_url = get_config('localBackendUrl')
    url = url_join(server_url, '/ocs/v1.php/cloud/capabilities?format=json')
    response = request.get(url)
    response_str = response.text
    response_doc = QJsonDocument.fromJson(response_str.encode("utf-8"))
    response_obj = response_doc.object()
    capabilities = response_obj.get('ocs').get('data').get('capabilities')
    return capabilities
