all:
	gcc -o img img.c -lpng -march=native

clean:
	rm -f img