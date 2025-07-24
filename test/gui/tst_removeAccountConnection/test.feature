Feature: remove account connection
  As a user
  I want to remove my account
  So that I won't be using any client-UI services


    Scenario: remove an account connection
        Given user "Alice" has been created in the server with default attributes
        And user "Brian" has been created in the server with default attributes
        And the user has set up the following accounts with default settings:
            | Alice |
            | Brian |
        When the user removes the connection for user "Brian"
        Then the account with displayname "Brian Murphy" should not be displayed
        But the account with displayname "Alice Hansen" should be displayed


    Scenario: remove the only account connection
        Given user "Alice" has been created in the server with default attributes
        And user "Alice" has created folder "large-folder" in the server
        And user "Alice" has uploaded file with content "test content" to "testFile.txt" in the server
        And user "Alice" has set up a client with default settings
        When the user removes the connection for user "Alice"
        Then the settings tab should have the following options in the general section:
            | Start on Login |
        And the folder "large-folder" should exist on the file system
        And the file "testFile.txt" should exist on the file system
