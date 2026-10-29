# disable ruff lint on this file as it contains code that are taken from other sources
# ruff: noqa

############################################################################################################
# This is a wrapper around the original atspi webdriver that allows for modifications and extensions
# without altering the original source code.
#
# Implementation: https://github.com/KDE/selenium-webdriver-at-spi
# See MODIFY START and MODIFY END comments for the changes made.
############################################################################################################

import importlib.util
import sys
import os
import urllib.request
import pyatspi
from datetime import datetime, timedelta
from pathlib import Path

TEST_DIR = Path(__file__).parent.parent
DRIVER_DIR = TEST_DIR / "__webdriver"
DRIVER_FILE_PATH = DRIVER_DIR / "atspi-webdriver.py"

if not DRIVER_FILE_PATH.exists():
    app_roles_file = "app_roles.py"
    driver_url = "https://raw.githubusercontent.com/KDE/selenium-webdriver-at-spi"

    # get driver version
    env_file = TEST_DIR / ".woodpecker.env"
    driver_version = "master"  # default to master
    if env_file.exists():
        with open(env_file) as f:
            for line in f:
                if line.strip() and not line.startswith("#"):
                    key, value = line.strip().split("=", 1)
                    value = value.strip('"')
                    if key == "ATSPI_WEBDRIVER_VERSION":
                        driver_version = value

    os.makedirs(DRIVER_DIR, exist_ok=True)
    # get driver file
    urllib.request.urlretrieve(
        f'{driver_url}/{driver_version}/selenium-webdriver-at-spi.py',
        DRIVER_FILE_PATH,
    )
    # get app roles file
    urllib.request.urlretrieve(
        f'{driver_url}/{driver_version}/{app_roles_file}',
        DRIVER_DIR / app_roles_file,
    )

# Load atspi-webdriver.py file as a module
sys.path.insert(0, str(DRIVER_DIR))
spec = importlib.util.spec_from_file_location("atspi-webdriver", DRIVER_FILE_PATH)
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)

app = base.app
sessions = base.sessions


# [INFO] The function is taken from the original implementation in atspi-webdriver.py
# Apart from the modification listed below, the function remains the same.
# See MODIFY START and MODIFY END comments for the changes made.
def locator(session, strategy, selector, start, findAll=False):
    end_time = datetime.now() + timedelta(milliseconds=session.timeouts['implicit'])
    results = []

    while datetime.now() < end_time:
        if strategy == 'xpath':
            doc = base._createNode2(start, None)
            for c in doc.xpath(selector):
                path = [int(x) for x in c.get('path').split()]

                ##### MODIFY START #####
                # path is relative to the start node, not the app root
                item = start
                ##### MODIFY END #####

                for i in path:
                    item = item[i]
                if c.get('name') != item.name or c.get('description') != item.description:
                    return []
                results.append(item)
        else:
            # In a thrilling turn of events [push button | foo] became [button | foo] as of at-spi 2.53. Add compatibility
            if base.requires_button_compat:
                if '[push button |' in selector:
                    print(
                        "    --> [push button | foo] is deprecated. Please port to [button | foo]"
                    )
                    selector = selector.replace('[push button |', '[button |')

            # TODO can I switch this in python +++ raise on unmapped strategy
            pred = None
            if strategy == 'accessibility id':

                def pred(x):
                    return x.accessibleId.endswith(selector) and (
                        x.getState().contains(pyatspi.STATE_VISIBLE)
                        and x.getState().contains(pyatspi.STATE_SENSITIVE)
                    )
            # pyatspi strings "[ roleName | name ]"
            elif strategy == 'class name':

                def pred(x):
                    return str(x) == selector and (
                        x.getState().contains(pyatspi.STATE_VISIBLE)
                        or x.getState().contains(pyatspi.STATE_SENSITIVE)
                    )
            elif strategy == 'name':

                def pred(x):
                    return x.name == selector and (
                        x.getState().contains(pyatspi.STATE_VISIBLE)
                        or x.getState().contains(pyatspi.STATE_SENSITIVE)
                    )
            elif strategy == 'description':

                def pred(x):
                    return x.description == selector and (
                        x.getState().contains(pyatspi.STATE_VISIBLE)
                        or x.getState().contains(pyatspi.STATE_SENSITIVE)
                    )

            # there are also id and accessibleId but they seem not ever set. Not sure what to make of that :shrug:
            if findAll:
                accessible = pyatspi.findAllDescendants(start, pred)
                if accessible:
                    results += accessible
            else:
                accessible = pyatspi.findDescendant(start, pred)
                if accessible:
                    results.append(accessible)
        if len(results) > 0:
            break

    return results


base.locator = locator
