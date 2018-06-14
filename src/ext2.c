#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <sys/stat.h>
#include <string.h>
#include "ext2.h"

// Variável global para guardar o storage_device que vai ser único
int storage_device;

void sb(struct ext2_super_block);
struct ext2_super_block *get_super_block();
struct ext2_group_desc* get_group_desc(int group, struct ext2_super_block super);
int calculate_nr_groups(struct ext2_super_block super); 
int block_size(struct ext2_super_block super);
int inode_group(int inode, struct ext2_super_block super);
int inode_index(int inode, struct ext2_super_block super);
int inode_block(int inode, struct ext2_super_block super);
int block_offset(int block, struct ext2_super_block super);
struct ext2_inode* get_root_directory(struct ext2_super_block super);
int roundup(double);
void print_inode_info(struct ext2_inode inode);
void ls(struct ext2_inode inode, struct ext2_super_block super);

int main(int argc, char *argv[]) {
	char imgpath[255];
	struct ext2_super_block *super;
	struct ext2_inode *curdir;

	if(argc <= 1) {
		fprintf(stderr, "Informe um arquivo de imagem.\n");
		return EXIT_FAILURE;
	}

	if((storage_device = open(argv[1], O_RDONLY)) < 0) {
		fprintf(stderr, "Erro ao abir o arquivo %s, certifique-se que ele existe\n", argv[1]);
		return EXIT_FAILURE;
	}
	super = get_super_block(storage_device);
	if(super->s_magic != (__le16)EXT2_SUPER_MAGIC) {
		fprintf(stderr, "A imagem %s não corresponde a um formato ext2\n", argv[1]);
		fprintf(stderr, "EXT2_SUPER_MAGIC: 0x%hx\n", (__le16)EXT2_SUPER_MAGIC);
		fprintf(stderr, "Magic do superblock: 0x%hx\n", super->s_magic);
		return EXIT_FAILURE;
	}
	sb(*super);
	curdir = get_root_directory(*super);
	print_inode_info(*curdir);
	ls(*curdir, *super);

	// Free resources
	free(super);
	free(curdir);
	close(storage_device);
	return EXIT_SUCCESS;
}

struct ext2_super_block* get_super_block() {
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
	printf("Magic signature: 0x%hx\n", super.s_magic);
	printf("First non-reserved inode: %d\n", super.s_first_ino);
	printf("Inode size: %d\n", super.s_inode_size);
}

struct ext2_group_desc* get_group_desc(int group, struct ext2_super_block super) {
	// TODO apenas o primeiro grupo é retornado... usar group para calcular o offset
	struct ext2_group_desc *gd = (struct ext2_group_desc*)malloc(sizeof(struct ext2_group_desc));
	lseek(storage_device, BASE_OFFSET + block_size(super), SEEK_SET);
	read(storage_device, gd, sizeof(*gd));
	return gd;
}

int calculate_nr_groups(struct ext2_super_block super) {
	// Calcula quantos grupos de blocos há no disco
	// A conta é feita pelo número de inodes e de blocks
	// Caso haja uma diferença há um erro no superbloco e um valor negativo é retornado
	int nr_groups_inodes = roundup((double)super.s_inodes_count / (double)super.s_inodes_per_group);
	int nr_groups_blocks = roundup((double)super.s_blocks_count / (double)super.s_blocks_per_group);
	if(nr_groups_inodes != nr_groups_blocks) {
		fprintf(stderr, "Erro no superbloco, o número de grupos não pode ser calculado.\n");
		return -1;
	}
	return nr_groups_inodes;
}

int block_size(struct ext2_super_block super) {
	return 1024 << super.s_log_block_size;
}

int inode_group(int inode, struct ext2_super_block super) {
	return (inode - 1) / super.s_inodes_per_group;
}

int inode_index(int inode, struct ext2_super_block super) {
	return (inode - 1) % super.s_inodes_per_group;
}

int inode_block(int inode, struct ext2_super_block super) {
	return (inode_index(inode, super) * super.s_inode_size) / block_size(super);
}

int block_offset(int block, struct ext2_super_block super) {
	return BASE_OFFSET + (block - 1) * block_size(super);
}

struct ext2_inode* get_root_directory(struct ext2_super_block super) {
	// O diretório root fica no inode 2 https://wiki.osdev.org/Ext2
	struct ext2_group_desc *gd = get_group_desc(inode_group(EXT2_ROOT_INO, super), super);
	struct ext2_inode *inode = (struct ext2_inode*) malloc(sizeof(struct ext2_inode));
	lseek(
		storage_device,
		block_offset(gd->bg_inode_table, super) + (EXT2_ROOT_INO - 1) * sizeof(struct ext2_inode),
		SEEK_SET
	);
	read(storage_device, inode, sizeof(*inode));
	free(gd);
	return inode;
}

int roundup(double x) {
	if(x > (int)x) {
		return (int)(x+1);
	}
	return (int)x;
}

void print_inode_info(struct ext2_inode inode) {
	printf("File mode: %d\n", inode.i_mode);
	if(S_ISDIR(inode.i_mode)) {
		printf("DIretório!\n");
	}
	printf("Owner: %d\n", inode.i_uid);
	printf("Size: %d\n", inode.i_size);
	printf("Access time: %d\n", inode.i_atime);
	printf("Creation time: %d\n", inode.i_ctime);
	printf("Modification time: %d\n", inode.i_mtime);
	printf("Deletion time: %d\n", inode.i_dtime);
	printf("Group: %d\n", inode.i_gid);
	printf("Links: %d\n", inode.i_links_count);
	printf("Blocks: %d\n", inode.i_blocks);
	printf("Flags: %d\n", inode.i_flags);
}

void ls(struct ext2_inode inode, struct ext2_super_block super) {
	if(!S_ISDIR(inode.i_mode)) {
		fprintf(stderr, "Diretório inválido.\n");
		return;
	}
	struct ext2_dir_entry_2 *entry;
	unsigned int bytes_read = 0;
	int bs = block_size(super);
	void *block = malloc(bs);

	lseek(storage_device, block_offset(inode.i_block[0], super), SEEK_SET);
	read(storage_device, block, bs);
	printf("LS\n");
	entry = (struct ext2_dir_entry_2 *)block;
	while(bytes_read < inode.i_size && entry->inode) {
		char file_name[EXT2_NAME_LEN+1];
		memcpy(file_name, entry->name, entry->name_len);
		file_name[entry->name_len] = '\0';
		printf("%s\t", file_name);
		entry = (void*) entry +  entry->rec_len;
		bytes_read += entry->rec_len;
	}
	printf("\n");
	free(block);
}
