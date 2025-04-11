# OpenCloud Desktop Client Manual Test Plan

## Account Management

Try everything in `Setup and Configuration` once with Keycloak and once with Lico (the single binary builtin IdP).

### Setup and Configuration
* Add a new account with valid credentials
* Attempt to add an account with invalid credentials
* Skip sync folder configuration during setup
* Configure multiple sync connections for one account

### Connection Management
* Pause and resume synchronization
* Remove a folder sync connection
* Remove an account

## Basic Synchronization
### Basic File Operations
* Create a new file locally and verify it syncs to server
* Download files from the server to local machine
* Modify a file locally and verify changes sync to server
* Modify a file on server and verify changes sync to local machine
* Delete a file locally and verify deletion syncs to server
* Delete a file on server and verify deletion syncs to local machine

### Folder Operations
* Create empty folders and verify they sync
* Create nested folder structures and verify they sync
* Sync folders with blanks or special characters in the name
* Verify long folder names sync correctly
* Delete folders and verify deletion syncs

### File Conflict Management
* Create a file conflict by modifying the same file both locally and on server
* Verify conflict resolution creates a proper conflict file
* Test conflict resolution options

## Advanced Synchronization
### Selective Sync
* Choose specific folders to sync during initial setup
* Change selective sync settings for existing account
* Verify unselected folders don't sync
* Test sorting folder list by name and size

### Large Files and Folders
* Sync files of different sizes (1MB, 100MB, 1GB)
* Sync folders containing many files (500+ files)
* Sync deeply nested folder structures

### Special Files
* Sync files with various extensions (.txt, .jpg, .png, .pdf, .mp3, .mp4)
* Test with very long filenames
* Test with special characters in filenames
* Test with files having spaces in names

## Sharing
### Public Link Sharing
* Create public link for a folder with different roles (Viewer, Editor, Contributor)
* Verify public link permissions work correctly
* Test password protection on public links
* Test expiration date on public links

### User and Group Sharing
* Share a folder with another user
* Modify permissions (read, edit, share) for a shared folder
* Remove permissions for a shared folder
* Verify permission changes reflect on server

## Project Spaces
### Space Access and Permissions
* Access a space with Viewer role and verify read-only permissions
* Access a space with Editor role and verify edit capabilities
* Access a space with Manager role and verify creation capabilities

### File Operations in Spaces
* Create files in spaces with appropriate permissions
* Verify restrictions when using spaces with read-only access
* Test that changes in spaces sync correctly

## Edge Cases and Error Handling
### File System Limitations
* Test with files that exceed server limitations
* Test with files/folders that have invalid names for the platform
* Test trailing spaces in filenames (platform-specific)

### Network Issues
* Test sync behavior when network is disconnected
* Test sync resumption after network reconnects
* Test client behavior during server unavailability
