Feature:  Logout users
  As a user
  I want to be able to login and logout of my account
  So that I can protect my work and identity and be assured of privacy

    Background:
        Given user "Alice" has been created in the server with default attributes


    Scenario: logging out
        Given user "Alice" has set up a client with default settings
        When the user "Alice" logs out using the client-UI
        Then user "Alice" should be signed out


    Scenario: login after logging out
        Given user "Alice" has set up a client with default settings
        And user "Alice" has logged out from the client-UI
        When user "Alice" logs in using the client-UI
        Then user "Alice" should be connected to the server
        When the user quits the client
        And the user starts the client
        Then user "Alice" should be connected to the server


    Scenario: login with incorrect after log out
      Given user "Alice" has set up a client with default settings
      And user "Alice" has logged out from the client-UI
      When user "ALice" opens login dialog
      And user "ALice" enters the password "invalid"
      Then the account with displayname "Alice Hansen" and host "%local_server_hostname%" should not be displayed
