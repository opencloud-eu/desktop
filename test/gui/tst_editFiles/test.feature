Feature: edit files
    As a user
    I want to be able to edit the file content
    So that I can modify and change file data

    Background:
        Given user "Alice" has been created in the server with default attributes


    Scenario: Modify original content of a file with special character
        Given user "Alice" has uploaded file with content "openCloud test text file 0" to "S@mpleFile!With,$pecial&Characters.txt" in the server
        And user "Alice" has set up a client with default settings
        When the user overwrites the file "S@mpleFile!With,$pecial&Characters.txt" with content "overwrite openCloud test text file"
        And the user waits for file "S@mpleFile!With,$pecial&Characters.txt" to be synced
        Then as "Alice" the file "S@mpleFile!With,$pecial&Characters.txt" should have the content "overwrite openCloud test text file" in the server


   Scenario: Modify original content of a file
        Given user "Alice" has set up a client with default settings
        When user "Alice" creates a file "testfile.txt" with the following content inside the sync folder
            """
            test content
            """
		And the user waits for file "testfile.txt" to be synced
        And the user overwrites the file "testfile.txt" with content "overwrite openCloud test text file"
        And the user waits for file "testfile.txt" to be synced
        Then as "Alice" the file "testfile.txt" should have the content "overwrite openCloud test text file" in the server


   Scenario: Replace and modify the content of a pdf file multiple times
     Given user "Alice" has set up a client with default settings
     And user "Alice" has created a file "file1.pdf" with the following content inside the sync folder
            """
            to be replaced
            """
     And user "Alice" has created a folder "Folder1" inside the sync folder
     And user "Alice" has created a file "Folder1/file1.pdf" with the following content inside the sync folder
            """
            to replace file inside root sync folder
            """
     When user "Alice" replaces file "file1.pdf" with "Folder1/file1.pdf" in the sync folder
     And the user waits for the files to sync
     And the user overwrites the file "file1.pdf" with content "overwritten to replace file inside root sync folder"
     And the user waits for file "file1.pdf" to be synced
     And the user overwrites the file "file1.pdf" with content "overwritten again to replace file inside root sync folder"
     And the user waits for file "file1.pdf" to be synced
     Then as "Alice" the file "file1.pdf" should have the content "overwritten again to replace file inside root sync folder" in the server
