/*
 * Reading a FAT disk image (an SD card .img): the files on it are copied
 * into a host folder, which then is the drive. That is how AmiCradle treats
 * images too: an image is where files come from, and the drive is a folder.
 *
 * Reads a whole-disk image with an MBR (first FAT partition) or a bare
 * FAT volume; FAT12, FAT16 and FAT32; long names. Never writes the image.
 */
#ifndef OTB_FATIMG_H
#define OTB_FATIMG_H

/* Copy every file and folder of the image's FAT volume into dir (made if
 * missing; files there with the same names are replaced). 0 on success;
 * *files counts the files written. */
int otb_img_extract(const char *img, const char *dir, long *files);

/* 1 when path is a file holding a FAT volume this reader understands. */
int otb_img_is_fat(const char *path);

#endif
