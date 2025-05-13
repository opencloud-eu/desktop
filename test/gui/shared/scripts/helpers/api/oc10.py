import json

import helpers.api.http_helper as request
from helpers.api.utils import url_join
from helpers.ConfigHelper import get_config


# store the apps initial state to restore it after the test
# E.g.:
#   "app_name": "enabled"
apps_initial_state = {}


def format_json(url):
    return url + "?format=json"


def get_ocs_url():
    return url_join(get_config("localBackendUrl"), "ocs", "v2.php")


def get_provisioning_url(*paths):
    return format_json(url_join(get_ocs_url(), "cloud", *paths))


def check_success_ocs_status(response):
    if response.text:
        ocs_data = json.loads(response.text)
        if ocs_data["ocs"]["meta"]["statuscode"] not in [100, 200]:
            raise AssertionError("Request failed." + response.text)
    else:
        raise ValueError(
            "No OCS response body. HTTP status was " + str(response.status_code)
        )


def create_user(username, password, displayname, email):
    url = get_provisioning_url("users")
    body = {
        "userid": username,
        "password": password,
        "displayname": displayname,
        "email": email,
    }
    response = request.post(url, body)
    request.assert_http_status(response, 200, f"Failed to create user '{username}'")
    check_success_ocs_status(response)

    # oc10 does not set display name while creating user,
    # so we need update the user info
    user_url = get_provisioning_url("users", username)
    display_name_body = {"key": "displayname", "value": displayname}
    display_name_response = request.put(user_url, display_name_body)
    request.assert_http_status(
        display_name_response, 200, f"Failed to update displayname of user '{username}'"
    )
    check_success_ocs_status(display_name_response)

    return {
        "id": username,
        "username": username,
        "password": password,
        "displayname": displayname,
        "email": email,
    }


def delete_user(user_id):
    url = get_provisioning_url("users", user_id)
    response = request.delete(url)
    request.assert_http_status(response, 200, f"Failed to delete user '{user_id}'")
    check_success_ocs_status(response)


def create_group(group_name):
    body = {"groupid": group_name}
    response = request.post(get_provisioning_url("groups"), body)
    request.assert_http_status(response, 200, f"Failed to create group '{group_name}'")
    check_success_ocs_status(response)
    return {"id": group_name}


def delete_group(group_id):
    url = get_provisioning_url("groups", group_id)
    response = request.delete(url)
    request.assert_http_status(response, 200, f"Failed to delete group '{group_id}'")
    check_success_ocs_status(response)


def add_user_to_group(user, group_name):
    url = get_provisioning_url("users", user, "groups")
    body = {"groupid": group_name}
    response = request.post(url, body)
    request.assert_http_status(
        response, 200, f"Failed to add user '{user}' to group '{group_name}'"
    )
    check_success_ocs_status(response)


def get_enabled_apps():
    url = get_provisioning_url("apps")
    response = request.get(f"{url}&filter=enabled")
    request.assert_http_status(response, 200, "Failed to get enabled apps")
    check_success_ocs_status(response)
    return json.loads(response.text)["ocs"]["data"]["apps"]


def enable_app(app_name):
    url = get_provisioning_url("apps", app_name)
    response = request.post(url)
    request.assert_http_status(response, 200, f"Failed to enable app '{app_name}'")
    check_success_ocs_status(response)


def disable_app(app_name):
    url = get_provisioning_url("apps", app_name)
    response = request.delete(url)
    request.assert_http_status(response, 200, f"Failed to disable app '{app_name}'")
    check_success_ocs_status(response)


def setup_app(app_name, action):
    if action.startswith("enable"):
        enable_app(app_name)
    elif action.startswith("disable"):
        disable_app(app_name)
    else:
        raise ValueError("Unknown action: " + action)

    if app_name not in apps_initial_state:
        enabled_apps = get_enabled_apps()
        if app_name in enabled_apps:
            apps_initial_state[app_name] = "enabled"
        apps_initial_state[app_name] = "disabled"


def restore_apps_state():
    for app_name, action in apps_initial_state.items():
        setup_app(app_name, action)
    apps_initial_state.clear()
