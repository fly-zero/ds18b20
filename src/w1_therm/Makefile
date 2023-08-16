target=w1_therm
src=w1_therm.cpp sqlite_storage.cpp influx_storage.cpp
obj=w1_therm.o sqlite_storage.o influx_storage.o
libs=-lsqlite3 -lcurl
defs=
cxxflag=
lnkflag=
LNK=g++
CXX=g++

.PHONY: all debug release clean

all: debug

debug: cxxflag+=-g3
debug: defs+=-D_DEBUG_
debug: ${target}

release: cxxflag+=-flto -O3
release: lnkflag+=-flto -O3
release: ${target}

clean:
	rm -f ${obj} ${target}

${target}: ${obj}
	${LNK} ${lnkflag} $^ -o $@ ${libs}

%.o: %.cpp
	${CXX} -c -Wall -Werror -Wextra -std=c++20 ${cxxflag} ${defs} -o $@ $<
