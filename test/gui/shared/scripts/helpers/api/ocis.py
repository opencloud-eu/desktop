import json

import helpers.api.http_helper as request
from helpers.api.utils import url_join
from helpers.ConfigHelper import get_config


def get_graph_url():
    return url_join(get_config("localBackendUrl"), "graph", "v1.0")


def create_group(group_name):
    url = url_join(get_graph_url(), "groups")
    body = json.dumps({"displayName": group_name})
    response = request.post(url, body)
    request.assert_http_status(response, 201, f"Failed to create group '{group_name}'")
    resp_object = response.json()
    return {
        "id": resp_object["id"],
        "displayName": resp_object["displayName"],
    }


def delete_group(group_id):
    url = url_join(get_graph_url(), "groups", group_id)
    response = request.delete(url)
    request.assert_http_status(response, 200, f"Failed to delete group '{group_id}'")


def get_group_id(group_name):
    url = url_join(get_graph_url(), "groups", group_name)
    response = request.get(url)
    request.assert_http_status(response, 200, f"Failed to get group '{group_name}'")
    resp_object = response.json()
    return resp_object["id"]


def get_user_id(user):
    url = url_join(get_graph_url(), "users", user)
    response = request.get(url)
    request.assert_http_status(response, 200, f"Failed to get user '{user}'")
    resp_object = response.json()
    return resp_object["id"]


def add_user_to_group(user, group_name):
    url = url_join(
        get_graph_url(), "groups", get_group_id(group_name), "members", "$ref"
    )
    data = url_join(get_graph_url(), "users", get_user_id(user))
    body = json.dumps({"@odata.id": data})
    response = request.post(url, body)
    request.assert_http_status(
        response, 204, f"Failed to add user '{user}' to group '{group_name}'"
    )


def create_user(username, password, displayname, email):
    url = url_join(get_graph_url(), "users")
    body = json.dumps(
        {
            "onPremisesSamAccountName": username,
            "passwordProfile": {"password": password},
            "displayName": displayname,
            "mail": email,
        }
    )
    response = request.post(url, body)
    request.assert_http_status(response, 201, f"Failed to create user '{username}'")
    resp_object = response.json()
    return {
        "id": resp_object["id"],
        "username": username,
        "password": password,
        "displayname": resp_object["displayName"],
        "email": resp_object["mail"],
    }


def delete_user(user_id):
    url = url_join(get_graph_url(), "users", user_id)
    response = request.delete(url)
    request.assert_http_status(response, 204, "Failed to delete user")
