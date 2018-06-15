#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
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
struct ext2_inode* read_inode(__le32 inode_nr, struct ext2_super_block super);
struct ext2_inode* get_inode_by_name(struct ext2_inode root, char *name, struct ext2_super_block super);

int main(int argc, char *argv[]) {
	char imgpath[255];
	struct ext2_super_block *super;
	struct ext2_inode *curdir, *dir;

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
	ls(*curdir, *super);
	dir = get_inode_by_name(*curdir, "documents", *super);
	ls(*dir, *super);

	// Free resources
	free(super);
	free(curdir);
	free(dir);
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
	lseek(
		storage_device,
		BASE_OFFSET + block_size(super) + (group)*super.s_blocks_per_group*block_size(super),
		SEEK_SET
	);
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
	return read_inode(EXT2_ROOT_INO, super);
}

struct ext2_inode* read_inode(__le32 inode_nr, struct ext2_super_block super) {
	int group = inode_group(inode_nr, super);
	int index = inode_index(inode_nr, super);
	struct ext2_group_desc *gd = get_group_desc(group, super);
	long int offset = block_offset(
		gd->bg_inode_table + group*super.s_blocks_per_group, super
	) + index * sizeof(struct ext2_inode);
	struct ext2_inode *inode = (struct ext2_inode*) malloc(sizeof(struct ext2_inode));
	lseek(storage_device, offset, SEEK_SET);
	read(storage_device, inode, sizeof(*inode));
	free(gd);
	return inode;
}

struct ext2_inode* get_inode_by_name(struct ext2_inode root, char *name, struct ext2_super_block super) {
	struct ext2_inode *inode = NULL;
	if(!S_ISDIR(root.i_mode)) {
		fprintf(stderr, "Diretório root inválido.\n");
		return inode;
	}
	struct ext2_dir_entry_2 *entry;
	unsigned int bytes_read = 0;
	int bs = block_size(super);
	void *block = malloc(bs);
	char file_name[EXT2_NAME_LEN+1];
	int inode_nr;

	lseek(storage_device, block_offset(root.i_block[0], super), SEEK_SET);
	read(storage_device, block, bs);
	entry = (struct ext2_dir_entry_2*)block;
	while(bytes_read < root.i_size && entry->inode) {
		memcpy(file_name, entry->name, entry->name_len);
		file_name[entry->name_len] = '\0';
		if(!strcmp(file_name, name)) {
			// inode encontrado
			inode_nr = entry->inode;
			printf("Inode %s encontrado: %d\n", name, entry->inode);
			free(block);
			return read_inode(inode_nr, super);
		}
		bytes_read += entry->rec_len;
		entry = (void*) entry + entry->rec_len;
	}
}

int roundup(double x) {
	if(x > (int)x) {
		return (int)(x+1);
	}
	return (int)x;
}

void print_inode_info(struct ext2_inode inode) {
	char time_string[100];
	time_t date;
	char *format = "%Y-%m-%d %H:%M:%S %Z00";
	printf("File mode: %d\n", inode.i_mode);
	if(S_ISDIR(inode.i_mode)) {
		printf("Diretório!\n");
	}
	printf("Size: %d\t", inode.i_size);
	printf("Blocks: %d\t", inode.i_blocks);
	printf("IO Block: \t");
	if(S_ISDIR(inode.i_mode)) {
		printf("directory\n");
	} else if (S_ISREG(inode.i_mode)) {
		printf("regular file\n");
	} else if (S_ISCHR(inode.i_mode)) {
		printf("character device\n");
	} else if (S_ISBLK(inode.i_mode)) {
		printf("block device\n");
	} else if (S_ISFIFO(inode.i_mode)) {
		printf("fifo\n");
	} else if (S_ISSOCK(inode.i_mode)) {
		printf("socket\n");
	} else if (S_ISLNK(inode.i_mode)) {
		printf("symbolic link\n");
	} else {
		printf("Unkown tipe\n");
	}

	printf("Owner: %d\n", inode.i_uid);

	date = inode.i_atime;
	strftime(time_string, sizeof(time_string), format, localtime(&date));
	printf("Access time: %s\n", time_string);
	
	date = inode.i_ctime;
	strftime(time_string, sizeof(time_string), format, localtime(&date));
	printf("Creation time: %s\n", time_string);
	
	date = inode.i_mtime;
	strftime(time_string, sizeof(time_string), format, localtime(&date));
	printf("Modification time: %s\n", time_string);
	
	date = inode.i_dtime;
	strftime(time_string, sizeof(time_string), format, localtime(&date));
	printf("Deletion time: %s\n", time_string);
	
	printf("Group: %d\n", inode.i_gid);
	printf("Links: %d\n", inode.i_links_count);
	printf("Flags: %d\n", inode.i_flags);
}

void ls(struct ext2_inode inode, struct ext2_super_block super) {
	if(!S_ISDIR(inode.i_mode)) {
		fprintf(stderr, "Diretório inválido, i_mode: %d\n", inode.i_mode);
		print_inode_info(inode);
		return;
	}
	struct ext2_dir_entry_2 *entry;
	unsigned int bytes_read = 0;
	int bs = block_size(super);
	void *block = malloc(bs);
	char file_name[EXT2_NAME_LEN+1];

	lseek(storage_device, block_offset(inode.i_block[0], super), SEEK_SET);
	read(storage_device, block, bs);
	entry = (struct ext2_dir_entry_2 *)block;
	while(bytes_read < inode.i_size && entry->inode) {
		memcpy(file_name, entry->name, entry->name_len);
		file_name[entry->name_len] = '\0';
		printf("%s\t", file_name);
		bytes_read += entry->rec_len;
		entry = (void*) entry +  entry->rec_len;
	}
	printf("\n");
	free(block);
}
