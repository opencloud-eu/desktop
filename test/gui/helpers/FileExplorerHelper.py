from helpers.SyncHelper import get_socket_connection
from helpers.AppHelper import app


def get_file_context_menus(resource):
    menu_items = []
    socket_connect = get_socket_connection()
    socket_connect.sendCommand(f'GET_MENU_ITEMS:{resource}\n')
    if not socket_connect.read_socket_data_with_timeout(0.1):
        return menu_items
    for line in socket_connect.get_available_responses():
        if line == 'GET_MENU_ITEMS:END':
            break
        if line.startswith('MENU_ITEM:'):
            item = line.split("::")[1]
            menu_items.append(item)
    return menu_items


def copy_private_link(resource):
    socket_connect = get_socket_connection()
    socket_connect.sendCommand(f'COPY_PRIVATE_LINK:{resource}\n')


def get_clipboard_content():
    return app().get_clipboard_text()
