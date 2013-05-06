#!/bin/sh

sudo mkfs.vfat -n OSII /dev/sde1
sudo mount /dev/sde1/ /mnt/usb
sudo touch /mnt/usb/hello
sudo mkdir /mnt/usb/dir_test
sudo umount /mnt/usb
