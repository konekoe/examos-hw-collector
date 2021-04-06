CXX      = g++
OBJ      = hw_collector.o
LINKOBJ  = hw_collector.o
CFLAGS = -std=c++17 -Wall -g -c -Wno-parentheses
LIBS = -lboost_filesystem -lboost_system -lssl -lcrypto -lpci -lpthread
BIN	 = examtool_hw_collector

mkfile_dir := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

.PHONY: all all-before all-after clean clean-custom default
default: build ;
all: build ;

install: $(BIN)
	mkdir -p $(DESTDIR)usr/bin && cp $(BIN) $(DESTDIR)usr/bin/$(BIN) && cp dmi2json $(DESTDIR)usr/bin/dmi2json
	mkdir -p $(DESTDIR)usr/lib/systemd/system && cp examtool-hw-collector.service $(DESTDIR)usr/lib/systemd/system/examtool-hw-collector.service

build: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(LINKOBJ) -o $(BIN) $(LIBS)

./%.o: ./%.cpp
	$(CXX) $(CFLAGS) $< -o $@ $(LIBS)

.PHONY : clean
clean :
	-rm -rf $(OBJ) $(BIN) usr/
