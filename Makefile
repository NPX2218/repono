# Makefile for ReponoDB

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

.PHONY: all clean run

all: repono

repono: repono.cpp
	$(CXX) $(CXXFLAGS) -o repono repono.cpp

clean:
	rm -f repono

run: repono
	./repono
