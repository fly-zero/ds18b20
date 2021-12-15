target = a.out

.PHONY: all
all: $(target)

$(target): gpio.o
	g++ $^ -o $@ -lgpiodcxx

gpio.o: gpio.cpp
	g++ -c -g3 $< -o $@

.PHONY: clean
clean:
	rm -f gpio.o $(target)
