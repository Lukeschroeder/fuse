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
	//printf("\n-------------- CALLING DIR FIND ON FILE %s FROM INODE %d----------------\n", fname, ino);	


	// Step 1: Call readi() to get the inode using ino (inode number of current directory)
	struct inode * dirinode = (struct inode *) malloc(sizeof(struct inode));
	readi(ino, dirinode);

	// Step 2: Get data block of current directory from inode
	// Step 3: Read directory's data block and check each directory entry.
	struct dirent * datablockdirent = getFnameDirent(*dirinode, fname);


	if(!datablockdirent) {
		//printf("Data block dirent is nullington\n");
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

	//printf("\n------- CALLING DIR ADD ON FILE %s, INODE %d --------\n", fname, f_ino);

	//Returns null if not already present, a dirent containing file info if present
	struct dirent * alreadyPresentDirent = getFnameDirent(dir_inode, fname);
	if(alreadyPresentDirent) {
		//printf("File already present in dir\n");
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
	//printf("\nNo free data blocks");
	
	//If dirinode not full of datablocks
	if(datablockcount < 16) {
		//printf(", adding datablock: %d\n", datablockcount);
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
		//printf(", root directory full\n");
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





char * getParentDir(const char * path) {

	char * forwardslash = "/";
	if(!strcmp(path, forwardslash)){
		char * forwardslashpath = (char *) malloc(2);
		forwardslashpath[0] = '/';
		forwardslashpath[1] = '\0';
		return forwardslashpath;
	}

	int index = 1;
	while(path[index] != '\0' && path[index] != '/') {
		//printf("Path[%d] = %c\n", index, path[index]);
		index++;
	}

	//printf("Index: %d\n", index);
	char * parentDirString = (char *) malloc(index);	


	memcpy(parentDirString, path + 1, index - 1);
	parentDirString[index - 1] = '\0';
	return parentDirString;
}



/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way
	//printf("GNBP: %s, LENGTH: %d\n", path, strlen(path));	

	char * forwardslash = "/";
	
	//Path is "/"
	if(!strcmp(forwardslash, (char *) path)) {
		
		readi(0, inode);
		

	//Path contains subentries	
	} else {

		//Get name of top level directory entry to access
		char * direntName = getParentDir(path);		

		
		int parentlength = strlen(direntName) + 1;
		int totallength = strlen(path);
		int childlength = totallength - parentlength;

		/*printf("Parent Length: %d\n", parentlength);
		printf("Child Length: %d\n", childlength);
		printf("Dirent Name: %s\n", direntName);*/	


		//Get Inode number of top level directory entry
		//printf("ACCESSING DIRENT %s\n", direntName);
		struct dirent * storage = (struct dirent *) malloc(sizeof(struct dirent));
		int found = dir_find(ino, direntName, strlen(direntName), storage);
		if(found < 0) {
			return -1;
		}

		uint16_t direntino = storage->ino; 			

		//Top level directory entry is child inode to load
		if(!childlength) {

			//Read in child inode
			readi(direntino, inode);

		//Top level directory entry is parent inode 	
		} else {

			char * childstring = (char *) malloc(childlength + 1);
			memcpy(childstring, path + parentlength, childlength);
			childstring[childlength] = '\0';
			return get_node_by_path((const char *) childstring, direntino, inode);
			//CALL GET NODE BY PATH ON 
		}		

	}


	return 0;
}


void testGetNodeByPath() {

	//Read current rootinode
	struct inode * rootinode = (struct inode *) malloc(sizeof(struct inode));
	readi(0, rootinode);


	int nDirectoriesToNest = 7;
	char fname[256];
	strcpy(fname, "dir");
	uint16_t f_ino;
	struct inode * finode;
	

	
	int i;
	for(i = 0; i < nDirectoriesToNest; i++){

		//initialize papadir inode
		f_ino = get_avail_ino();
		sprintf(fname+3, "%d", i);
		fname[5] = '\0';
		printf("Adding direntry: %s", fname);
		finode = (struct inode *) malloc(sizeof(struct inode));
		finode->ino = f_ino;
		finode->valid = 1;
		finode->link = 0;
	

		//Write papadir inode to block
		writei(f_ino, finode);
	 

		//Add Papadirino to directory jawn
		dir_add(*rootinode, f_ino, fname, strlen(fname));

	
		//Read papadir as rootinode
		readi(f_ino, rootinode);

	}

	//Get node by path specified
	rootinode = (struct inode *) malloc(sizeof(struct inode));
	char * pathName = "/dir0/dir1/dir2/dir3";
	get_node_by_path(pathName, 0, rootinode);

	//Initialize new inode
	f_ino = get_avail_ino();
	strcpy(fname, "yousonice");
	finode = (struct inode *) malloc(sizeof(struct inode));
	finode->ino = f_ino;
	finode->valid = 1;
	finode->link = 0;
	

	//Write yousonice inode to block
	writei(f_ino, finode);

	//Add yousonice to directory dir3
	dir_add(*rootinode, f_ino, fname, strlen(fname));

	pathName = "/dir0/dir1/dir2/dir3/yousonice";
	get_node_by_path(pathName, 0, rootinode);
	
	if(rootinode->valid) {
		//printf("Storage inode number: %d\n", rootinode->ino);
	} else {
		//printf("Back to the debugging grind son\n");
	}
	
}



void testAddMethod() {
	//Initialize before add inode
	struct inode * rootinode = (struct inode *) malloc(sizeof(struct inode));	
	readi(0, rootinode);
	printinode(rootinode);

	//ADDING FILES
	int nfilestoadd = 100;
	char fname[256];
	uint16_t f_ino;

	strcpy(fname, "myfile");

	int i;
	for(i = 0; i < nfilestoadd; i++) {
		sprintf(fname + 6, "%d", i);
		fname[10] = '\0';
		f_ino = get_avail_ino();
		dir_add(*rootinode, f_ino, fname, strlen(fname));	
		readi(0, rootinode);
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
		readi(0, rootinode);
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
		readi(0, rootinode);
		//printinode(rootinode);
	}

	printDirectoryContents(rootinode);

	
	//FINDING FILE
	char * finddir = "newguy202";
	struct dirent * storage = (struct dirent *) malloc(sizeof(struct dirent));
	dir_find(0, finddir,strlen(finddir), storage);
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
	set_bitmap(ibitmap, 0);

	bio_write(sb.i_bitmap_blk, ibitmap);
	bio_write(sb.d_bitmap_blk, dbitmap);
			
	// update bitmap information for root directory			
	struct inode * rootinode = (struct inode *) malloc(sizeof(struct inode));
	rootinode->ino = 0;
	rootinode->valid = 1;
	rootinode->link = 0;
	
	rootinode->vstat.st_ino = 0;
	rootinode->vstat.st_mode   = S_IFDIR | 0755;
	rootinode->vstat.st_blksize = 4096;
	rootinode->vstat.st_size = 0;
	rootinode->vstat.st_blocks = 0;
		
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
		//testGetNodeByPath();
	} else {
		bio_read(0, &sb);
		//printf("Superblock Magic Num: %x", sb.magic_num);
	}

  	// Step 1b: If disk file is found, just initialize in-memory data structures
  	// and read superblock from disk

	return NULL;
}

static void tfs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	dev_close();
	// Step 2: Close diskfile

}

static int tfs_getattr(const char *path, struct stat *stbuf) {


	printf("\nCALLING GET ATTR ON PATH: %s\n", path);
	// Step 1: call get_node_by_path() to get inode from path
	struct inode * inode = (struct inode *) malloc(sizeof(struct inode));
	int found = get_node_by_path(path, 0, inode);

	//printf("Filling Attribute: %d\n", inode->ino);

	if(found < 0) {
		//printf("Path %s attributes not found\n", path);
		return -ENOENT;
	} else {
		//printf("Path %s attributes found\n", path);
		memcpy(stbuf, &inode->vstat, sizeof(struct stat));
		return 0;
	}

}

static int tfs_opendir(const char *path, struct fuse_file_info *fi) {


	//printf("\nCALLING OPEN DIR: %s\n", path);

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode * inode = (struct inode *) malloc(sizeof(struct inode));
	int found = get_node_by_path(path, 0, inode);

	//printf("Node NumberJawn: %d\n", inode->ino);
	
	if(found < 0) {
		return -ENOENT;
	} else {
		return 0;
	}
	

	// Step 2: If not find, return -1
}

static int tfs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {


	//printf("\nCALLING READ DIR ON PATH: %s\n", path);

	// Step 1: Call get_node_by_path() to get inode from path
	struct inode * dirinode = (struct inode *) malloc(sizeof(struct inode));
	int found = get_node_by_path(path, 0, dirinode);
	
	if(found < 0) {
		return -ENOENT;
	}

	// Step 2: Read directory entries from its data blocks, and copy them to filler
	void * datablock = malloc(4096);
	int i;
	struct dirent * datablockdirent;
	for(i = 0; i < dirinode->link; i++) {
	 	bio_read(dirinode->direct_ptr[i], datablock);

		datablockdirent = (struct dirent *) datablock;
		
		while((void *) datablockdirent <= datablock + 4096 - sizeof(struct dirent)){
			if(datablockdirent->valid == 1){
				filler(buffer, datablockdirent->name, NULL, 0);
			}
			datablockdirent++;
		}
	}


	return 0;
}


static int tfs_mkdir(const char *path, mode_t mode) {
	//printf("\nCALLING MAKE DIR ON PATH: %s\n", path);
	

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	int pathlength = strlen(path);

	char * pathcopy1 = (char *) malloc(pathlength + 1);
	char * pathcopy2 = (char *) malloc(pathlength + 1);

	memcpy(pathcopy1, (char *) path, pathlength);
	memcpy(pathcopy2, (char *) path, pathlength);

	pathcopy1[pathlength] = '\0';
	pathcopy2[pathlength] = '\0';

	
	char * parentname = dirname(pathcopy1);
	char * childname = basename(pathcopy2);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode * dirinode = (struct inode *) malloc(sizeof(struct inode));
	int found = get_node_by_path(parentname, 0, dirinode);

	if(found < 0) {
		return -ENOENT;
	} else {
		// Step 3: Call get_avail_ino() to get an available inode number

		uint16_t ino = get_avail_ino();

		// Step 4: Call dir_add() to add directory entry of target directory to parent directory

		dir_add(*dirinode, ino, childname, strlen(childname));

		// Step 5: Update inode for target directory
	

		//printf("ADDING CHILD ENTRY %s, INODE NUMBER %d, TO DIRINODE %d\n", childname, ino, dirinode->ino);

		// Step 6: Call writei() to write inode to disk
		struct inode * childinode = (struct inode *) malloc(sizeof(struct inode));
		childinode->ino = ino;
		childinode->valid = 1;
		childinode->link = 0;
		childinode->size = 0;
	
		childinode->vstat.st_ino = ino;
		childinode->vstat.st_mode   = S_IFDIR | 0755;
		childinode->vstat.st_blksize = 4096;
		childinode->vstat.st_size = 0;
		childinode->vstat.st_blocks = 0;
		//childnode->vstat.st_nlink  = 2;
		//time(&stbuf->st_mtime);
	
		writei(ino, childinode);
		return 0;
	}

}





static int tfs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	int pathlength = strlen(path);

	char * pathcopy1 = (char *) malloc(pathlength + 1);
	char * pathcopy2 = (char *) malloc(pathlength + 1);

	memcpy(pathcopy1, (char *) path, pathlength);
	memcpy(pathcopy2, (char *) path, pathlength);

	pathcopy1[pathlength] = '\0';
	pathcopy2[pathlength] = '\0';

	
	char * parentname = dirname(pathcopy1);
	char * childname = basename(pathcopy2);


	// Step 2: Call get_node_by_path() to get inode of target directory
	struct inode * targetinode = (struct inode *) malloc(sizeof(struct inode));
	int found = get_node_by_path(path, 0, targetinode);


	if(found < 0) {
		return -ENOENT;
	} else {
		
		
		// Step 3: Clear data block bitmap of target directory
		bitmap_t dbitmap = (bitmap_t) malloc(4096);
		bio_read(sb.d_bitmap_blk, dbitmap);
		int i;
		for(i = 0; i < targetinode->link; i++) {
	 		unset_bitmap(dbitmap, targetinode->direct_ptr[i] - sb.d_bitmap_blk);
		}
		bio_write(sb.d_bitmap_blk, dbitmap);


		
		// Step 4: Clear inode bitmap and its data block		
		targetinode->valid = 0;
		writei(targetinode->ino, targetinode);
		
		bitmap_t ibitmap = (bitmap_t) malloc(4096);
		bio_read(sb.i_bitmap_blk, ibitmap);
		unset_bitmap(ibitmap, targetinode->ino);
		bio_write(sb.i_bitmap_blk, ibitmap);

		// Step 5: Call get_node_by_path() to get inode of parent directory
		struct inode * dirinode = (struct inode *) malloc(sizeof(struct inode));
		found = get_node_by_path(parentname, 0, dirinode);

		if(found < 0) {
			return -ENOENT;
		} else {
		
			// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory
			return dir_remove(*dirinode, childname, strlen(childname));
		}

	}
}

static int tfs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int tfs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	int pathlength = strlen(path);

	char * pathcopy1 = (char *) malloc(pathlength + 1);
	char * pathcopy2 = (char *) malloc(pathlength + 1);

	memcpy(pathcopy1, (char *) path, pathlength);
	memcpy(pathcopy2, (char *) path, pathlength);

	pathcopy1[pathlength] = '\0';
	pathcopy2[pathlength] = '\0';

	
	char * parentname = dirname(pathcopy1);
	char * childname = basename(pathcopy2);

	// Step 2: Call get_node_by_path() to get inode of parent directory
	struct inode * dirinode = (struct inode *) malloc(sizeof(struct inode));
	int found = get_node_by_path(parentname, 0, dirinode);

	if(found < 0) {
		return -ENOENT;
	} else {
		// Step 3: Call get_avail_ino() to get an available inode number

		uint16_t ino = get_avail_ino();

		// Step 4: Call dir_add() to add directory entry of target directory to parent directory

		dir_add(*dirinode, ino, childname, strlen(childname));

		// Step 5: Update inode for target directory
	

		//printf("ADDING CHILD ENTRY %s, INODE NUMBER %d, TO DIRINODE %d\n", childname, ino, dirinode->ino);

		// Step 6: Call writei() to write inode to disk
		struct inode * childinode = (struct inode *) malloc(sizeof(struct inode));
		childinode->ino = ino;
		childinode->valid = 1;
		childinode->link = 0;	
		childinode->size = 0;	
		childinode->vstat.st_ino = ino;
		childinode->vstat.st_mode   = S_IFREG | 0755;
		childinode->vstat.st_blksize = 4096;
		childinode->vstat.st_size = 0;
		childinode->vstat.st_blocks = 0;
		//childnode->vstat.st_nlink  = 2;
		//time(&stbuf->st_mtime);
	
		writei(ino, childinode);
		return 0;
	}


	return 0;
}

static int tfs_open(const char *path, struct fuse_file_info *fi) {
	//printf("\n---------------CALLING TFS OPEN-----------\n");
	// Step 1: Call get_node_by_path() to get inode from path
	struct inode * inode = (struct inode *) malloc(sizeof(struct inode));
	int found = get_node_by_path(path, 0, inode);

	if(found < 0) {
		return -ENOENT;
	} else {
		return 0;
	}
	
}

static int tfs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path
	//printf("\n---------------CALLING TFS READ PATH: %s, SIZE: %d, OFFSET: %d\n", path, size, offset);
	struct inode * dirinode = (struct inode *) malloc(sizeof(struct inode));
	int found = get_node_by_path(path, 0, dirinode);
	
	if(found < 0) {
		return -ENOENT;
	} else {
	
		if(size == 0){
			return 0;
		}
		
		int bytenumber;
		int blocknumber;
		int written = 0;

		void * datablock = malloc(4096);
		printf("Link count: %d\n", dirinode->link);	
		for(blocknumber = 0; blocknumber < dirinode->link; blocknumber++){
			bio_read(dirinode->direct_ptr[blocknumber], datablock); 
			for(bytenumber = 0; bytenumber < 4096; bytenumber++){
				if(bytenumber + blocknumber*4096 >= offset && bytenumber + blocknumber*4096 < size + offset) {
					memcpy(buffer + written, datablock + bytenumber, 1); 		
					written++;
					if(written == size){		
						printf("Read: %s\n from block %d\n", (char *) datablock, dirinode->direct_ptr[blocknumber]);	
						return written;
					}
				}
			}
		}
		
		return -1;
	}

	// Step 2: Based on size and offset, read its data blocks from disk	

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
}

static int tfs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	
	printf("\n---------------CALLING TFS WRITE PATH: %s, SIZE: %d, OFFSET: %d\n", path, size, offset);

	// Step 1: You could call get_node_by_path() to get inode from path
	struct inode * dirinode = (struct inode *) malloc(sizeof(struct inode));
	int found = get_node_by_path(path, 0, dirinode);


	
	if(found < 0) {
		return -ENOENT;
	} else {
		if(size == 0){
			return 0;
		}

		int bytenumber;
		int blocknumber;
		void * datablock = malloc(4096);
		int read = 0;

		for(blocknumber = 0; blocknumber < 16; blocknumber++){
			if(blocknumber == dirinode->link){
				readi(dirinode->ino, dirinode);
				dirinode->link += 1;
				int blockno = get_avail_blkno();
				blockno += sb.d_start_blk;			
				dirinode->direct_ptr[blocknumber] = blockno;
				writei(dirinode->ino, dirinode);
			}
		
			for(bytenumber = 0; bytenumber < 4096; bytenumber++){	
				if(bytenumber + blocknumber*4096 >= offset && bytenumber + blocknumber*4096 < offset + size) {
					memcpy(datablock + bytenumber, buffer + read, 1);
					read++;
					if(read == size) {
						readi(dirinode->ino, dirinode);
						dirinode->size += size;
						dirinode->vstat.st_size += size;
						writei(dirinode->ino, dirinode);
						bio_write(dirinode->direct_ptr[blocknumber], datablock);
						printf("Write: %s\n in blocknumber%d\n", (char *) datablock, dirinode->direct_ptr[blocknumber]);
						return read;
					}			

				}					
			}
			bio_write(dirinode->direct_ptr[blocknumber], datablock);
		}
		
		return -1;
	}


	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk

	// Note: this function should return the amount of bytes you write to disk
}

static int tfs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	int pathlength = strlen(path);

	char * pathcopy1 = (char *) malloc(pathlength + 1);
	char * pathcopy2 = (char *) malloc(pathlength + 1);

	memcpy(pathcopy1, (char *) path, pathlength);
	memcpy(pathcopy2, (char *) path, pathlength);

	pathcopy1[pathlength] = '\0';
	pathcopy2[pathlength] = '\0';

	
	char * parentname = dirname(pathcopy1);
	char * childname = basename(pathcopy2);


	// Step 2: Call get_node_by_path() to get inode of target file
	struct inode * targetinode = (struct inode *) malloc(sizeof(struct inode));
	int found = get_node_by_path(path, 0, targetinode);


	if(found < 0) {
		return -ENOENT;
	} else {
		
		
		// Step 3: Clear data block bitmap of target file
		bitmap_t dbitmap = (bitmap_t) malloc(4096);
		bio_read(sb.d_bitmap_blk, dbitmap);
		int i;
		for(i = 0; i < targetinode->link; i++) {
	 		unset_bitmap(dbitmap, targetinode->direct_ptr[i] - sb.d_bitmap_blk);
		}
		bio_write(sb.d_bitmap_blk, dbitmap);


		
		// Step 4: Clear inode bitmap and its data block		
		targetinode->valid = 0;
		writei(targetinode->ino, targetinode);
		
		bitmap_t ibitmap = (bitmap_t) malloc(4096);
		bio_read(sb.i_bitmap_blk, ibitmap);
		unset_bitmap(ibitmap, targetinode->ino);
		bio_write(sb.i_bitmap_blk, ibitmap);

		// Step 5: Call get_node_by_path() to get inode of parent directory
		struct inode * dirinode = (struct inode *) malloc(sizeof(struct inode));
		found = get_node_by_path(parentname, 0, dirinode);

		if(found < 0) {
			return -ENOENT;
		} else {
		
			// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory
			return dir_remove(*dirinode, childname, strlen(childname));
		}

	}

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

