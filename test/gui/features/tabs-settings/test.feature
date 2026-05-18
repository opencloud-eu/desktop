Feature: Visually check all tabs
    As a user
    I want to visually check all tabs in client
    So that I can perform all the actions related to client

    @smoke
    Scenario: Tabs in toolbar looks correct
        Given user "Alice" has been created in the server with default attributes
        And user "Alice" has set up a client with default settings
        Then the toolbar should have the following tabs:
            | Add Account |
            | Activity    |
            | Settings    |
            | Quit        |

    @smoke
    Scenario: Verify various setting options in Settings tab
        Given user "Alice" has been created in the server with default attributes
        And user "Alice" has set up a client with default settings
        When the user opens the settings tab
        Then the settings tab should have the following options in the general section:
            | Start on Login |
        And the settings tab should have the following options in the advanced section:
            | Sync hidden files  |
            | Edit ignored files |
            | Log settings       |
        And the settings tab should have the following options in the network section:
            | Download Bandwidth |
            | Upload Bandwidth   |
        When the user opens the about dialog
        And the user closes the about dialog
