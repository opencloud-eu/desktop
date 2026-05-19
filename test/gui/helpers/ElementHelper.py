def get_element_center_xy(element):
    rect = element.rect
    x = int(rect['x'] + (rect['width'] // 2))
    y = int(rect['y'] + (rect['height'] // 2))
    return x, y
