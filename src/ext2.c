#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/magic.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include "ext2.h"

// help macros
#define BLOCK_SIZE (1024 << super->s_log_block_size)
#define BLOCK_OFFSET(block)(BASE_OFFSET + (block - 1) * BLOCK_SIZE)
#define BLOCKS_PER_GROUP (super->s_blocks_per_group)
#define INODES_PER_GROUP (super->s_inodes_per_group)
#define INODE_GROUP(inode)((inode - 1) / INODES_PER_GROUP)
#define INODE_INDEX(inode)((inode - 1) % INODES_PER_GROUP)

int roundup(double x) {
	if(x > (int)x) {
		return (int)(x+1);
	}
	return (int)x;
}

// Variáveis globais
int storage_device;
struct ext2_super_block *super;
struct ext2_inode *curdir;

// Funçoes principais
void sb(); // printa as informações do super bloco
void ls(char*); // printa as entradas do diretorio passado
void cd(char*); // muda o curdir para o inode de nome *name
void status(char*); // printa o stat do diretório como nome *name
void find(); // printa a árvore de caminhos

// Funções auxiliares
struct ext2_super_block *get_super_block();
struct ext2_group_desc* get_group_desc(int group);
struct ext2_inode* get_root_directory();
struct ext2_inode* read_inode(__le32 inode_nr);
struct ext2_inode* get_inode_by_name(char *name, __le32*); // o inode_nr é gravado no __le32*

// funções do shell
struct cmd {
	char *command;
	char *operand;
};
int getcmd(char *buf, int nbuf);
struct cmd* parsecmd(char*);
void runcmd(struct cmd*);

int main(int argc, char *argv[]) {
	char imgpath[255];
	static char buf[255];

	if(argc <= 1) {
		fprintf(stderr, "Informe um arquivo de imagem.\n");
		return EXIT_FAILURE;
	}

	if((storage_device = open(argv[1], O_RDONLY)) < 0) {
		fprintf(stderr, "Erro ao abir o arquivo %s, certifique-se que ele existe\n", argv[1]);
		return EXIT_FAILURE;
	}
	super = get_super_block();
	if(super->s_magic != (__le16)EXT2_SUPER_MAGIC) {
		fprintf(stderr, "A imagem %s não corresponde a um formato ext2\n", argv[1]);
		fprintf(stderr, "EXT2_SUPER_MAGIC: 0x%hx\n", (__le16)EXT2_SUPER_MAGIC);
		fprintf(stderr, "Magic do superblock: 0x%hx\n", super->s_magic);
		return EXIT_FAILURE;
	}
	curdir = get_root_directory();

	while(getcmd(buf, sizeof(buf)) >= 0) {
		buf[strlen(buf)-1] = '\0';
		if(buf[0] == 'q' && buf[1] == '\0') {
			printf("Exit\n");
			break;
		}
		runcmd(parsecmd(buf));
	}

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

void sb() {
	// Print superblock information
	printf("Inodes count:             %d\n", super->s_inodes_count);
	printf("Blocks count:             %d\n", super->s_blocks_count);
	printf("Reserved blocks:          %d\n", super->s_r_blocks_count);
	printf("Free blocks:              %d\n", super->s_free_blocks_count);
	printf("Free inodes:              %d\n", super->s_free_inodes_count);
	printf("First data block:         %d\n", super->s_first_data_block);
	printf("Block size (log2 - 10)    %d\n", super->s_log_block_size);
	printf("Blocks per group:         %d\n", super->s_blocks_per_group);
	printf("Inodes per group:         %d\n", super->s_inodes_per_group);
	printf("Magic signature:          0x%hx\n", super->s_magic);
	printf("First non-reserved inode: %d\n", super->s_first_ino);
	printf("Inode size:               %d\n", super->s_inode_size);
}

struct ext2_group_desc* get_group_desc(int group) {
	struct ext2_group_desc *gd = (struct ext2_group_desc*)malloc(sizeof(struct ext2_group_desc));
	lseek(
		storage_device,
		BASE_OFFSET + BLOCK_SIZE + group * BLOCKS_PER_GROUP * BLOCK_SIZE,
		SEEK_SET
	);
	read(storage_device, gd, sizeof(*gd));
	return gd;
}

int calculate_nr_groups() {
	// Calcula quantos grupos de blocos há no disco
	// A conta é feita pelo número de inodes e de blocks
	// Caso haja uma diferença há um erro no superbloco e um valor negativo é retornado
	int nr_groups_inodes = roundup((double)super->s_inodes_count / (double)super->s_inodes_per_group);
	int nr_groups_blocks = roundup((double)super->s_blocks_count / (double)super->s_blocks_per_group);
	if(nr_groups_inodes != nr_groups_blocks) {
		fprintf(stderr, "Erro no superbloco, o número de grupos não pode ser calculado.\n");
		return -1;
	}
	return nr_groups_inodes;
}

struct ext2_inode* get_root_directory() {
	return read_inode(EXT2_ROOT_INO);
}

void cd(char *name) {
	struct ext2_inode *dir;
	__le32 inode_nr;
	if(name == NULL) {
		dir = get_root_directory();
	} else {
		dir = get_inode_by_name(name, &inode_nr);
	}
	if(!dir) {
		fprintf(stderr, "%s não é um caminho válido.\n", name);
		return;
	}
	if(!S_ISDIR(dir->i_mode)) {
		fprintf(stderr, "%s não é um diretório.\n", name);
		free(dir);
		return;
	}
	free(curdir);
	curdir = dir;
}

void status(char *name) {
	struct ext2_inode *inode;
	char time_string[100];
	time_t date;
	char *format = "%Y-%m-%d %H:%M:%S %Z00";
	__le32 inode_nr;
	if(name == NULL) {
		fprintf(stderr, "É necessário informar um caminho.\n");
		return;
	}
	
	inode = get_inode_by_name(name, &inode_nr);
	if(!inode) {
		fprintf(stderr, "%s não é um caminho válido.\n", name);
		return;
	}

	printf("  File: %s\n", name);

	printf("  Size: %d\t\t", inode->i_size);
	printf("Blocks: %d\t\t", inode->i_blocks);
	printf("IO Block: %d\t", BLOCK_SIZE);
	if(S_ISDIR(inode->i_mode)) {
		printf("directory\n");
	} else if (S_ISREG(inode->i_mode)) {
		printf("regular file\n");
	} else if (S_ISCHR(inode->i_mode)) {
		printf("character device\n");
	} else if (S_ISBLK(inode->i_mode)) {
		printf("block device\n");
	} else if (S_ISFIFO(inode->i_mode)) {
		printf("fifo\n");
	} else if (S_ISSOCK(inode->i_mode)) {
		printf("socket\n");
	} else if (S_ISLNK(inode->i_mode)) {
		printf("symbolic link\n");
	} else {
		printf("Unknown tipe\n");
	}

	printf("Device: \t\t");
	printf("Inode: %d\t\t", inode_nr);
	printf("Links: %hd\n", inode->i_links_count);

	printf("Access: ()\t");
	printf("Uid: (%hd/ )\t", inode->i_uid);
	printf("Gid: (%hd/ )\n", inode->i_gid);


	date = inode->i_atime;
	strftime(time_string, sizeof(time_string), format, localtime(&date));
	printf("Access: %s\n", time_string);
	date = inode->i_mtime;
	strftime(time_string, sizeof(time_string), format, localtime(&date));
	printf("Modify: %s\n", time_string);
	date = inode->i_ctime;
	strftime(time_string, sizeof(time_string), format, localtime(&date));
	printf("Change: %s\n", time_string);
	
	printf(" Birth: -\n");
	free(inode);
}

struct ext2_inode* read_inode(__le32 inode_nr) {
	struct ext2_group_desc *gd = get_group_desc(INODE_GROUP(inode_nr));
	long int offset = BLOCK_OFFSET(
		gd->bg_inode_table + INODE_GROUP(inode_nr)*BLOCKS_PER_GROUP
	) + INODE_INDEX(inode_nr) * sizeof(struct ext2_inode);
	struct ext2_inode* inode = (struct ext2_inode*)malloc(sizeof(struct ext2_inode));
	lseek(storage_device, offset, SEEK_SET);
	read(storage_device, inode, sizeof(*inode));
	free(gd);
	return inode;
}

struct ext2_inode* get_inode_by_name(char *name, __le32* inode_nr) {
	struct ext2_dir_entry_2 *entry;
	unsigned int bytes_read = 0;
	void *block = malloc(BLOCK_SIZE);
	char file_name[EXT2_NAME_LEN+1];
	struct ext2_inode *inode;

	lseek(storage_device, BLOCK_OFFSET(curdir->i_block[0]), SEEK_SET);
	read(storage_device, block, BLOCK_SIZE);
	entry = (struct ext2_dir_entry_2*)block;
	while(bytes_read < curdir->i_size && entry->inode) {
		memcpy(file_name, entry->name, entry->name_len);
		file_name[entry->name_len] = '\0';
		if(!strcmp(file_name, name)) {
			// inode encontrado
			*inode_nr = entry->inode;
			free(block);
			inode = read_inode(*inode_nr);
			return inode;
		}
		bytes_read += entry->rec_len;
		entry = (void*) entry + entry->rec_len;
	}
	*inode_nr = -1;
	return NULL;
}

void ls(char* name) {
	__le32 inode_nr;
	if(name == NULL) name = ".";
	struct ext2_inode *inode = get_inode_by_name(name, &inode_nr);
	if(!inode) {
		fprintf(stderr, "%s não é um caminho válido.\n", name);
		return;
	}
	if(!S_ISDIR(inode->i_mode)) {
		printf("%s\n", name);
		free(inode);
		return;
	}
	struct ext2_dir_entry_2 *entry;
	unsigned int bytes_read = 0;
	void *block = malloc(BLOCK_SIZE);
	char file_name[EXT2_NAME_LEN+1];

	lseek(storage_device, BLOCK_OFFSET(inode->i_block[0]), SEEK_SET);
	read(storage_device, block, BLOCK_SIZE);
	entry = (struct ext2_dir_entry_2 *)block;
	while(bytes_read < inode->i_size && entry->inode) {
		memcpy(file_name, entry->name, entry->name_len);
		file_name[entry->name_len] = '\0';
		printf("%s\t", file_name);
		bytes_read += entry->rec_len;
		entry = (void*) entry +  entry->rec_len;
	}
	printf("\n");
	free(inode);
	free(block);
}

void find() {
}

int getcmd(char *buf, int nbuf) {
	// implementação igual ao do TP1
	if(isatty(fileno(stdin)))
		printf("$ ");
	memset(buf, 0, nbuf);
	fgets(buf, nbuf, stdin);
	if(buf[0] == 0) // EOF
		return -1;
	return 0;
}

struct cmd* parsecmd(char* line) {
	int counter = 0;
	struct cmd* cmd = (struct cmd*)malloc(sizeof(struct cmd));

	cmd->command = strsep(&line, " ");
	cmd->operand = strsep(&line, " ");
	return cmd;
}

void runcmd(struct cmd* cmd) {
	if(!strcmp(cmd->command, "cd")) {
		cd(cmd->operand);
	} else if (!strcmp(cmd->command, "ls")) {
		ls(cmd->operand);
	} else if (!strcmp(cmd->command, "stat")) {
		status(cmd->operand);
	} else if (!strcmp(cmd->command, "sb")) {
		sb();
	} else if (!strcmp(cmd->command, "find")) {
		find();
	} else {
		printf("%s: commando inválido.\n", cmd->command);
	}
	free(cmd);
}
