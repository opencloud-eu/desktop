@skipOnLinux
Feature: VFS support
    As a user
    I want to sync files with vfs
    So that I can decide which files to download


    Scenario: Default VFS sync
        Given user "Alice" has been created in the server with default attributes
        And user "Alice" has uploaded file with content "openCloud" to "testFile.txt" in the server
        And user "Alice" has created folder "parent" in the server
        And user "Alice" has uploaded file with content "some contents" to "parent/lorem.txt" in the server
        And user "Alice" has set up a client with default settings
        Then the placeholder of file "testFile.txt" should exist on the file system
        And the placeholder of file "parent/lorem.txt" should exist on the file system
        When user "Alice" reads the content of file "parent/lorem.txt"
        Then the file "parent/lorem.txt" should be downloaded
        And the placeholder of file "testFile.txt" should exist on the file system


    Scenario: Copy placeholder file
        Given user "Alice" has been created in the server with default attributes
        And user "Alice" has uploaded file with content "sample file" to "sampleFile.txt" in the server
        And user "Alice" has uploaded file with content "lorem file" to "lorem.txt" in the server
        And user "Alice" has uploaded file with content "test file" to "testFile.txt" in the server
        And user "Alice" has created folder "Folder" in the server
        And user "Alice" has set up a client with default settings
        Then the placeholder of file "lorem.txt" should exist on the file system
        And the placeholder of file "sampleFile.txt" should exist on the file system
        And the placeholder of file "testFile.txt" should exist on the file system
        When user "Alice" copies file "sampleFile.txt" to temp folder
        And the user copies file "lorem.txt" into folder "Folder"
        And the user copies file "testFile.txt" into the same folder
        And the user waits for file "Folder/lorem.txt" to be synced
        Then the file "sampleFile.txt" should be downloaded
        And the file "Folder/lorem.txt" should be downloaded
        And the file "lorem.txt" should be downloaded
        And the file "testFile.txt" should be downloaded
        And the file "testFile (Copy).txt" should be downloaded
        And as "Alice" file "Folder/lorem.txt" should exist in the server
        And as "Alice" file "lorem.txt" should exist in the server
        And as "Alice" file "sampleFile.txt" should exist in the server
        And as "Alice" file "testFile.txt" should exist in the server
        And as "Alice" file "testFile (Copy).txt" should exist in the server


    Scenario: Move placeholder file
        Given user "Alice" has been created in the server with default attributes
        And user "Alice" has uploaded file with content "lorem file" to "lorem.txt" in the server
        And user "Alice" has uploaded file with content "some contents" to "sampleFile.txt" in the server
        And user "Alice" has created folder "Folder" in the server
        And user "Alice" has set up a client with default settings
        When user "Alice" moves file "lorem.txt" to "Folder" in the sync folder
        And user "Alice" moves file "sampleFile.txt" to the temp folder
        And the user waits for file "Folder/lorem.txt" to be synced
        Then the placeholder of file "Folder/lorem.txt" should exist on the file system
        And as "Alice" file "Folder/lorem.txt" should exist in the server
        And as "Alice" file "lorem.txt" should not exist in the server
        And as "Alice" file "sampleFile.txt" should not exist in the server


    Scenario: File explorer actions
        Given user "Alice" has been created in the server with default attributes
        And user "Alice" has uploaded file with content "openCloud" to "testFile.txt" in the server
        And user "Alice" has created folder "parent" in the server
        And user "Alice" has uploaded file with content "some contents" to "parent/lorem.txt" in the server
        And user "Alice" has set up a client with default settings
        Then the placeholder of file "testFile.txt" should exist on the file system
        When user "Alice" marks file "testFile.txt" as available-locally from the file explorer
        And the user waits for file "testFile.txt" to be synced
        Then the file "testFile.txt" should be downloaded
        When user "Alice" marks file "testFile.txt" as online-only from the file explorer
        And the user waits for file "testFile.txt" to be synced
        Then the placeholder of file "testFile.txt" should exist on the file system
