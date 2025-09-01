Feature: Project spaces
    As a user
    I want to sync project space
    So that I can do view and manage the space

    Background:
        Given user "Alice" has been created in the server with default attributes
        And the administrator has created a space "Project101"


    Scenario: User with Viewer role can open the file
        Given the administrator has created a folder "planning" in space "Project101"
        And the administrator has uploaded a file "testfile.txt" with content "some content" inside space "Project101"
        And the administrator has added user "Alice" to space "Project101" with role "viewer"
        And user "Alice" has set up a client with space "Project101"
        Then user "Alice" should be able to open the file "testfile.txt" on the file system
        And as "Alice" the file "testfile.txt" should have content "some content" on the file system


    Scenario: User with Viewer role cannot edit the file
        Given the administrator has created a folder "planning" in space "Project101"
        And the administrator has uploaded a file "testfile.txt" with content "some content" inside space "Project101"
        And the administrator has added user "Alice" to space "Project101" with role "viewer"
        And user "Alice" has set up a client with space "Project101"
        Then user "Alice" should not be able to edit the file "testfile.txt" on the file system
        And as "Alice" the file "testfile.txt" in the space "Project101" should have content "some content" in the server


    Scenario: User with Editor role can edit the file
        Given the administrator has created a folder "planning" in space "Project101"
        And the administrator has uploaded a file "testfile.txt" with content "some content" inside space "Project101"
        And the administrator has added user "Alice" to space "Project101" with role "editor"
        And user "Alice" has set up a client with space "Project101"
        When the user overwrites the file "testfile.txt" with content "some content edited"
        And the user waits for file "testfile.txt" to be synced
        Then as "Alice" the file "testfile.txt" in the space "Project101" should have content "some content edited" in the server


    Scenario: User with Manager role can add files and folders
        Given the administrator has added user "Alice" to space "Project101" with role "manager"
        And user "Alice" has set up a client with space "Project101"
        When user "Alice" creates a file "localFile.txt" with the following content inside the sync folder
            """
            test content
            """
        And user "Alice" creates a folder "localFolder" inside the sync folder
        And the user waits for the files to sync
        Then as "Alice" the file "localFile.txt" in the space "Project101" should have content "test content" in the server
        And as "Alice" the space "Project101" should have folder "localFolder" in the server


    Scenario: User with Editor role can rename a file
        Given the administrator has uploaded a file "testfile.txt" with content "some content" inside space "Project101"
        And the administrator has added user "Alice" to space "Project101" with role "editor"
        And user "Alice" has set up a client with space "Project101"
        When the user renames a file "testfile.txt" to "renamedFile.txt"
        And the user waits for file "renamedFile.txt" to be synced
        Then as "Alice" the space "Project101" should have file "renamedFile.txt" in the server
        And as "Alice" the file "renamedFile.txt" in the space "Project101" should have content "some content" in the server


    Scenario: Remove folder sync connection (Project Space)
        Given the administrator has uploaded a file "testfile.txt" with content "some content" inside space "Project101"
        And the administrator has added user "Alice" to space "Project101" with role "manager"
        And user "Alice" has set up a client with space "Project101"
        When the user removes the folder sync connection
        Then the sync folder list should be empty
        But the file "testfile.txt" should exist on the file system

    @issue-435
    Scenario: User with Viewer role cannot create resource
        Given the administrator has added user "Alice" to space "Project101" with role "viewer"
        And user "Alice" has set up a client with space "Project101"
        When user "Alice" creates a folder "simple-folder" inside the sync folder
        Then the following error message should appear in the client
            """
            simple-folder: Not allowed because you don't have permission to add subfolders to that folder
            """
        When the user clicks on the activity tab
        And the user selects "Not Synced" tab in the activity
        Then the following activities should be displayed in not synced table
            | resource      | status      | account                              |
            | simple-folder | Blacklisted | Alice Hansen@%local_server_hostname% |

    @issue-435
    Scenario: Sharee with Editor role deletes the shared resource
        Given user "Brian" has been created in the server with default attributes
        And user "Alice" has created folder "simple-folder" in the server
        And user "Alice" has uploaded file with content "test content" to "simple-folder/uploaded-lorem.txt" in the server
        And user "Alice" has sent the following resource share invitation:
            | resource        | simple-folder |
            | sharee          | Brian         |
            | permissionsRole | Editor        |
        And user "Brian" has set up a client with space "Shares"
        When user "Brian" deletes the folder "Shares/simple-folder" in the server
        And the user waits for the files to sync
        Then the folder "simple-folder" should not exist on the file system
