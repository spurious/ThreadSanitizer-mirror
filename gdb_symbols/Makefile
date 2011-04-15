CXX=g++
DBGINFO=-g

main:	main.o symbol_table.o
	$(CXX) $^ -o main -lpthread

%.o:	%.cc %.h
	$(CXX) $(DBGINFO) $< -c

%.o:	%.cc
	$(CXX) $(DBGINFO) $< -c

clean:
	rm main
	rm *.o
