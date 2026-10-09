Feature: Syncing files
    As a user
    I want to be able to sync the files and folders to/from the server
    so that my files are always up to date

    Background:
        Given user "Alice" has been created in the server with default attributes

    @issue-649 @skip
    Scenario: Syncing a file from the server and creating a conflict
        Given user "Alice" has uploaded file with content "server content" to "/conflict.txt" in the server
        And user "Alice" has set up a client with default settings
        And the user has paused the file sync
        And the user has changed the content of local file "conflict.txt" to:
            """
            client content
            """
        And user "Alice" has uploaded file with content "changed server content" to "/conflict.txt" in the server
        And the user has waited for "5" seconds
        When the user resumes the file sync on the client
        And the user opens the activity tab
        And the user selects "Not Synced" tab in the activity
        Then the table of conflict warnings should include file "conflict.txt"
        And the file "conflict.txt" should exist on the file system with the following content
            """
            changed server content
            """
        And a conflict file for "conflict.txt" should exist on the file system with the following content
            """
            client content
            """

    @skipOnWindows
    Scenario Outline: Syncing a folder having space at the end (Linux only)
        Given user "Alice" has set up a client with default settings
        When user "Alice" creates a folder <foldername> inside the sync folder
        And the user waits for folder <foldername> to be synced
        Then as "Alice" folder <foldername> should exist in the server
        Examples:
            | foldername                  |
            | "folder with space at end " |

    @skipOnLinux
    Scenario: Try to sync files having space at the end (Windows only)
        Given user "Alice" has uploaded file with content "lorem epsum" to "trailing-space.txt " in the server
        And user "Alice" has set up a client with default settings
        When user "Alice" creates a folder "folder with space at end " inside the sync folder
        And the user force syncs the files
        And the user opens the activity tab
        And the user selects "Not Synced" tab in the activity
        Then the file "trailing-space.txt " should be ignored
        And the file "folder with space at end " should be ignored

    @issue-9281 @smoke
    Scenario: Verify that you can create a subfolder with long name(~220 characters)
        Given user "Alice" has created a folder "Folder1" inside the sync folder
        And user "Alice" has set up a client with default settings
        When user "Alice" creates a folder "Folder1/thisIsAVeryLongFolderNameToCheckThatItWorks-thisIsAVeryLongFolderNameToCheckThatItWorks-thisIsAVeryLongFolderNameToCheckThatItWorks-thisIsAVeryLongFolderNameToCheckThatItWorks" inside the sync folder
        And the user waits for folder "Folder1/thisIsAVeryLongFolderNameToCheckThatItWorks-thisIsAVeryLongFolderNameToCheckThatItWorks-thisIsAVeryLongFolderNameToCheckThatItWorks-thisIsAVeryLongFolderNameToCheckThatItWorks" to be synced
        Then the folder "Folder1/thisIsAVeryLongFolderNameToCheckThatItWorks-thisIsAVeryLongFolderNameToCheckThatItWorks-thisIsAVeryLongFolderNameToCheckThatItWorks-thisIsAVeryLongFolderNameToCheckThatItWorks" should exist on the file system
        And as "Alice" folder "Folder1/thisIsAVeryLongFolderNameToCheckThatItWorks-thisIsAVeryLongFolderNameToCheckThatItWorks-thisIsAVeryLongFolderNameToCheckThatItWorks-thisIsAVeryLongFolderNameToCheckThatItWorks" should exist in the server


    Scenario Outline: Sync long nested folder
        Given user "Alice" has created folder "<foldername>" in the server
        And user "Alice" has set up a client with default settings
        When user "Alice" creates a folder "<foldername>/<foldername>" inside the sync folder
        And user "Alice" creates a folder "<foldername>/<foldername>/<foldername>" inside the sync folder
        And user "Alice" creates a folder "<foldername>/<foldername>/<foldername>/<foldername>" inside the sync folder
        And user "Alice" creates a folder "<foldername>/<foldername>/<foldername>/<foldername>/<foldername>" inside the sync folder
        And the user waits for folder "<foldername>/<foldername>/<foldername>/<foldername>/<foldername>" to be synced
        Then as "Alice" folder "<foldername>/<foldername>" should exist in the server
        And as "Alice" folder "<foldername>/<foldername>/<foldername>" should exist in the server
        And as "Alice" folder "<foldername>/<foldername>/<foldername>/<foldername>" should exist in the server
        And as "Alice" folder "<foldername>/<foldername>/<foldername>/<foldername>/<foldername>" should exist in the server
        Examples:
            | foldername                                                      |
            | An empty folder which name is obviously more than 59 characters |

    @smoke
    Scenario Outline: File with long name can be synced
        Given user "Alice" has set up a client with default settings
        When user "Alice" creates a file "<filename>" with the following content inside the sync folder
            """
            test content
            """
        And the user waits for file "<filename>" to be synced
        Then as "Alice" file "<filename>" should exist in the server
        Examples:
            | filename                                                                                                                                                                                                                     |
            | thisIsAVeryLongFileNameToCheckThatItWorks-thisIsAVeryLongFileNameToCheckThatItWorks-thisIsAVeryLongFileNameToCheckThatItWorks-thisIsAVeryLongFileNameToCheckThatItWorks-thisIsAVeryLongFileNameToCheckThatItWorks-thisIs.txt |


    Scenario: File with spaces in the name can sync
        Given user "Alice" has set up a client with default settings
        When user "Alice" creates a file "file with space.txt" with the following content inside the sync folder
            """
            test contents
            """
        And the user waits for file "file with space.txt" to be synced
        Then as "Alice" file "file with space.txt" should exist in the server


    Scenario: extract a zip file in the sync folder
        Given the user has created a zip file "archive.zip" with the following resources in the temp folder
            | resource  | type   | content    |
            | folder1   | folder |            |
            | folder2   | folder |            |
            | file1.txt | file   | Test file1 |
            | file2.txt | file   | Test file2 |
        And user "Alice" has set up a client with default settings
        When user "Alice" moves file "archive.zip" from the temp folder into the sync folder
        And user "Alice" unzips the zip file "archive.zip" inside the sync root
        And the user waits for the files to sync
        Then as "Alice" folder "folder1" should exist in the server
        And as "Alice" folder "folder2" should exist in the server
        And as "Alice" the file "file1.txt" should have the content "Test file1" in the server
        And as "Alice" the file "file2.txt" should have the content "Test file2" in the server

    @skipOnWindows
    Scenario: sync remote folder to a local sync folder having special characters
        Given user "Alice" has created folder "~`!@#$^&()-_=+{[}];',)" in the server
        And user "Alice" has created folder "simple-folder" in the server
        And user "Alice" has created folder "test-folder" in the server
        And user "Alice" has created folder "test-folder/sub-folder1" in the server
        And user "Alice" has created folder "test-folder/sub-folder2" in the server
        And user "Alice" has created folder "~test%" in the server
        And the user has created a folder "~`!@#$^&()-_=+{[}];',)PRN%" in temp folder
        And the user has started the client
        And the user has entered the following account information:
            | server   | %local_server% |
            | user     | Alice          |
            | password | 1234           |
        When the user selects manual sync folder option in advanced section
        And the user sets the temp folder "~`!@#$^&()-_=+{[}];',)PRN%" as local sync path in sync connection wizard
        And the user selects "Personal" space in sync connection wizard
        And the user selects only the following folders to sync:
            | folder                  |
            | ~`!@#$^&()-_=+{[}];',)  |
            | simple-folder           |
            | test-folder/sub-folder2 |
        Then the folder "~`!@#$^&()-_=+{[}];',)" should exist on the file system
        And the folder "simple-folder" should exist on the file system
        But the folder "~test%" should not exist on the file system
        When user "Alice" deletes the folder "simple-folder" in the server
        And the user waits for the files to sync
        Then the folder "simple-folder" should not exist on the file system
        And the folder "test-folder/sub-folder2" should exist on the file system
        And the folder "test-folder/sub-folder1" should not exist on the file system


    Scenario: Syncing a local folder having special characters to the server
        Given user "Alice" has set up a client with default settings
        When user "Alice" creates a folder "~`!@#$^&()-_=+{[}];',)💥🫨❤️‍🔥" inside the sync folder
        And the user waits for folder "~`!@#$^&()-_=+{[}];',)💥🫨❤️‍🔥" to be synced
        Then as "Alice" folder "~`!@#$^&()-_=+{[}];',)💥🫨❤️‍🔥" should exist in the server


    Scenario Outline: File with long multi-byte characters name can be synced (76 characters, 255 bytes including extension)
        Given user "Alice" has set up a client with default settings
        When user "Alice" creates a file "<filename>" with the following content inside the sync folder
            """
            test content
            """
        And the user waits for file "<filename>" to be synced
        Then as "Alice" file "<filename>" should exist in the server
        Examples:
            | filename                                                                    |
            | 𒁰𒁱𒁲𒁳𒁴𒁵𒁶𒁷𒁸𒁹𒁺𒁻𒁼𒁾𒁿𒁰𒁱𒁲𒁳𒁴𒁵𒁶𒁷𒁸𒁹𒁺𒁻𒁼𒁾𒁿𒁰𒁱𒁲𒁳𒁴𒁵𒁶𒁷𒁸𒁹𒁺abôǣฎพฒฆ๘ตกกผพฒณญไใๅำ๊๒๔๗๘รศฬอฮ.txt |


    Scenario: Sync a received shared folder with Viewer permission role
        Given user "Brian" has been created in the server with default attributes
        And user "Alice" has created folder "simple-folder" in the server
        And user "Alice" has uploaded file with content "test content" to "simple-folder/uploaded-lorem.txt" in the server
        And user "Alice" has sent the following resource share invitation:
            | resource        | simple-folder |
            | sharee          | Brian         |
            | permissionsRole | Viewer        |
        And user "Brian" has set up a client with space "Shares"
        When user "Brian" creates a folder "simple-folder/sub-folder" inside the sync folder
        Then the folder "simple-folder/sub-folder" should exist on the file system
        But the following error message should appear in the client
            """
            simple-folder/sub-folder: Not allowed because you don't have permission to add subfolders to that folder
            """
        When the user copies file "simple.pdf" from outside the sync folder to "simple-folder/simple.pdf" in the sync folder
        Then the file "simple-folder/simple.pdf" should exist on the file system
        But the following error message should appear in the client
            """
            simple-folder/simple.pdf: Not allowed because you don't have permission to add files in that folder
            """
        And as "Brian" folder "simple-folder/sub-folder" should not exist in the server
        And as "Brian" file "simple-folder/simple.pdf" should not exist in the server
        When the user opens the activity tab
        And the user selects "Not Synced" tab in the activity
        Then the following activities should be displayed in not synced table
            | resource                 | status      | account                              |
            | simple-folder/sub-folder | Blacklisted | Brian Murphy@%local_server_hostname% |
            | simple-folder/simple.pdf | Blacklisted | Brian Murphy@%local_server_hostname% |


    Scenario: Sync a received shared folder with Editor permission role
        Given user "Brian" has been created in the server with default attributes
        And user "Alice" has created folder "simple-folder" in the server
        And user "Alice" has uploaded file with content "test content" to "simple-folder/uploaded-lorem.txt" in the server
        And user "Alice" has sent the following resource share invitation:
            | resource        | simple-folder |
            | sharee          | Brian         |
            | permissionsRole | Editor        |
        And user "Brian" has set up a client with space "Shares"
        When user "Brian" creates a folder "simple-folder/sub-folder" inside the sync folder
        And the user copies file "simple.pdf" from outside the sync folder to "simple-folder/simple.pdf" in the sync folder
        And the user overwrites the file "simple-folder/uploaded-lorem.txt" with content "overwrite openCloud test text file"
        And the user waits for the files to sync
        And the user waits for folder "simple-folder/sub-folder" to be synced
        Then the folder "simple-folder/sub-folder" should exist on the file system
        And the file "simple-folder/simple.pdf" should exist on the file system
        And as "Brian" folder "Shares/simple-folder/sub-folder" should exist in the server
        And as "Brian" file "Shares/simple-folder/simple.pdf" should exist in the server
        And as "Brian" the file "Shares/simple-folder/uploaded-lorem.txt" should have the content "overwrite openCloud test text file" in the server


    Scenario:  Sync same unicode folder from server and client at the same time
        Given user "Alice" has set up a client with default settings
        And the user has paused the file sync
        And user "Alice" has created folder "Öü" in the server
        And user "Alice" has uploaded file with content "openCloud test" to "Öü/testFile.txt" in the server
        When user "Alice" creates a folder "Öü" inside the sync folder
        And user "Alice" creates a file "Öü/newfile.txt" with the following content inside the sync folder
            """
            test content
            """
        And the user waits for "1" seconds
        And the user resumes the file sync on the client
        And the user waits for the files to sync
        Then the file "Öü/newfile.txt" should exist on the file system
        And as user "Alice" folder "/" should contain "1" items in the server
        And as "Alice" file "Öü/testFile.txt" should exist in the server
        And as "Alice" file "Öü/newfile.txt" should exist in the server


    Scenario: Try to sync hidden files and folders
        Given user "Alice" has created folder "folder" in the server
        And user "Alice" has created folder ".hidefol" in the server
        And user "Alice" has created folder "folder/subfolder" in the server
        And user "Alice" has created folder "folder/.hidesubfol" in the server
        And user "Alice" has uploaded file with content "lorem" to ".roothide.txt" in the server
        And user "Alice" has uploaded file with content "lorem" to "folder/.hidefile.txt" in the server
        And user "Alice" has uploaded file with content "lorem" to ".hidefol/file.txt" in the server
        And user "Alice" has uploaded file with content "lorem" to "folder/subfolder/.hidesubfile.txt" in the server
        And user "Alice" has uploaded file with content "lorem" to "folder/.hidesubfol/subfile.txt" in the server
        And user "Alice" has created the following folders inside the sync folder:
            | foldername                |
            | localFol                  |
            | .localhideFol             |
            | localFol/localsub         |
            | localFol/.localsubhideFol |
        And user "Alice" has created the following files inside the sync folder:
            | filename                                  | content |
            | .localroothide.txt                        | local   |
            | localFol/.localhide.txt                   | local   |
            | .localhideFol/localfile.txt               | local   |
            | localFol/localsub/.localsubhide.txt       | local   |
            | localFol/.localsubhideFol/localsubfle.txt | local   |
        And user "Alice" has set up a client with default settings

        # update files content
        When user "Alice" uploads file with content "lorem-edited" to ".roothide.txt" in the server
        And user "Alice" uploads file with content "lorem-edited" to "folder/.hidefile.txt" in the server
        And user "Alice" uploads file with content "lorem-edited" to ".hidefol/file.txt" in the server
        And user "Alice" uploads file with content "lorem-edited" to "folder/subfolder/.hidesubfile.txt" in the server
        And user "Alice" uploads file with content "lorem-edited" to "folder/.hidesubfol/subfile.txt" in the server
        And user "Alice" updates the content of the following files inside the sync folder:
            | filename                                  | content      |
            | .localroothide.txt                        | local-edited |
            | localFol/.localhide.txt                   | local-edited |
            | .localhideFol/localfile.txt               | local-edited |
            | localFol/localsub/.localsubhide.txt       | local-edited |
            | localFol/.localsubhideFol/localsubfle.txt | local-edited |
        And the user force syncs the files
        And the user waits for the files to sync
        Then the file ".roothide.txt" should not exist on the file system
        And the file "folder/.hidefile.txt" should not exist on the file system
        And the file ".hidefol/file.txt" should not exist on the file system
        And the file "folder/subfolder/.hidesubfile.txt" should not exist on the file system
        And the file "folder/.hidesubfol/subfile.txt" should not exist on the file system
        And as "Alice" file ".localroothide.txt" should not exist in the server
        And as "Alice" file "localFol/.localhide.txt" should not exist in the server
        And as "Alice" file ".localhideFol/localfile.txt" should not exist in the server
        And as "Alice" file "localFol/localsub/.localsubhide.txt" should not exist in the server
        And as "Alice" file "localFol/.localsubhideFol/localsubfle.txt" should not exist in the server

    @issue-1127 @issue-714
    Scenario: Sync hidden files
        Given user "Alice" has created folder "folder" in the server
        And user "Alice" has created folder "folder/subfolder" in the server
        And user "Alice" has uploaded file with content "lorem" to ".roothide.txt" in the server
        And user "Alice" has uploaded file with content "lorem" to "folder/.hidefile.txt" in the server
        And user "Alice" has uploaded file with content "lorem" to "folder/subfolder/.hidesubfile.txt" in the server
        And user "Alice" has created the following folders inside the sync folder:
            | foldername        |
            | localFol          |
            | localFol/localsub |
        And user "Alice" has created the following files inside the sync folder:
            | filename                            | content |
            | .localroothide.txt                  | local   |
            | localFol/.localhide.txt             | local   |
            | localFol/localsub/.localsubhide.txt | local   |
        And user "Alice" has set up a client with default settings
        When the user enables sync of hidden files in the settings
        And the user opens the account "Alice"
        And the user waits for the files to sync
        Then the following files should exist on the file system with the content:
            | filename                          | content |
            | .roothide.txt                     | lorem   |
            | folder/.hidefile.txt              | lorem   |
            | folder/subfolder/.hidesubfile.txt | lorem   |
        And as "Alice" file ".localroothide.txt" should exist in the server
        And as "Alice" file "localFol/.localhide.txt" should exist in the server
        And as "Alice" file "localFol/localsub/.localsubhide.txt" should exist in the server

        # add/update files
        When user "Alice" uploads file with content "lorem" to ".newhide.txt" in the server
        And user "Alice" uploads file with content "lorem-edited" to ".roothide.txt" in the server
        And user "Alice" uploads file with content "lorem-edited" to "folder/.hidefile.txt" in the server
        And user "Alice" uploads file with content "lorem-edited" to "folder/subfolder/.hidesubfile.txt" in the server
        And user "Alice" updates the content of the following files inside the sync folder:
            | filename                            | content      |
            | .newlocalhide.txt                   | local        |
            | .localroothide.txt                  | local-edited |
            | localFol/.localhide.txt             | local-edited |
            | localFol/localsub/.localsubhide.txt | local-edited |
        And the user waits for the files to sync without force sync
        Then the following files should exist on the file system with the content:
            | filename                          | content      |
            | .newhide.txt                      | lorem        |
            | .roothide.txt                     | lorem-edited |
            | folder/.hidefile.txt              | lorem-edited |
            | folder/subfolder/.hidesubfile.txt | lorem-edited |
        And as "Alice" the file ".newlocalhide.txt" should have the content "local" in the server
        And as "Alice" the file ".localroothide.txt" should have the content "local-edited" in the server
        And as "Alice" the file "localFol/.localhide.txt" should have the content "local-edited" in the server
        And as "Alice" the file "localFol/localsub/.localsubhide.txt" should have the content "local-edited" in the server

    @issue-714 @skip
    Scenario: Sync hidden folders
        Given user "Alice" has created folder "parent" in the server
        And user "Alice" has created folder ".folder" in the server
        And user "Alice" has created folder "parent/.subfolder" in the server
        And user "Alice" has uploaded file with content "lorem" to ".folder/file.txt" in the server
        And user "Alice" has uploaded file with content "lorem" to "parent/.subfolder/subfile.txt" in the server
        And user "Alice" has created the following folders inside the sync folder:
            | foldername             |
            | localFol               |
            | .localhideFol          |
            | localFol/.localhidesub |
        And user "Alice" has created the following files inside the sync folder:
            | filename                                | content |
            | .localhideFol/localfile.txt             | local   |
            | localFol/.localhidesub/localsubfile.txt | local   |
        And user "Alice" has set up a client with default settings
        When the user enables sync of hidden files in the settings
        And the user opens the account "Alice"
        And the user waits for the files to sync without force sync
        Then the following files should exist on the file system with the content:
            | filename                      | content |
            | .folder/file.txt              | lorem   |
            | parent/.subfolder/subfile.txt | lorem   |
        And as "Alice" file ".localhideFol/localfile.txt" should exist in the server
        And as "Alice" file "localFol/.localhidesub/localsubfile.txt" should exist in the server

        # add/update files
        When user "Alice" creates folder ".newfolder" in the server
        And user "Alice" uploads file with content "lorem" to ".newfolder/newfile.txt" in the server
        And user "Alice" uploads file with content "lorem-edited" to ".folder/file.txt" in the server
        And user "Alice" uploads file with content "lorem-edited" to "parent/.subfolder/subfile.txt" in the server
        And user "Alice" creates a folder ".newlocal" inside the sync folder
        And user "Alice" creates the following files inside the sync folder:
            | filename                   | content |
            | .newlocal/newlocalfile.txt | local   |
        And user "Alice" updates the content of the following files inside the sync folder:
            | filename                                | content      |
            | .localhideFol/localfile.txt             | local-edited |
            | localFol/.localhidesub/localsubfile.txt | local-edited |
        And the user waits for the files to sync without force sync
        Then the following files should exist on the file system with the content:
            | filename                      | content      |
            | .newfolder/newfile.txt        | lorem        |
            | .folder/file.txt              | lorem-edited |
            | parent/.subfolder/subfile.txt | lorem-edited |
        And as "Alice" the file ".newlocal/newlocalfile.txt" should have the content "local" in the server
        And as "Alice" the file ".localhideFol/localfile.txt" should have the content "local-edited" in the server
        And as "Alice" the file "localFol/.localhidesub/localsubfile.txt" should have the content "local-edited" in the server
