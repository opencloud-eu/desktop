Feature: adding accounts
    As a user
    I want to be able join multiple opencloud servers to the client
    So that I can sync data with various organisations

    Background:
        Given user "Alice" has been created in the server with default attributes


    Scenario: Check default options in advanced configuration
        Given the user has started the client
        And the user has entered the following account information:
            | server   | %local_server% |
            | user     | Alice          |
            | password | 1234           |
        When the user opens the advanced configuration
        Then the download everything option should be selected by default for Linux
        And the user should be able to choose the local download directory


    Scenario: Adding normal Account
        Given the user has started the client
        When the user adds the following account:
            | server   | %local_server% |
            | user     | Alice          |
            | password | 1234           |
        Then the account with displayname "Alice Hansen" should be displayed


    Scenario: Adding multiple accounts
        Given user "Brian" has been created in the server with default attributes
        And user "Alice" has set up a client with default settings
        When the user opens the add-account dialog
        And the user adds the following account:
            | server   | %local_server% |
            | user     | Brian          |
            | password | AaBb2Cc3Dd4    |
        Then "Brian Murphy" account should be opened
        And the account with displayname "Alice Hansen" should be displayed
        And the account with displayname "Brian Murphy" should be displayed


    Scenario: Adding account with self signed certificate for the first time
        Given the user has started the client
        When the user adds the server "%local_server%"
        And the user accepts the certificate
        Then credentials wizard should be visible
        When the user adds the following account:
            | user     | Alice |
            | password | 1234  |
        Then "Alice Hansen" account should be opened


    Scenario: Add space manually from sync connection window
        Given user "Alice" has created folder "simple-folder" in the server
        And the user has started the client
        And the user has entered the following account information:
            | server   | %local_server% |
            | user     | Alice          |
            | password | 1234           |
        When the user selects manual sync folder option in advanced section
        And the user syncs the "Personal" space
        Then the folder "simple-folder" should exist on the file system


    Scenario: Check for suffix when sync path exists
        Given the user has created folder "OpenCloud" in the default home path
        And the user has started the client
        And the user has entered the following account information:
            | server | %local_server% |
        When the user adds the following user credentials:
            | user     | Alice |
            | password | 1234  |
        And the user opens the advanced configuration
        Then the default local sync path should contain "%home%/OpenCloud (2)" in the configuration wizard
        When the user selects download everything option in advanced section
        Then the button to open sync connection wizard should be disabled
