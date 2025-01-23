build:
	icx -o img-bench img.c -lpng -march=native -O3 -DBENCH
	icx -o img img.c -lpng -march=native -O3

clean:
	rm -f img img-bench out.png

benchmark: build
	./img-bench

out: build
	./img

all: clean build benchmark out