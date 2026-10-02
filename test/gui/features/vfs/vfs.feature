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
        Then the placeholder file "testFile.txt" should exist on the file system
        And the placeholder file "parent/lorem.txt" should exist on the file system
        When user "Alice" reads the content of file "parent/lorem.txt"
        Then the file "parent/lorem.txt" should be downloaded
        And the placeholder file "testFile.txt" should exist on the file system


    Scenario: Copy placeholder file
        Given user "Alice" has been created in the server with default attributes
        And user "Alice" has uploaded file with content "sample file" to "sampleFile.txt" in the server
        And user "Alice" has uploaded file with content "lorem file" to "lorem.txt" in the server
        And user "Alice" has uploaded file with content "test file" to "testFile.txt" in the server
        And user "Alice" has created folder "Folder" in the server
        And user "Alice" has set up a client with default settings
        Then the placeholder file "lorem.txt" should exist on the file system
        And the placeholder file "sampleFile.txt" should exist on the file system
        And the placeholder file "testFile.txt" should exist on the file system
        When user "Alice" copies file "sampleFile.txt" to temp folder
        And the user copies file "lorem.txt" into folder "Folder"
        And the user copies file "testFile.txt" into the same directory
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
        Then the placeholder file "Folder/lorem.txt" should exist on the file system
        And as "Alice" file "Folder/lorem.txt" should exist in the server
        And as "Alice" file "lorem.txt" should not exist in the server
        And as "Alice" file "sampleFile.txt" should not exist in the server


    Scenario: Hydration and dehydration of files via file explorer
        Given user "Alice" has been created in the server with default attributes
        And user "Alice" has uploaded file with content "test content" to "testFile.txt" in the server
        And user "Alice" has uploaded file with content "test content" to "simple.txt" in the server
        And user "Alice" has uploaded file with content "test content" to "large.txt" in the server
        And user "Alice" has created folder "parent" in the server
        And user "Alice" has uploaded file with content "test content" to "parent/lorem.txt" in the server
        And user "Alice" has uploaded file with content "test content" to "parent/epsum.txt" in the server
        And user "Alice" has set up a client with default settings
        Then the placeholder file "testFile.txt" should exist on the file system
        And the placeholder file "simple.txt" should exist on the file system
        And the placeholder file "large.txt" should exist on the file system
        And the placeholder file "parent/lorem.txt" should exist on the file system
        And the placeholder file "parent/epsum.txt" should exist on the file system

        # Hydrate some files by reading the content
        When user "Alice" reads the content of file "testFile.txt"
        And user "Alice" reads the content of file "parent/lorem.txt"
        Then the file "testFile.txt" should be downloaded
        And the file "parent/lorem.txt" should be downloaded
        And the placeholder file "parent/epsum.txt" should exist on the file system

        # mark files "Always keep on this device"
        When user "Alice" marks file "testFile.txt" as "Always keep on this device" from the file explorer
        And the user waits for file "testFile.txt" to be synced
        Then the file "testFile.txt" should be downloaded
        When user "Alice" marks file "simple.txt" as "Always keep on this device" from the file explorer
        And the user waits for file "simple.txt" to be synced
        Then the file "simple.txt" should be downloaded
        And the placeholder file "large.txt" should exist on the file system

        # mark files "Free up space"
        When user "Alice" marks file "testFile.txt" as "Free up space" from the file explorer
        And the user waits for file "testFile.txt" to be synced
        Then the placeholder file "testFile.txt" should exist on the file system
        When user "Alice" marks file "parent/lorem.txt" as "Free up space" from the file explorer
        And the user waits for file "parent/lorem.txt" to be synced
        Then the placeholder file "parent/lorem.txt" should exist on the file system
        When user "Alice" marks file "simple.txt" as "Free up space" from the file explorer
        And the user waits for file "simple.txt" to be synced
        Then the placeholder file "simple.txt" should exist on the file system


    Scenario: Hydration and dehydration of folders via file explorer
        Given user "Alice" has been created in the server with default attributes
        And user "Alice" has created folder "testFol" in the server
        And user "Alice" has created folder "nested" in the server
        And user "Alice" has created folder "nested/subfol1" in the server
        And user "Alice" has created folder "nested/subfol1/subfol2" in the server
        And user "Alice" has created folder "nested/subfol1/subfol2/subfol3" in the server
        And user "Alice" has created folder "nested/subfol1/subfol2/subfol3/subfol4" in the server
        And user "Alice" has uploaded file with content "test content" to "simple.txt" in the server
        And user "Alice" has uploaded file with content "some contents" to "nested/lorem.txt" in the server
        And user "Alice" has uploaded file with content "some contents" to "nested/subfol1/subfile1.txt" in the server
        And user "Alice" has uploaded file with content "some contents" to "nested/subfol1/subfol2/subfile2.txt" in the server
        And user "Alice" has uploaded file with content "some contents" to "nested/subfol1/subfol2/subfol3/subfile3.txt" in the server
        And user "Alice" has uploaded file with content "some contents" to "nested/subfol1/subfol2/subfol3/subfol4/subfile4.txt" in the server
        And user "Alice" has set up a client with default settings
        Then the placeholder file "simple.txt" should exist on the file system
        And the placeholder file "nested/lorem.txt" should exist on the file system
        And the placeholder file "nested/subfol1/subfol2/subfol3/subfol4/subfile4.txt" should exist on the file system

        # mark sub folder as "Always keep on this device"
        When user "Alice" reads the content of file "nested/subfol1/subfol2/subfile2.txt"
        And user "Alice" marks folder "nested/subfol1" as "Always keep on this device" from the file explorer
        And the user waits for folder "nested/subfol1" to be synced
        Then the file "nested/subfol1/subfile1.txt" should be downloaded
        And the file "nested/subfol1/subfol2/subfile2.txt" should be downloaded
        And the file "nested/subfol1/subfol2/subfol3/subfile3.txt" should be downloaded
        And the file "nested/subfol1/subfol2/subfol3/subfol4/subfile4.txt" should be downloaded
        And the placeholder file "nested/lorem.txt" should exist on the file system

        # create local files and folders in "Always keep on this device" folder
        When user "Alice" creates a folder "nested/subfol1/subfol2/localFol" inside the sync folder
        And user "Alice" creates a file "nested/subfol1/subfol2/local.txt" with the following content inside the sync folder
            """
            local file
            """
        And the user waits for folder "nested/subfol1/subfol2/localFol" to be synced
        And the user waits for file "nested/subfol1/subfol2/local.txt" to be synced
        Then the file "nested/subfol1/subfol2/local.txt" should be downloaded

        # create local files and folders in "Free up space" folder
        When user "Alice" creates a folder "nested/localFol" inside the sync folder
        And user "Alice" creates a file "nested/local.txt" with the following content inside the sync folder
            """
            local file
            """
        And the user waits for folder "nested/localFol" to be synced
        And the user waits for file "nested/local.txt" to be synced
        Then the file "nested/local.txt" should be downloaded

        # upload files to "Always keep on this device" folder in the server
        When user "Alice" uploads file with content "server content" to "nested/subfol1/subfol2/localFol/fromServer.txt" in the server
        And the user waits for file "nested/subfol1/subfol2/localFol/fromServer.txt" to be synced
        Then the file "nested/subfol1/subfol2/localFol/fromServer.txt" should be downloaded

        # upload files to "Free up space" folder in the server
        When user "Alice" uploads file with content "server content" to "nested/fromServer.txt" in the server
        And user "Alice" uploads file with content "server content" to "nested/localFol/fromServer.txt" in the server
        And the user waits for file "nested/localFol/fromServer.txt" to be synced
        Then the placeholder file "nested/fromServer.txt" should exist on the file system
        And the placeholder file "nested/localFol/fromServer.txt" should exist on the file system

        # mark sub folder as "Free up space"
        When user "Alice" marks folder "nested/subfol1/subfol2" as "Free up space" from the file explorer
        And the user waits for folder "nested/subfol1/subfol2" to be synced
        Then the placeholder file "nested/subfol1/subfol2/subfile2.txt" should exist on the file system
        And the placeholder file "nested/subfol1/subfol2/local.txt" should exist on the file system
        And the placeholder file "nested/subfol1/subfol2/localFol/fromServer.txt" should exist on the file system
        And the placeholder file "nested/subfol1/subfol2/subfol3/subfile3.txt" should exist on the file system
        And the placeholder file "nested/subfol1/subfol2/subfol3/subfol4/subfile4.txt" should exist on the file system
        And the file "nested/subfol1/subfile1.txt" should be downloaded
