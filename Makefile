# Makefile for ReponoDB

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -I/opt/homebrew/opt/openssl/include
LDFLAGS = -L/opt/homebrew/opt/openssl/lib -lssl -lcrypto
.PHONY: all clean run

all: repono

repono: repono.cpp
	$(CXX) $(CXXFLAGS) -o repono repono.cpp $(LDFLAGS)

clean:
	rm -f repono

run: repono
	./repono
