target=w1_therm
src=w1_therm.cpp
obj=w1_therm.o
libs=-lsqlite3
defs=
cxxflag=

.PHONY: all debug release clean

all: debug

debug: cxxflag+=-g3
debug: defs+=-D_DEBUG_
debug: ${target}

release: cxxflag+=-O3
release: ${target}

clean:
	rm -f ${obj} ${target}

${target}: ${obj}
	g++ $^ -o $@ ${libs}

%.o: %.cpp
	g++ -c -Wall -Werror -Wextra -std=c++11 ${cxxflag} ${defs} -o $@ $<
