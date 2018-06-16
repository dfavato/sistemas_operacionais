dcc_fs_shell: ext2.c ext2.h
	cc -g -o dcc_fs_shell ext2.c

clean:
	rm dcc_fs_shell

run:
	./dcc_fs_shell ../fs-0x00dcc605-ext2-10240.img

valgrind:
	valgrind --leak-check=full ./dcc_fs_shell ../fs-0x00dcc605-ext2-10240.img
