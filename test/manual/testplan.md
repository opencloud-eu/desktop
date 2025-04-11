# OpenCloud Desktop Client Manual Test Plan

## Account Management

Try everything in `Setup and Configuration` once with Keycloak and once with Lico (the single binary builtin IdP).

### Setup and Configuration
* Add a new account with valid credentials
* Attempt to add an account with invalid credentials
* Skip sync folder configuration during setup
* Second run: Configure sync folder configuration during setup

### Sync Connections
* Pause and resume synchronization on Personal and a project space
* Force a sync while an Upload to a different space is running
* Force a sync while a Download from a different space is running
* Remove a project space sync connection
* Remove an account
* Add an account directly after the removal
* Create a complicated folder tree in a project space and then configure selective sync 

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
### User and Group Sharing
* In web: Share a folder from another user, verify local sync
* In web: Modify permissions (view, edit) for a shared folder, try to modify files locally
* In web: Remove share and verify local sync

## Project Spaces
### Space Access and Permissions
* Gain access to a space with Viewer role (in web) and verify read-only permissions (local)
* Gain access to a space with Editor role (in web) and verify edit capabilities (local)
* Gain access to a space with Manager role (in web) and verify edit capabilities (local)

### File Operations in Spaces
* Create files in spaces with appropriate permissions
* Verify restrictions when using spaces with read-only access
* Test that changes in spaces sync correctly (add/delete/rename/duplicate/move)

## Edge Cases and Error Handling
### File System Limitations
* Test with files that exceed server limitations
* Test with files/folders that have invalid names for the platform
* Test trailing spaces in filenames (platform-specific)

### Network Issues
* Test sync behavior when network is disconnected
* Test sync resumption after network reconnects
* Test client behavior during server unavailability
