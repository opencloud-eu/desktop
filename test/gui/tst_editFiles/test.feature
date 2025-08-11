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


  Scenario Outline: Replace and modify the content of a file multiple times
    Given user "Alice" has set up a client with default settings
    And the user has copied file "<initialFile>" from outside the sync folder to "/" in the sync folder
    When the user copies file "<updateFile1>" from outside the sync folder to "/<initialFile>" in the sync folder
    And the user waits for file "<initialFile>" to be synced
    And the user copies file "<updateFile2>" from outside the sync folder to "/<initialFile>" in the sync folder
    And the user waits for file "<initialFile>" to be synced
    And the user copies file "<updateFile3>" from outside the sync folder to "/<initialFile>" in the sync folder
    And the user waits for file "<initialFile>" to be synced
    Then as "Alice" the content of file "<initialFile>" in the server should match the content of local file "<updateFile3>"
    Examples:
      | initialFile | updateFile1  | updateFile2  | updateFile3  |
      | simple.pdf  | simple1.pdf  | simple2.pdf  | simple3.pdf  |
      | simple.docx | simple1.docx | simple2.docx | simple3.docx |
      | simple.xlsx | simple1.xlsx | simple2.xlsx | simple3.xlsx |

