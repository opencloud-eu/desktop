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
     And the user has copied file "simple.pdf" from outside the sync folder to "/" in the sync folder
     When the user copies file "simple1.pdf" from outside the sync folder to "/simple.pdf" in the sync folder
     And the user waits for file "simple.pdf" to be synced
     And the user copies file "simple2.pdf" from outside the sync folder to "/simple.pdf" in the sync folder
     And the user waits for file "simple.pdf" to be synced
     And the user copies file "simple3.pdf" from outside the sync folder to "/simple.pdf" in the sync folder
     And the user waits for file "simple.pdf" to be synced
     Then as "Alice" the content of file "simple.pdf" in the server should match the content of local file "simple3.pdf"
