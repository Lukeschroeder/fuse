/*
 *  Copyright (C) 2019 CS416 Spring 2019
 *	
 *	Tiny File System
 *
 *	File:	tfs.c
 *  Author: Yujie REN
 *	Date:	April 2019
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "tfs.h"

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here

struct superblock sb;



/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	bitmap_t ibitmap = (bitmap_t) malloc(4096);

	bio_read(sb.i_bitmap_blk, ibitmap);

	// Step 2: Traverse inode bitmap to find an available slot
	int ino = 0;
	while(ino < MAX_INUM && get_bitmap(ibitmap, ino)){
		ino++;		
	} 

	if(ino == MAX_INUM){
		printf("Out of space");
		return -1;
	} else {
		// Step 3: Update inode bitmap and write to disk 
		set_bitmap(ibitmap, ino);
		bio_write(sb.i_bitmap_blk, ibitmap);
		return ino;
	}

}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	// Step 1: Read data block bitmap from disk
	bitmap_t dbitmap = (bitmap_t) malloc(4096);
	memset(dbitmap, 0, 4096);
	bio_read(sb.d_bitmap_blk, dbitmap);

	// Step 2: Traverse data block bitmap to find an available slot
	int blockno = 0;	
	while(blockno < MAX_DNUM && get_bitmap(dbitmap, blockno)){
		blockno++;
	}
	if(blockno == MAX_DNUM){
		printf("Out of space");
		return -1;
	} else {
		// Step 3: Update data block bitmap and write to disk 
		set_bitmap(dbitmap, blockno);
		bio_write(sb.d_bitmap_blk, dbitmap);
		return blockno;
	}

}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

	// Step 1: Get the inode's on-disk block number
  	// Step 2: Get offset of the inode in the inode on-disk block
  	// Step 3: Read the block from disk and then copy into inode structure
	void * datablock = malloc(4096);	
	uint32_t index = sb.i_start_blk + ino;
	
	bio_read(index, datablock);
	
	struct inode * inodeptr = (struct inode *) datablock;
	memcpy(inode, inodeptr, sizeof(struct inode));
	

	return 0;
}

int writei(uint16_t ino, struct inode *inode) {


	// Step 1: Get the block number where this inode resides on disk
	// Step 2: Get the offset in the block where this inode resides on disk
	// Step 3: Write inode to disk 
	
	uint32_t index = sb.i_start_blk + ino;
	void * datablock = malloc(4096);
	memcpy(datablock, inode, sizeof(struct inode));
	bio_write(index, datablock);	

	return 0;
}


void printinode(struct inode * inode){
	printf("\n--------PRINTING INODE-------------\n");
	printf("Inode Number: %d\n", inode->ino);
	printf("Link Count: %d\n", inode->link);
	printf("Data Block Numbers: ");
	int i;
	for(i = 0; i < inode->link; i++) {
		printf("[%d] -> ", inode->direct_ptr[i]);
	} 
	printf("\n\n");
}


void printDirectoryContents(struct inode * dirinode) {
	printf("\n-------------DIRECTORY CONTENTS--------------\n");
	int i;
	for(i = 0; i < dirinode->link; i++) {
		void * datablock = malloc(4096);
		bio_read(dirinode->direct_ptr[i], datablock);
		
		struct dirent * direntptr = (struct dirent *) datablock;
		while((void *) direntptr <= datablock + 4096 - sizeof(struct dirent)) {
			if(direntptr->valid == 1) {
				printf("Filename: %s ", direntptr->name);
				printf("Inode: %d\n", direntptr->ino);			
			}
			direntptr++;
		} 

	}
	printf("\n");

}


struct dirent * getFnameDirent(struct inode dirinode, const char *fname) {
	
	void * datablock = malloc(4096);
	int i;
	struct dirent * datablockdirent;
	for(i = 0; i < dirinode.link; i++) {
	 	bio_read(dirinode.direct_ptr[i], datablock);

		datablockdirent = (struct dirent *) datablock;
		
		while((void *) datablockdirent <= datablock + 4096 - sizeof(struct dirent)){
			/*if(datablockdirent->valid == 1)  {
				printf("Accessing dirent with name: %s, ino: %d\n", datablockdirent->name, datablockdirent->ino);
			}*/
			if(datablockdirent->valid == 1 && !strcmp(fname, datablockdirent->name)){
				return datablockdirent;
			}
			datablockdirent++;
		}
	}
	return NULL;
}  




/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
	printf("\n-------------- CALLING DIR FIND ON FILE %s FROM INODE %d----------------\n", fname, ino);	


	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
	struct inode * dirinode = (struct inode *) malloc(sizeof(struct inode));
	readi(ino, dirinode);

	// Step 2: Get data block of current directory from inode
	// Step 3: Read directory's data block and check each directory entry.
	struct dirent * datablockdirent = getFnameDirent(*dirinode, fname);


	if(!datablockdirent) {
		printf("Data block dirent is nullington\n");
		return -1;
	} else {
		dirent->ino = datablockdirent->ino;
		dirent->valid = datablockdirent->valid;
		memcpy(dirent->name, fname, name_len);	
	}

	//If the name matches, then copy directory entry to dirent structure

	return 0;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	printf("\n------- CALLING DIR ADD ON FILE %s --------\n", fname);

	//Returns null if not already present, a dirent containing file info if present
	struct dirent * alreadyPresentDirent = getFnameDirent(dir_inode, fname);
	if(alreadyPresentDirent) {
		printf("File already present in dir\n");
		return -1;	
	}

	//Initialize stuff
	void *datablock = malloc(4096);
	int i;
	struct dirent * datablockdirent;
	
	//For each data block in the dir_inode
	for(i = 0; i < dir_inode.link; i++){

		//Read dirinode's ith block into datablock
		bio_read(dir_inode.direct_ptr[i], datablock);
		
		//Cast as dirent pointer
		datablockdirent = (struct dirent *) datablock;
	

		//While dirent pointer is in data block	
		while((void *) datablockdirent <= datablock + 4096 - sizeof(struct dirent)) {
			
			//If pointing to invalid dirent
			if(datablockdirent->valid == 0) {

				//Store new dirent in datablock		
				datablockdirent->valid = 1;
				datablockdirent->ino = f_ino;
				memset(datablockdirent->name, '\0', 252);
				memcpy(datablockdirent->name, fname, name_len);

				//Write datablock back to diskfile
				bio_write(dir_inode.direct_ptr[i], datablock);
				return 0;
			} else {
				//Increment datablockdirent pointer
				datablockdirent++;
			}
		}
		
	}

	
	// Allocate a new data block for this directory if it does not exist
	// Update directory inode
	// Write directory entry;
	

	//No free dirents in existing datablocks!!
	
	//Get current count of datablocks
	int datablockcount = dir_inode.link;
	printf("\nNo free data blocks");
	
	//If dirinode not full of datablocks
	if(datablockcount < 16) {
		printf(", adding datablock: %d\n", datablockcount);
		//Get free block
		int blockno = get_avail_blkno();	
		blockno += sb.d_start_blk;
	
		if(blockno < 0) {
			return blockno;
		}

		//update parent dir inode
		dir_inode.direct_ptr[datablockcount] = blockno;
		dir_inode.link = (uint32_t) datablockcount + 1;		

		writei(dir_inode.ino, &dir_inode);	

		//add data block with new dirent
		void * newdatablock = malloc(4096);
		struct dirent * newdirent = (struct dirent *) newdatablock;
		newdirent->valid = 1;
		newdirent->ino = f_ino;
		memset(newdirent->name, '\0', 252);
		memcpy(newdirent->name, fname, name_len);
		bio_write(blockno, newdirent);
		

		return 0;

	//Dirinode full
	} else {
		printf(", root directory full\n");
		return -1;	
	}


}





int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	// Step 2: Check if fname exist
	// Step 3: If exist, then remove it from dir_inode's data block and write to disk
	
	void * datablock = malloc(4096);
	int i;
	struct dirent * datablockdirent;
	for(i = 0; i < dir_inode.link; i++) {
	 	bio_read(dir_inode.direct_ptr[i], datablock);

		datablockdirent = (struct dirent *) datablock;
		
		while((void *) datablockdirent <= datablock + 4096 - sizeof(struct dirent)){
			/*if(datablockdirent->valid == 1)  {
				printf("Accessing dirent with name: %s, ino: %d\n", datablockdirent->name, datablockdirent->ino);
			}*/
			if(datablockdirent->valid == 1 && !strcmp(fname, datablockdirent->name)){
				datablockdirent->valid = 0;
				bio_write(dir_inode.direct_ptr[i], datablock);
				return 0;
			}
			datablockdirent++;
		}
	}
	return -1;	
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	return 0;
}


void testAddMethod() {
	//Initialize before add inode
	struct inode * rootinode = (struct inode *) malloc(sizeof(struct inode));	
	readi(sb.i_start_blk, rootinode);
	printinode(rootinode);

	//ADDING FILES
	int nfilestoadd = 100;
	char fname[256];
	uint64_t f_ino;

	strcpy(fname, "myfile");

	int i;
	for(i = 0; i < nfilestoadd; i++) {
		sprintf(fname + 6, "%d", i);
		fname[10] = '\0';
		f_ino = get_avail_ino();
		dir_add(*rootinode, f_ino, fname, strlen(fname));	
		readi(sb.i_start_blk, rootinode);
		//printinode(rootinode);
	}

	printDirectoryContents(rootinode);	


	//DELETING FILES
	int nfilestodelete = 47;
	char dfname[256];
	
	strcpy(dfname, "myfile");

	for(i = 0; i < nfilestodelete; i++) {
		sprintf(dfname + 6, "%d", i);
		dfname[10] = '\0';
		dir_remove(*rootinode, dfname, strlen(dfname));	
		readi(sb.i_start_blk, rootinode);
		//printinode(rootinode);
	}


	printDirectoryContents(rootinode);
	

	//ADDING DIFFERENT FILES
	int nfilestoreadd = 209;
	char rfname[256];	
	strcpy(rfname, "newguy");
	for(i = 0; i < nfilestoreadd; i++) {
		sprintf(rfname + 6, "%d", i);
		rfname[10] = '\0';
		f_ino = get_avail_ino();
		dir_add(*rootinode, f_ino, rfname, strlen(rfname));	
		readi(sb.i_start_blk, rootinode);
		//printinode(rootinode);
	}

	printDirectoryContents(rootinode);

	
	//FINDING FILE
	char * finddir = "newguy202";
	struct dirent * storage = (struct dirent *) malloc(sizeof(struct dirent));
	dir_find(sb.i_start_blk, finddir,strlen(finddir), storage);
	printf("Resulting dirent block: %s, %d\n", storage->name, storage->ino);
	
	
}



/* 
 * Make file system
 */
int tfs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile

	dev_init(diskfile_path);

	// write superblock information
	
	sb.magic_num = MAGIC_NUM;
	sb.max_inum = MAX_INUM;
	sb.max_dnum = MAX_DNUM;
	sb.i_bitmap_blk = 1;
	sb.d_bitmap_blk = 2;
	sb.i_start_blk = 3;
	sb.d_start_blk = 3 + MAX_INUM;

	bio_write(0, &sb);
	

	// initialize inode bitmap	
	// initialize data block bitmap
	bitmap_t ibitmap = (bitmap_t) malloc(4096);
	bitmap_t dbitmap = (bitmap_t) malloc(4096);

	bio_write(1, ibitmap);
	bio_write(2, dbitmap);
			
	// update bitmap information for root directory			
	struct inode * rootinode = (struct inode *) malloc(sizeof(struct inode));
	rootinode->ino = sb.i_start_blk;
	rootinode->valid = 1;
	rootinode->link = 0;
		
	writei(rootinode->ino, rootinode);
	
	return 0;
}






/* 
 * FUSE file operations
 */
static void *tfs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs

	if(dev_open(diskfile_path) < 0) {
		tfs_mkfs();	
		testAddMethod();	
	} else {
		bio_read(0, &sb);
		printf("Superblock Magic Num: %x", sb.magic_num);
	}

  // Step 1b: If disk file is found, just initialize in-memory data structures
  // and read superblock from disk

	return NULL;
}

static void tfs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures

	// Step 2: Close diskfile

}

static int tfs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path


	// Step 2: fill attribute of file into stbuf from inode

		stbuf->st_mode   = S_IFDIR | 0755;
		stbuf->st_nlink  = 2;
		time(&stbuf->st_mtime);

	return 0;
}

static int tfs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

    return 0;
}

static int tfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: Read directory entries from its data blocks, and copy them to filler

	return 0;
}


static int tfs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory

	// Step 5: Update inode for target directory

	// Step 6: Call writei() to write inode to disk
	

	return 0;
}

static int tfs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of target directory

	// Step 3: Clear data block bitmap of target directory

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

	return 0;
}

static int tfs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target file to parent directory

	// Step 5: Update inode for target file

	// Step 6: Call writei() to write inode to disk

	return 0;
}

static int tfs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

	return 0;
}

static int tfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
	return 0;
}

static int tfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk

	// Note: this function should return the amount of bytes you write to disk
	return size;
}

static int tfs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of target file

	// Step 3: Clear data block bitmap of target file

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

	return 0;
}

static int tfs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int tfs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations tfs_ope = {
	.init		= tfs_init,
	.destroy	= tfs_destroy,

	.getattr	= tfs_getattr,
	.readdir	= tfs_readdir,
	.opendir	= tfs_opendir,
	.releasedir	= tfs_releasedir,
	.mkdir		= tfs_mkdir,
	.rmdir		= tfs_rmdir,

	.create		= tfs_create,
	.open		= tfs_open,
	.read 		= tfs_read,
	.write		= tfs_write,
	.unlink		= tfs_unlink,

	.truncate   = tfs_truncate,
	.flush      = tfs_flush,
	.utimens    = tfs_utimens,
	.release	= tfs_release
};


int main(int argc, char *argv[]) {
	if(!strcmp(argv[1],"-simple")){

		getcwd(diskfile_path, PATH_MAX);
		strcat(diskfile_path, "/DISKFILE");
		tfs_init(NULL);

		return 0;	
	} else {

		int fuse_stat;
		getcwd(diskfile_path, PATH_MAX);
		strcat(diskfile_path, "/DISKFILE");
		fuse_stat = fuse_main(argc, argv, &tfs_ope, NULL);

		return fuse_stat;
	}
}

