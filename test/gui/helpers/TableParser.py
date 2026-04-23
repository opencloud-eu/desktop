from behave.model import Table


def table_raw(table: Table):
    """
    Args:
        table (Table): Behave Table object.
    Returns:
        list: List of lists (including header row) - each row is a list of cells.

    Example:
        | header1 | header2 | header3 |
        | value1  | value2  | value3  |
    Output:
        [
            ['header1', 'header2', 'header3'],
            ['value1', 'value2', 'value3'],
        ]
    """
    data_table = [table.headings]
    data_table.extend(table_rows(table))
    return data_table


def table_rows(table: Table):
    """
    Args:
        table (Table): Behave Table object.
    Returns:
        list: List of lists (excluding header row) - each row is a list of cells.

    Example:
        | header1 | header2 | header3 |
        | value1  | value2  | value3  |
    Output:
        [
            ['value1', 'value2', 'value3'],
        ]
    """
    data_table = []
    for row in table:
        data_table.append(row.cells)
    return data_table


def table_rows_hash(table: Table):
    """
    Args:
        table (Table): Behave Table object. Table MUST have exactly 2 columns.
    Returns:
        dict: Dictionary where keys are from the first column and values are from the second column.
    Raises:
        ValueError: If the table does not have exactly 2 columns.

    Example:
        | key1    | value1  |
        | key2    | value2  |
        | key3    | value3  |
    Output:
        {
            'key1': 'value1',
            'key2': 'value2',
            'key3': 'value3',
        }
    """
    if len(table.headings) != 2:
        raise ValueError(
            "table_rows_hash() can only be called on a data table where all rows have exactly two columns."
        )

    data_table = {
        table.headings[0]: table.headings[1],
    }
    for row in table:
        data_table[row[0]] = row[1]
    return data_table


def table_hashes(table: Table):
    """
    Args:
        table (Table): Behave Table object.
    Returns:
        list: List of dictionaries, where each dictionary represents a row with keys from the header and values from the corresponding cells.

    Example:
        | key1    | key2    | key3   |
        | value1  | value2  | value3 |
        | value4  | value5  | value6 |
    Output:
        [
            {'key1': 'value1', 'key2': 'value2', 'key3': 'value3'},
            {'key1': 'value4', 'key2': 'value5', 'key3': 'value6'},
        ]
    """
    data_table = []
    for row in table:
        row_dict = {}
        for idx, heading in enumerate(table.headings):
            row_dict[heading] = row.cells[idx]
        data_table.append(row_dict)
    return data_table
