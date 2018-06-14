#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "ext2.h"

void sb(struct ext2_super_block);
struct ext2_super_block *get_super_block(int);

int main(int argc, char *argv[]) {
	char imgpath[255];
	struct ext2_super_block *super;
	int storage_device;

	if(argc <= 1) {
		fprintf(stderr, "Informe um arquivo de imagem.\n");
		return EXIT_FAILURE;
	}

	storage_device = open(argv[1], O_RDONLY);
	super = get_super_block(storage_device);
	sb(*super);

	// Free resources
	free(super);
	close(storage_device);
	return EXIT_SUCCESS;
}

struct ext2_super_block* get_super_block(int storage_device) {
	// Lê as informações do superbloco
	struct ext2_super_block *super = (struct ext2_super_block*)malloc(sizeof(struct ext2_super_block));
	lseek(storage_device, BASE_OFFSET, SEEK_SET);
	read(storage_device, super, sizeof(struct ext2_super_block));
	return super;
}

void sb(struct ext2_super_block super) {
	// Print superblock information
	printf("Inodes count: %d\n", super.s_inodes_count);
	printf("Blocks count: %d\n", super.s_blocks_count);
	printf("Reserved blocks: %d\n", super.s_r_blocks_count);
	printf("Free blocks: %d\n", super.s_free_blocks_count);
	printf("Free inodes: %d\n", super.s_free_inodes_count);
	printf("First data block: %d\n", super.s_first_data_block);
	printf("Block size: %d\n", super.s_log_block_size);
	printf("Blocks per group: %d\n", super.s_blocks_per_group);
	printf("Inodes per group: %d\n", super.s_inodes_per_group);
	printf("Magic signature: %d\n", super.s_magic);
	printf("First non-reserved inode: %d\n", super.s_first_ino);
	printf("Inode size: %d\n", super.s_inode_size);
}
