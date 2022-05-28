target=w1_therm
src=w1_therm.cpp
obj=w1_therm.o
libs=-lsqlite3
defs=
cxxflag=
LNK=g++
CXX=g++

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
	${LNK} $^ -o $@ ${libs}

%.o: %.cpp
	${CXX} -c -Wall -Werror -Wextra -std=c++20 ${cxxflag} ${defs} -o $@ $<
