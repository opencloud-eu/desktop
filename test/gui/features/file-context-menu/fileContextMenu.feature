Feature: File Explorer Context Menu
    As a user
    I want to access file and folder actions from the file explorer context menu
    So that I can quickly share, copy links, and open files in the web browser


    Scenario: Check the file explorer context menu of resources
        Given user "Alice" has been created in the server with default attributes
        And user "Alice" has created folder "mydocs" in the server
        And user "Alice" has uploaded file with content "lorem epsum" to "lorem.txt" in the server
        And user "Alice" has set up a client with default settings
        Then the following file explorer context menus should be available for file "lorem.txt" of user "Alice"
            | menu                              |
            | Share...                          |
            | Copy private link to clipboard    |
            | Show in web browser               |
            | Show file versions in web browser |
        When user "Alice" copies the private link of folder "mydocs" from the file explorer context menu
        Then the private link should be copied to the clipboard
