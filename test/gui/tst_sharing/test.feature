@skipOnOCIS @pr-10241
Feature: Sharing
    As a user
    I want to share files and folders with other users
    So that those users can access the files and folders

    Background:
        Given user "Alice" has been created in the server with default attributes


    Scenario: sharee edits content of files shared by sharer
        Given user "Alice" has created folder "simple-folder" in the server
        And user "Alice" has uploaded file with content "file inside a folder" to "simple-folder/textfile.txt" in the server
        And user "Alice" has uploaded file with content "file in the root" to "textfile.txt" in the server
        And user "Brian" has been created in the server with default attributes
        And user "Alice" has shared folder "simple-folder" in the server with user "Brian" with "all" permissions
        And user "Alice" has shared file "textfile.txt" in the server with user "Brian" with "all" permissions
        And user "Brian" has set up a client with default settings
        When the user overwrites the file "textfile.txt" with content "overwrite file in the root"
        And the user waits for file "textfile.txt" to be synced
        And the user overwrites the file "simple-folder/textfile.txt" with content "overwrite file inside a folder"
        And the user waits for file "simple-folder/textfile.txt" to be synced
        Then as "Brian" the file "simple-folder/textfile.txt" should have the content "overwrite file inside a folder" in the server
        And as "Brian" the file "textfile.txt" should have the content "overwrite file in the root" in the server
        And as "Alice" the file "simple-folder/textfile.txt" should have the content "overwrite file inside a folder" in the server
        And as "Alice" the file "textfile.txt" should have the content "overwrite file in the root" in the server


    Scenario: sharee tries to edit content of files shared without write permission
        Given user "Alice" has created folder "Parent" in the server
        And user "Alice" has uploaded file with content "file inside a folder" to "Parent/textfile.txt" in the server
        And user "Alice" has uploaded file with content "file in the root" to "textfile.txt" in the server
        And user "Brian" has been created in the server with default attributes
        And user "Alice" has shared folder "Parent" in the server with user "Brian" with "read" permissions
        And user "Alice" has shared file "textfile.txt" in the server with user "Brian" with "read" permissions
        And user "Brian" has set up a client with default settings
        When the user tries to overwrite the file "Parent/textfile.txt" with content "overwrite file inside a folder"
        And the user tries to overwrite the file "textfile.txt" with content "overwrite file in the root"
        And the user waits for file "textfile.txt" to have sync error
        Then as "Brian" the file "Parent/textfile.txt" should have the content "file inside a folder" in the server
        And as "Brian" the file "textfile.txt" should have the content "file in the root" in the server
        And as "Alice" the file "Parent/textfile.txt" should have the content "file inside a folder" in the server
        And as "Alice" the file "textfile.txt" should have the content "file in the root" in the server


    Scenario: sharee creates a file and a folder inside a shared folder
        Given user "Alice" has created folder "Parent" in the server
        And user "Brian" has been created in the server with default attributes
        And user "Alice" has shared folder "Parent" in the server with user "Brian" with "all" permissions
        And user "Brian" has set up a client with default settings
        When user "Brian" creates a file "Parent/localFile.txt" with the following content inside the sync folder
            """
            test content
            """
        And user "Brian" creates a folder "Parent/localFolder" inside the sync folder
        And the user waits for file "Parent/localFile.txt" to be synced
        And the user waits for folder "Parent/localFolder" to be synced
        Then as "Brian" file "Parent/localFile.txt" should exist in the server
        And as "Brian" folder "Parent/localFolder" should exist in the server
        And as "Alice" file "Parent/localFile.txt" should exist in the server
        And as "Alice" folder "Parent/localFolder" should exist in the server


    Scenario: sharee tries to create a file and a folder inside a shared folder without write permission
        Given user "Alice" has created folder "Parent" in the server
        And user "Brian" has been created in the server with default attributes
        And user "Alice" has shared folder "Parent" in the server with user "Brian" with "read" permissions
        And user "Brian" has set up a client with default settings
        When user "Brian" creates a file "Parent/localFile.txt" with the following content inside the sync folder
            """
            test content
            """
        And user "Brian" creates a folder "Parent/localFolder" inside the sync folder
        And the user waits for file "Parent/localFile.txt" to have sync error
        And the user waits for folder "Parent/localFolder" to have sync error
        Then as "Brian" file "Parent/localFile.txt" should not exist in the server
        And as "Brian" folder "Parent/localFolder" should not exist in the server
        And as "Alice" file "Parent/localFile.txt" should not exist in the server
        And as "Alice" folder "Parent/localFolder" should not exist in the server


    Scenario: sharee renames the shared file and folder
        Given user "Alice" has uploaded file with content "ownCloud test text file 0" to "textfile.txt" in the server
        And user "Alice" has created folder "FOLDER" in the server
        And user "Brian" has been created in the server with default attributes
        And user "Alice" has shared file "textfile.txt" in the server with user "Brian" with "all" permissions
        And user "Alice" has shared file "FOLDER" in the server with user "Brian" with "all" permissions
        And user "Brian" has set up a client with default settings
        When the user renames a file "textfile.txt" to "lorem.txt"
        And the user renames a folder "FOLDER" to "PARENT"
        And the user waits for folder "PARENT" to be synced
        And the user waits for file "lorem.txt" to be synced
        Then as "Brian" folder "FOLDER" should not exist in the server
        And as "Brian" file "textfile.txt" should not exist in the server
        And as "Brian" folder "PARENT" should exist in the server
        And as "Brian" file "lorem.txt" should exist in the server
        # File/folder will not change for Alice
        And as "Alice" folder "FOLDER" should exist in the server
        And as "Alice" file "textfile.txt" should exist in the server
        And as "Alice" folder "PARENT" should not exist in the server
        And as "Alice" file "lorem.txt" should not exist in the server

    @issue-9439 @issue-11102
    Scenario: sharee deletes a file and folder shared by sharer
        Given user "Alice" has uploaded file with content "ownCloud test text file 0" to "textfile.txt" in the server
        And user "Alice" has created folder "Folder" in the server
        And user "Brian" has been created in the server with default attributes
        And user "Alice" has shared file "textfile.txt" in the server with user "Brian" with "all" permissions
        And user "Alice" has shared file "Folder" in the server with user "Brian" with "all" permissions
        And user "Brian" has set up a client with default settings
        When the user deletes the file "textfile.txt"
        And the user deletes the folder "Folder"
        And the user waits for the files to sync
        Then as "Brian" file "textfile.txt" should not exist in the server
        And as "Brian" folder "Folder" should not exist in the server
        And as "Alice" file "textfile.txt" should exist in the server
        And as "Alice" folder "Folder" should exist in the server

    @issue-11102
    Scenario: sharee tries to delete shared file and folder without permissions
        Given user "Alice" has uploaded file with content "ownCloud test text file 0" to "textfile.txt" in the server
        And user "Alice" has created folder "Folder" in the server
        And user "Brian" has been created in the server with default attributes
        And user "Alice" has shared file "textfile.txt" in the server with user "Brian" with "read" permissions
        And user "Alice" has shared file "Folder" in the server with user "Brian" with "read" permissions
        And user "Brian" has set up a client with default settings
        When the user deletes the file "textfile.txt"
        And the user deletes the folder "Folder"
        And the user waits for the files to sync
        # Sharee can delete (means unshare) the file shared with read permission
        Then as "Brian" file "textfile.txt" should not exist in the server
        And as "Brian" folder "Folder" should not exist in the server
        And as "Alice" file "textfile.txt" should exist in the server
        And as "Alice" folder "Folder" should exist in the server

