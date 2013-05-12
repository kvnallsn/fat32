FAT32 for Xinu
=========

This is part of a group project to port Fat32 to the Xinu OS.  It will probably not be the best implementation, but it'll work

Currently Support Features
=========
  - Read/Write in the Root Directory
  - Standards Complient (Mostly, Windows won't complain about files it creates)
  - Basic subdirectory support in progess
  - Test shell for Linux

Building
=========
To build, just run make.  There are no external dependanices, so it should compile on any Unix-based system.  (Might have difficulty with opening/closing devices)
  
Usage
=========
This currently builds under Linux.  It includes an mkfs utility to format the specific revisioning filesystem (aka Skinny28).  Also included is a shell to test the functionality of the filesystem, a shell is included as well.  To use a FAT32 filesystem, just use your operating systems built in FAT32 formatter.  

mkfs
=========
To format a filesystem, run "mkfs 2G fsname" (./mkfs 2G fsname) to create a 2 Gigabyte filesystem name fsname. 
shell
=========
Started by running shell (or ./shell). The following commands are supported.
  - mount       Mount a filesystem
  - umount      Unmount a filesystem          
  - ls          List the contents the current directory
  - touch       Create a file
  - mkdir       Create a directory
  - cat         Print the contents of a file
  - cd          Change to a new directory    
  - rm          Remove a file
  - echo        Write a string to a file, overwriting what already exists
  - echoa       Write a string to a file, appending from the end of the file
  - revs        Print the revisions of a file       (Skinny28 Only)
  - revert      Revert a file to specific version   (Skinny28 Only)
  - printrev    Print a specific revision           (Skinny28 Only)
  - exit        Exit the shell

Version
-

0.00000Alpha

License
-

MIT