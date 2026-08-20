Feature: Syncing files
    As a user
    I want to be able to sync the files and folders to/from the server
    so that my files are always up to date

    Background:
        Given user "Alice" has been created in the server with default attributes

    @issue-9281 @smoke
    Scenario: Syncing a file to the server
        Given user "Alice" has set up a client with default settings
        When user "Alice" creates a file "lorem-for-upload.txt" with the following content inside the sync folder
            """
            test content
            """
        And the user waits for file "lorem-for-upload.txt" to be synced
        And the user opens the activity tab
        And the user selects "Local Activity" tab in the activity
        Then the file "lorem-for-upload.txt" should have status "Uploaded" in the activity tab
        And as "Alice" the file "lorem-for-upload.txt" should have the content "test content" in the server

    @smoke
    Scenario: Syncing all files and folders from the server
        Given user "Alice" has created folder "simple-folder" in the server
        And user "Alice" has created folder "large-folder" in the server
        And user "Alice" has uploaded file with content "test content" to "uploaded-lorem.txt" in the server
        And user "Alice" has set up a client with default settings
        Then the file "uploaded-lorem.txt" should exist on the file system
        And the file "uploaded-lorem.txt" should exist on the file system with the following content
            """
            test content
            """
        And the folder "simple-folder" should exist on the file system
        And the folder "large-folder" should exist on the file system

    @skipOnWindows
    Scenario: Sync all is selected by default
        Given user "Alice" has created folder "simple-folder" in the server
        And user "Alice" has created folder "large-folder" in the server
        And user "Alice" has uploaded file with content "test content" to "testFile.txt" in the server
        And user "Alice" has uploaded file with content "lorem content" to "lorem.txt" in the server
        And the user has started the client
        And the user has entered the following account information:
            | server   | %local_server% |
            | user     | Alice          |
            | password | 1234           |
        When the user selects manual sync folder option in advanced section
        And the user sets the sync path in sync connection wizard
        And the user navigates back in the sync connection wizard
        And the user sets the temp folder "localSyncFolder" as local sync path in sync connection wizard
        And the user selects "Personal" space in sync connection wizard
        Then the sync all checkbox should be checked
        When user unselects all the remote folders
        And the user adds the folder sync connection
        And the user waits for the files to sync
        Then the file "testFile.txt" should exist on the file system
        And the file "lorem.txt" should exist on the file system
        But the folder "simple-folder" should not exist on the file system
        And the folder "large-folder" should not exist on the file system

    @skipOnWindows @smoke
    Scenario: Sync only one folder from the server
        Given user "Alice" has created folder "simple-folder" in the server
        And user "Alice" has created folder "large-folder" in the server
        And the user has started the client
        And the user has entered the following account information:
            | server   | %local_server% |
            | user     | Alice          |
            | password | 1234           |
        When the user selects manual sync folder option in advanced section
        And the user sets the sync path in sync connection wizard
        And the user selects "Personal" space in sync connection wizard
        And the user selects only the following folders to sync:
            | folder        |
            | simple-folder |
        Then the folder "simple-folder" should exist on the file system
        But the folder "large-folder" should not exist on the file system
        When user "Alice" uploads file with content "some content" to "simple-folder/lorem.txt" in the server
        And user "Alice" uploads file with content "openCloud" to "large-folder/lorem.txt" in the server
        And user "Alice" creates a file "simple-folder/localFile.txt" with the following content inside the sync folder
            """
            test content
            """
        And the user waits for the files to sync
        Then the file "simple-folder/lorem.txt" should exist on the file system
        And the file "large-folder/lorem.txt" should not exist on the file system
        And as "Alice" file "simple-folder/localFile.txt" should exist in the server

    @issue-9733 @skipOnWindows
    Scenario: sort folders list by name and size
        Given user "Alice" has created folder "123Folder" in the server
        And user "Alice" has uploaded file with content "small" to "123Folder/lorem.txt" in the server
        And user "Alice" has created folder "aFolder" in the server
        And user "Alice" has uploaded file with content "more contents" to "aFolder/lorem.txt" in the server
        And user "Alice" has created folder "bFolder" in the server
        And the user has started the client
        And the user has entered the following account information:
            | server   | %local_server% |
            | user     | Alice          |
            | password | 1234           |
        When the user selects manual sync folder option in advanced section
        And the user sets the sync path in sync connection wizard
        And the user selects "Personal" space in sync connection wizard
        # folders are sorted by name in ascending order by default
        Then the folders should be in the following order:
            | folder    |
            | 123Folder |
            | aFolder   |
            | bFolder   |
        # sort folder by name in descending order
        When the user sorts the folder list by "Name"
        Then the folders should be in the following order:
            | folder    |
            | bFolder   |
            | aFolder   |
            | 123Folder |
        # sort folder by size in ascending order
        When the user sorts the folder list by "Size"
        Then the folders should be in the following order:
            | folder    |
            | bFolder   |
            | 123Folder |
            | aFolder   |
        # sort folder by size in descending order
        When the user sorts the folder list by "Size"
        Then the folders should be in the following order:
            | folder    |
            | aFolder   |
            | 123Folder |
            | bFolder   |

    @smoke
    Scenario Outline: Syncing a folder to the server
        Given user "Alice" has set up a client with default settings
        When user "Alice" creates a folder <foldername> inside the sync folder
        And the user waits for folder <foldername> to be synced
        Then as "Alice" folder <foldername> should exist in the server
        Examples:
            | foldername                                                               |
            | "myFolder"                                                               |
            | "really long folder name with some spaces and special char such as $%ñ&" |

    @smoke
    Scenario: Many subfolders can be synced
        Given user "Alice" has created folder "parent" in the server
        And user "Alice" has set up a client with default settings
        When user "Alice" creates a folder "parent/subfolderEmpty1" inside the sync folder
        And user "Alice" creates a folder "parent/subfolderEmpty2" inside the sync folder
        And user "Alice" creates a folder "parent/subfolderEmpty3" inside the sync folder
        And user "Alice" creates a folder "parent/subfolderEmpty4" inside the sync folder
        And user "Alice" creates a folder "parent/subfolderEmpty5" inside the sync folder
        And user "Alice" creates a folder "parent/subfolder1" inside the sync folder
        And user "Alice" creates a folder "parent/subfolder2" inside the sync folder
        And user "Alice" creates a folder "parent/subfolder3" inside the sync folder
        And user "Alice" creates a folder "parent/subfolder4" inside the sync folder
        And user "Alice" creates a folder "parent/subfolder5" inside the sync folder
        And user "Alice" creates a file "parent/subfolder1/test.txt" with the following content inside the sync folder
            """
            test content
            """
        And user "Alice" creates a file "parent/subfolder2/test.txt" with the following content inside the sync folder
            """
            test content
            """
        And user "Alice" creates a file "parent/subfolder3/test.txt" with the following content inside the sync folder
            """
            test content
            """
        And user "Alice" creates a file "parent/subfolder4/test.txt" with the following content inside the sync folder
            """
            test content
            """
        And user "Alice" creates a file "parent/subfolder5/test.txt" with the following content inside the sync folder
            """
            test content
            """
        And the user waits for file "parent/subfolder5/test.txt" to be synced
        Then as "Alice" folder "parent/subfolderEmpty1" should exist in the server
        And as "Alice" folder "parent/subfolderEmpty2" should exist in the server
        And as "Alice" folder "parent/subfolderEmpty3" should exist in the server
        And as "Alice" folder "parent/subfolderEmpty4" should exist in the server
        And as "Alice" folder "parent/subfolderEmpty5" should exist in the server
        And as "Alice" folder "parent/subfolder1" should exist in the server
        And as "Alice" folder "parent/subfolder2" should exist in the server
        And as "Alice" folder "parent/subfolder3" should exist in the server
        And as "Alice" folder "parent/subfolder4" should exist in the server
        And as "Alice" folder "parent/subfolder5" should exist in the server

    @smoke
    Scenario: Both original and copied folders can be synced
        Given user "Alice" has set up a client with default settings
        When user "Alice" creates a folder "original" inside the sync folder
        And user "Alice" creates a file "original/localFile.txt" with the following content inside the sync folder
            """
            test content
            """
        And the user copies folder "original" into the same directory
        And the user waits for folder "original (Copy)" to be synced
        Then as "Alice" folder "original" should exist in the server
        And as "Alice" the file "original/localFile.txt" should have the content "test content" in the server
        And as "Alice" folder "original (Copy)" should exist in the server
        And as "Alice" the file "original (Copy)/localFile.txt" should have the content "test content" in the server

    @smoke
    Scenario: Verify pre existing folders in local (Desktop client) are copied over to the server
        Given user "Alice" has created a folder "Folder1" inside the sync folder
        And user "Alice" has created a folder "Folder1/subFolder1" inside the sync folder
        And user "Alice" has created a folder "Folder1/subFolder1/subFolder2" inside the sync folder
        And user "Alice" has set up a client with default settings
        Then as "Alice" folder "Folder1" should exist in the server
        And as "Alice" folder "Folder1/subFolder1" should exist in the server
        And as "Alice" folder "Folder1/subFolder1/subFolder2" should exist in the server

    @skipOnWindows
    Scenario: Filenames that are rejected by the server are reported (Linux only)
        Given user "Alice" has created folder "Folder1" in the server
        And user "Alice" has set up a client with default settings
        When user "Alice" creates a file "Folder1/a\\a.txt" with the following content inside the sync folder
            """
            test content
            """
        And the user opens the activity tab
        And the user selects "Not Synced" tab in the activity
        Then the file "Folder1/a\\a.txt" should exist on the file system
        And the file "Folder1/a\\a.txt" should be blacklisted

    @skipOnWindows @smoke
    Scenario: Invalid system names are synced (Linux only)
        Given user "Alice" has created folder "CON" in the server
        And user "Alice" has created folder "test%" in the server
        And user "Alice" has uploaded file with content "server content" to "/PRN" in the server
        And user "Alice" has uploaded file with content "server content" to "/foo%" in the server
        And user "Alice" has set up a client with default settings
        Then the folder "CON" should exist on the file system
        And the folder "test%" should exist on the file system
        And the file "PRN" should exist on the file system
        And the file "foo%" should exist on the file system
        And as "Alice" folder "CON" should exist in the server
        And as "Alice" folder "test%" should exist in the server
        And as "Alice" file "/PRN" should exist in the server
        And as "Alice" file "/foo%" should exist in the server

    @skipOnLinux @skip
    Scenario: Sync invalid system names (Windows only)
        Given user "Alice" has created folder "CON" in the server
        And user "Alice" has created folder "test%" in the server
        And user "Alice" has uploaded file with content "server content" to "/PRN" in the server
        And user "Alice" has uploaded file with content "server content" to "/foo%" in the server
        And user "Alice" has set up a client with default settings
        Then the folder "test%" should exist on the file system
        And the file "foo%" should exist on the file system
        But the folder "CON" should not exist on the file system
        And the file "PRN" should not exist on the file system

    @smoke
    Scenario: various types of files can be synced from server to client
        Given user "Alice" has created folder "simple-folder" in the server
        And user "Alice" has uploaded file "testavatar.png" to "simple-folder/testavatar.png" in the server
        And user "Alice" has uploaded file "testavatar.jpg" to "simple-folder/testavatar.jpg" in the server
        And user "Alice" has uploaded file "testavatar.jpeg" to "simple-folder/testavatar.jpeg" in the server
        And user "Alice" has uploaded file "testimage.mp3" to "simple-folder/testimage.mp3" in the server
        And user "Alice" has uploaded file "test_video.mp4" to "simple-folder/test_video.mp4" in the server
        And user "Alice" has uploaded file "simple.pdf" to "simple-folder/simple.pdf" in the server
        And user "Alice" has uploaded file "simple.docx" to "simple-folder/simple.docx" in the server
        And user "Alice" has uploaded file "simple.pptx" to "simple-folder/simple.pptx" in the server
        And user "Alice" has uploaded file "simple.xlsx" to "simple-folder/simple.xlsx" in the server
        And user "Alice" has set up a client with default settings
        Then the folder "simple-folder" should exist on the file system
        And the file "simple-folder/testavatar.png" should exist on the file system
        And the file "simple-folder/testavatar.jpg" should exist on the file system
        And the file "simple-folder/testavatar.jpeg" should exist on the file system
        And the file "simple-folder/testimage.mp3" should exist on the file system
        And the file "simple-folder/test_video.mp4" should exist on the file system
        And the file "simple-folder/simple.pdf" should exist on the file system
        And the file "simple-folder/simple.docx" should exist on the file system
        And the file "simple-folder/simple.pptx" should exist on the file system
        And the file "simple-folder/simple.xlsx" should exist on the file system


    Scenario: various types of files can be synced from client to server
        Given user "Alice" has set up a client with default settings
        When user "Alice" creates the following files inside the sync folder:
            | files            |
            | /testavatar.png  |
            | /testavatar.jpg  |
            | /testavatar.jpeg |
            | /testaudio.mp3   |
            | /test_video.mp4  |
            | /simple.txt      |
            | /simple.docx     |
            | /simple.pptx     |
            | /simple.xlsx     |
        And the user waits for the files to sync
        Then as "Alice" file "testavatar.png" should exist in the server
        And as "Alice" file "testavatar.jpg" should exist in the server
        And as "Alice" file "testavatar.jpeg" should exist in the server
        And as "Alice" file "testaudio.mp3" should exist in the server
        And as "Alice" file "test_video.mp4" should exist in the server
        And as "Alice" file "simple.txt" should exist in the server
        And as "Alice" file "simple.docx" should exist in the server
        And as "Alice" file "simple.pptx" should exist in the server
        And as "Alice" file "simple.xlsx" should exist in the server

    @smoke
    Scenario: Syncing file of 1 GB size
        Given user "Alice" has set up a client with default settings
        When user "Alice" creates a file "newfile.txt" with size "1GB" inside the sync folder
        And the user waits for file "newfile.txt" to be synced
        Then as "Alice" file "newfile.txt" should exist in the server


    Scenario: Syncing folders each having large number of files
        Given the user has created a folder "folder1" in temp folder
        And the user has created "500" files each of size "1048576" bytes inside folder "folder1" in temp folder
        And the user has created a folder "folder2" in temp folder
        And the user has created "500" files each of size "1048576" bytes inside folder "folder2" in temp folder
        And the user has created a folder "folder3" in temp folder
        And the user has created "1000" files each of size "1048576" bytes inside folder "folder3" in temp folder
        And user "Alice" has set up a client with default settings
        When user "Alice" moves folder "folder1" from the temp folder into the sync folder
        And user "Alice" moves folder "folder2" from the temp folder into the sync folder
        And user "Alice" moves folder "folder3" from the temp folder into the sync folder
        And the user waits for folder "folder1" to be synced
        And the user waits for folder "folder2" to be synced
        And the user waits for folder "folder3" to be synced
        Then as "Alice" folder "folder1" should exist in the server
        And as user "Alice" folder "folder1" should contain "500" items in the server
        And as "Alice" folder "folder2" should exist in the server
        And as user "Alice" folder "folder2" should contain "500" items in the server
        And as "Alice" folder "folder3" should exist in the server
        And as user "Alice" folder "folder3" should contain "1000" items in the server

    @smoke
    Scenario: Skip sync folder configuration
        Given the user has started the client
        And the user has entered the following account information:
            | server   | %local_server% |
            | user     | Alice          |
            | password | 1234           |
        When the user selects manual sync folder option in advanced section
        And the user cancels the sync connection wizard
        Then "Alice" account should be added
        And for user "Alice" sync folder "Personal" should not be displayed
        And for user "Alice" sync folder "Shares" should not be displayed

    @issue-11814
    Scenario: Remove folder sync connection (Personal Space)
        Given user "Alice" has created folder "simple-folder" in the server
        And user "Alice" has set up a client with default settings
        When the user removes the folder sync connection
        Then for user "Alice" sync folder "Personal" should not be displayed
        And the folder "simple-folder" should exist on the file system
        And as "Alice" folder "simple-folder" should exist in the server

    @skipOnWindows @smoke
    Scenario: Unselected subfolders are excluded from local sync
        Given user "Alice" has created folder "test-folder" in the server
        And user "Alice" has created folder "test-folder/sub-folder1" in the server
        And user "Alice" has created folder "test-folder/sub-folder2" in the server
        And user "Alice" has set up a client with default settings
        When the user unselects the following folders to sync in "Choose what to sync" window:
            | folder                  |
            | test-folder/sub-folder2 |
        And the user waits for folder "test-folder/sub-folder2" to be synced
        Then the folder "test-folder/sub-folder1" should exist on the file system
        And the folder "test-folder/sub-folder2" should not exist on the file system
        When user "Alice" uploads file with content "some content" to "test-folder/sub-folder2/lorem.txt" in the server
        And the user force syncs the files
        And the user waits for the files to sync
        Then the file "test-folder/sub-folder2/lorem.txt" should not exist on the file system

    @skipOnWindows
    Scenario: Only root level files sync when all folders are unselected
        Given user "Alice" has created folder "test-folder" in the server
        And user "Alice" has created folder "test-folder/sub-folder1" in the server
        And user "Alice" has created folder "test-folder/sub-folder2" in the server
        And user "Alice" has uploaded file with content "root file content" to "root-file.txt" in the server
        And user "Alice" has uploaded file with content "some subfolder content" to "test-folder/sub-folder1/lorem.txt" in the server
        And the user has started the client
        And the user has entered the following account information:
            | server   | %local_server% |
            | user     | Alice          |
            | password | 1234           |
        When the user selects manual sync folder option in advanced section
        And the user sets the sync path in sync connection wizard
        And the user selects "Personal" space in sync connection wizard
        And user unselects all the remote folders
        And the user adds the folder sync connection
        And the user waits for the files to sync
        Then the folder "test-folder/sub-folder1" should not exist on the file system
        And the folder "test-folder/sub-folder2" should not exist on the file system
        And the file "test-folder/sub-folder1/lorem.txt" should not exist on the file system
        But the file "root-file.txt" should exist on the file system

