CC = gcc
CFLAGS = -O2 -Wall
LIBS = -lm

all: englang

englang: englang.c
	$(CC) $(CFLAGS) -o englang englang.c $(LIBS)

clean:
	rm -f englang

run-hello: englang
	./englang examples/hello.eng

run-fizzbuzz: englang
	./englang examples/fizzbuzz.eng

run-fibonacci: englang
	./englang examples/fibonacci.eng

run-factorial: englang
	./englang examples/factorial.eng

run-sort: englang
	./englang examples/sort.eng

run-primes: englang
	./englang examples/primes.eng

run-advanced: englang
	./englang examples/advanced.eng

run-all: englang
	@for f in examples/*.eng; do \
		echo "=== $$f ==="; \
		./englang $$f; \
		echo ""; \
	done
