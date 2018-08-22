output: main.o
	g++ main.o -o server std=c++11 -lpthread

main.o: main.cpp
	g++ -c main.cpp

clean:
	rm *.o server