shell: ext2.c ext2.h
	cc -g -o shell ext2.c

clean:
	rm shell

run:
	./shell ../fs-0x00dcc605-ext2-10240.img

valgrind:
	valgrind --leak-check=full ./shell ../fs-0x00dcc605-ext2-10240.img

test:
	./shell << echo "find"
